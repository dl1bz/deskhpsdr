/*
 * Native RTTY/FSK TX engine for deskHPSDR.
 *
 * TCI supplies plain text and operating parameters.  This module performs
 * ITA2 encoding and sample-timed continuous-phase FSK generation.
 */

#include <glib.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "mode.h"
#include "radio.h"
#include "rtty_engine.h"

#define RTTY_QUEUE_SIZE 32768
#define RTTY_LTRS 0x1f
#define RTTY_FIGS 0x1b
#define RTTY_SPACE 0x04
#define RTTY_CR 0x08
#define RTTY_LF 0x02
#define RTTY_TWO_PI (2.0 * M_PI)

typedef enum {
  SHIFT_LTRS = 0,
  SHIFT_FIGS = 1
} SHIFT_STATE;

typedef enum {
  PHASE_IDLE = 0,
  PHASE_PREFIX_MARK,
  PHASE_STREAM,
  PHASE_FILL_MARK,
  PHASE_FILL_SPACE,
  PHASE_TAIL_MARK,
  PHASE_WAIT_RX
} TX_PHASE;

static GMutex rtty_mutex;
static int rtty_initialized = 0;
static uint8_t queue[RTTY_QUEUE_SIZE];
static int q_in = 0;
static int q_out = 0;
static SHIFT_STATE encoder_shift = SHIFT_LTRS;

static double cfg_baud = 45.4545454545;
static int cfg_shift = 170;
static int cfg_reverse = 0;
static double cfg_stop_bits = 1.5;
static int cfg_uos = 1;
static RTTY_FILL_MODE cfg_fill = RTTY_FILL_LTRS;
static int cfg_start_crlf = 1;
static int cfg_idle_timeout = 25;

static int active = 0;
/*
 * Lock-free mirror used by TCI while holding tci_mutex.  The RTTY engine
 * itself still owns active under rtty_mutex; keeping this mirror avoids the
 * tci_mutex -> rtty_mutex lock order that would otherwise invert against the
 * MOX callbacks (rtty_mutex -> radio_set_mox() -> tci_mutex).
 */
static gint active_atomic = 0;
static guint tx_generation = 0;
static int stopping = 0;
static int draining = 0;
static TX_PHASE tx_phase = PHASE_IDLE;
static int prefix_pending = 0;
static int prefix_ltrs_pending = 0;
static double phase_acc = 0.0;

/*
 * Match rttyTCI's proven 5 ms soft FSK transition.  Only the instantaneous
 * NCO frequency is ramped; phase continuity and the sample-accurate Baudot
 * timing remain unchanged.
 */
#define RTTY_SHAPE_MS 5.0
static int shape_initialized = 0;
static double shape_current_hz = 0.0;
static double shape_start_hz = 0.0;
static double shape_target_hz = 0.0;
static int shape_samples_total = 0;
static int shape_samples_remaining = 0;

static double timing_samples = 0.0;
static double mark_samples = 0.0;
static double idle_samples = 0.0;
static int frame_active = 0;
static int frame_bit = 0;
static uint8_t frame_symbol = 0;
static int frame_is_fill = 0;

static const char letters[32] = {
  '\0', 'E', '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
  'T', 'Z', 'L', 'W', 'H', 'Y', 'P', 'Q', 'O', 'B', 'G', '\0', 'M', 'X', 'V', '\0'
};

static const char figures[32] = {
  '\0', '3', '\n', '-', ' ', '\'', '8', '7', '\r', '$', '4', '\a', ',', '!', ':', '(',
  '5', '"', ')', '2', '#', '6', '0', '1', '9', '?', '&', '\0', '.', '/', ';', '\0'
};

static int queue_empty(void) {
  return q_in == q_out;
}

static int queue_put(uint8_t symbol) {
  int next = (q_in + 1) % RTTY_QUEUE_SIZE;
  if (next == q_out) {
    return 0;
  }
  queue[q_in] = symbol & 0x1f;
  q_in = next;
  return 1;
}

static int queue_get(uint8_t *symbol) {
  if (queue_empty()) {
    return 0;
  }
  *symbol = queue[q_out];
  q_out = (q_out + 1) % RTTY_QUEUE_SIZE;
  return 1;
}

static int find_symbol(const char *table, char c, uint8_t *symbol) {
  for (int i = 0; i < 32; i++) {
    if (table[i] == c) {
      *symbol = (uint8_t)i;
      return 1;
    }
  }
  return 0;
}

static int emit_shift(SHIFT_STATE wanted) {
  if (encoder_shift == wanted) {
    return 1;
  }
  if (!queue_put(wanted == SHIFT_FIGS ? RTTY_FIGS : RTTY_LTRS)) {
    return 0;
  }
  encoder_shift = wanted;
  return 1;
}

static int encode_char_internal(char input) {
  uint8_t symbol;
  char c = (char)toupper((unsigned char)input);
  if (c == '\n') {
    if (!queue_put(RTTY_CR) || !queue_put(RTTY_LF)) { return 0; }
    return 1;
  }
  if (c == '\r') {
    return queue_put(RTTY_CR);
  }
  if (find_symbol(letters, c, &symbol)) {
    if (!emit_shift(SHIFT_LTRS) || !queue_put(symbol)) { return 0; }
    if (cfg_uos && c == ' ') {
      if (!queue_put(RTTY_LTRS)) { return 0; }
      encoder_shift = SHIFT_LTRS;
    }
    return 1;
  }
  if (find_symbol(figures, c, &symbol)) {
    if (!emit_shift(SHIFT_FIGS) || !queue_put(symbol)) { return 0; }
    return 1;
  }
  if (!queue_put(RTTY_SPACE)) { return 0; }
  if (cfg_uos) {
    if (!queue_put(RTTY_LTRS)) { return 0; }
    encoder_shift = SHIFT_LTRS;
  }
  return 1;
}

static int encode_char(char input) {
  int q_checkpoint = q_in;
  SHIFT_STATE shift_checkpoint = encoder_shift;
  if (encode_char_internal(input)) {
    return 1;
  }
  /*
   * A single input character may need multiple ITA2 symbols (for example
   * FIGS+digit or SPACE+LTRS with UOS).  Never leave a partial character in
   * the ring when it fills between those symbols.
   */
  q_in = q_checkpoint;
  encoder_shift = shift_checkpoint;
  return 0;
}

static gboolean rtty_mox_on_cb(gpointer data) {
  guint generation = GPOINTER_TO_UINT(data);
  rtty_engine_init();
  g_mutex_lock(&rtty_mutex);
  /*
   * START is queued onto the GTK main loop.  The originating RTTY session may
   * already have been aborted (or superseded by a new session) by the time the
   * callback runs.  Never key MOX for a stale generation.
   *
   * Keep the mutex held through radio_set_mox(): abort/start can originate on
   * a TCI worker thread, so checking and keying must be one atomic operation
   * with respect to the RTTY lifecycle.
   */
  if (generation == tx_generation && active && CAT_rtty_is_active && !mox) {
    radio_set_mox(1);
  }
  g_mutex_unlock(&rtty_mutex);
  return G_SOURCE_REMOVE;
}

static gboolean rtty_mox_off_cb(gpointer data) {
  guint generation = GPOINTER_TO_UINT(data);
  rtty_engine_init();
  g_mutex_lock(&rtty_mutex);
  /*
   * Ignore an OFF callback belonging to an older RTTY session.  Without this
   * guard a rapid abort/restart can let the old callback switch the new
   * session back to RX and clear CAT_rtty_is_active underneath it.
   */
  if (generation != tx_generation) {
    g_mutex_unlock(&rtty_mutex);
    return G_SOURCE_REMOVE;
  }
  /*
   * Keep CAT_rtty_is_active asserted until MOX is actually removed.  Otherwise
   * transmitter.c can fall back to the normal DIGL/DIGU WDSP path for a short
   * interval between the sample-accurate MARK tail and this GTK idle callback.
   */
  if (mox) {
    radio_set_mox_immediate(0);
  }
  active = 0;
  g_atomic_int_set(&active_atomic, 0);
  stopping = 0;
  draining = 0;
  tx_phase = PHASE_IDLE;
  idle_samples = 0.0;
  CAT_rtty_is_active = 0;
  g_mutex_unlock(&rtty_mutex);
  return G_SOURCE_REMOVE;
}

void rtty_engine_init(void) {
  if (rtty_initialized) { return; }
  g_mutex_init(&rtty_mutex);
  rtty_initialized = 1;
}

#define SETTER(name, type, field, expr) \
  void name(type value) { rtty_engine_init(); g_mutex_lock(&rtty_mutex); field = (expr); g_mutex_unlock(&rtty_mutex); }
#define GETTER(name, type, field) \
  type name(void) { type value; rtty_engine_init(); g_mutex_lock(&rtty_mutex); value = field; g_mutex_unlock(&rtty_mutex); return value; }

SETTER(rtty_engine_set_baud, double, cfg_baud, (value >= 10.0 && value <= 300.0) ? value : cfg_baud)
GETTER(rtty_engine_get_baud, double, cfg_baud)
SETTER(rtty_engine_set_shift, int, cfg_shift, (value >= 1 && value <= 2000) ? value : cfg_shift)
GETTER(rtty_engine_get_shift, int, cfg_shift)
SETTER(rtty_engine_set_reverse, int, cfg_reverse, value ? 1 : 0)
GETTER(rtty_engine_get_reverse, int, cfg_reverse)
SETTER(rtty_engine_set_stop_bits, double, cfg_stop_bits, (value == 1.0 || value == 1.5 ||
    value == 2.0) ? value : cfg_stop_bits)
GETTER(rtty_engine_get_stop_bits, double, cfg_stop_bits)
SETTER(rtty_engine_set_uos, int, cfg_uos, value ? 1 : 0)
GETTER(rtty_engine_get_uos, int, cfg_uos)
SETTER(rtty_engine_set_fill, RTTY_FILL_MODE, cfg_fill,
       value == RTTY_FILL_LTRS ? RTTY_FILL_LTRS :
       value == RTTY_FILL_SPACE ? RTTY_FILL_SPACE : RTTY_FILL_MARK)
GETTER(rtty_engine_get_fill, RTTY_FILL_MODE, cfg_fill)
SETTER(rtty_engine_set_start_crlf, int, cfg_start_crlf, value ? 1 : 0)
GETTER(rtty_engine_get_start_crlf, int, cfg_start_crlf)
SETTER(rtty_engine_set_idle_timeout, int, cfg_idle_timeout, (value >= 1 && value <= 300) ? value : cfg_idle_timeout)
GETTER(rtty_engine_get_idle_timeout, int, cfg_idle_timeout)

static int rtty_engine_start_locked(void) {
  if (active) {
    return 0;
  }
  tx_generation++;
  if (tx_generation == 0) {
    tx_generation = 1;
  }
  active = 1;
  g_atomic_int_set(&active_atomic, 1);
  stopping = 0;
  draining = 0;
  tx_phase = PHASE_PREFIX_MARK;
  prefix_pending = 1;
  prefix_ltrs_pending = 3;
  shape_initialized = 0;
  shape_samples_total = 0;
  shape_samples_remaining = 0;
  timing_samples = 0.0;
  mark_samples = 0.0;
  idle_samples = 0.0;
  frame_active = 0;
  frame_bit = 0;
  encoder_shift = SHIFT_LTRS;
  q_in = q_out = 0;
  CAT_rtty_is_active = 1;
  if (cfg_start_crlf) {
    queue_put(RTTY_CR);
    queue_put(RTTY_LF);
  }
  return 1;
}

int rtty_engine_start(void) {
  int start_tx;
  guint generation = 0;
  rtty_engine_init();
  g_mutex_lock(&rtty_mutex);
  start_tx = rtty_engine_start_locked();
  if (start_tx) {
    generation = tx_generation;
  }
  g_mutex_unlock(&rtty_mutex);
  if (start_tx) {
    g_idle_add(rtty_mox_on_cb, GUINT_TO_POINTER(generation));
  }
  return start_tx;
}

int rtty_engine_queue_text(const char *text) {
  int queued_chars = 0;
  int start_tx = 0;
  guint generation = 0;
  if (text == NULL || text[0] == '\0') { return 0; }
  rtty_engine_init();
  g_mutex_lock(&rtty_mutex);
  if (rtty_engine_start_locked()) {
    start_tx = 1;
    generation = tx_generation;
  }
  if (!stopping && !draining) {
    while (*text) {
      if (text[0] == '\r' && text[1] == '\n') {
        if (!encode_char('\n')) { break; }
        text += 2;
      } else {
        if (!encode_char(*text++)) { break; }
      }
      queued_chars++;
    }
    if (queued_chars > 0) {
      idle_samples = 0.0;
      if (tx_phase == PHASE_FILL_MARK || tx_phase == PHASE_FILL_SPACE) {
        tx_phase = PHASE_STREAM;
      }
    }
  }
  g_mutex_unlock(&rtty_mutex);
  if (start_tx) {
    g_idle_add(rtty_mox_on_cb, GUINT_TO_POINTER(generation));
  }
  return queued_chars;
}

void rtty_engine_drain(void) {
  rtty_engine_init();
  g_mutex_lock(&rtty_mutex);
  if (active && !stopping && !draining) {
    draining = 1;
    idle_samples = 0.0;
    /*
     * If the stream has already fallen into a continuous fill state, there
     * is no queued payload left to preserve.  Go directly to the normal
     * MARK tail.  A currently active framed LTRS fill is allowed to finish
     * so we never cut a Baudot frame in the middle.
     */
    if (tx_phase == PHASE_FILL_MARK || tx_phase == PHASE_FILL_SPACE) {
      stopping = 1;
      draining = 0;
      tx_phase = PHASE_TAIL_MARK;
      mark_samples = 0.0;
    }
  }
  g_mutex_unlock(&rtty_mutex);
}

void rtty_engine_stop(void) {
  rtty_engine_init();
  g_mutex_lock(&rtty_mutex);
  if (active && !stopping) {
    stopping = 1;
    draining = 0;
    q_in = q_out = 0;
    frame_active = 0;
    frame_bit = 0;
    timing_samples = 0.0;
    tx_phase = PHASE_TAIL_MARK;
    mark_samples = 0.0;
    idle_samples = 0.0;
  }
  g_mutex_unlock(&rtty_mutex);
}

void rtty_engine_abort(void) {
  int was_active;
  guint generation;
  rtty_engine_init();
  g_mutex_lock(&rtty_mutex);
  was_active = active;
  generation = tx_generation;
  active = 0;
  g_atomic_int_set(&active_atomic, 0);
  stopping = 0;
  draining = 0;
  tx_phase = PHASE_IDLE;
  q_in = q_out = 0;
  frame_active = 0;
  timing_samples = 0.0;
  mark_samples = 0.0;
  idle_samples = 0.0;
  /*
   * Keep CAT_rtty_is_active asserted until rtty_mox_off_cb() has actually
   * removed MOX.  Otherwise tx_full_buffer() can briefly fall back to the
   * normal WDSP DIGL/DIGU path while the transmitter is still keyed.
   */
  g_mutex_unlock(&rtty_mutex);
  if (was_active) {
    g_idle_add(rtty_mox_off_cb, GUINT_TO_POINTER(generation));
  }
}

int rtty_engine_is_active(void) {
  return g_atomic_int_get(&active_atomic) != 0;
}

static void load_frame(uint8_t symbol, int is_fill, double samples_per_bit) {
  frame_symbol = symbol & 0x1f;
  frame_bit = 0;
  frame_active = 1;
  frame_is_fill = is_fill;
  if (timing_samples <= 0.0) {
    timing_samples += samples_per_bit;
  }
}

static double shaped_frequency(double target_hz, int sample_rate) {
  int ramp_samples;
  if (!shape_initialized) {
    shape_initialized = 1;
    shape_current_hz = target_hz;
    shape_start_hz = target_hz;
    shape_target_hz = target_hz;
    shape_samples_total = 0;
    shape_samples_remaining = 0;
    return target_hz;
  }
  if (fabs(shape_target_hz - target_hz) > 0.01) {
    ramp_samples = (int)(((double)sample_rate * RTTY_SHAPE_MS / 1000.0) + 0.5);
    if (ramp_samples < 1) {
      ramp_samples = 1;
    }
    shape_start_hz = shape_current_hz;
    shape_target_hz = target_hz;
    shape_samples_total = ramp_samples;
    shape_samples_remaining = ramp_samples;
  }
  if (shape_samples_remaining > 0 && shape_samples_total > 0) {
    double done = (double)(shape_samples_total - shape_samples_remaining + 1);
    double frac = done / (double)shape_samples_total;
    if (frac > 1.0) {
      frac = 1.0;
    }
    shape_current_hz = shape_start_hz
                       + ((shape_target_hz - shape_start_hz) * frac);
    shape_samples_remaining--;
  } else {
    shape_current_hz = shape_target_hz;
  }
  return shape_current_hz;
}

void rtty_engine_render_iq(double *iq, int frames, int sample_rate, int txmode) {
  double samples_per_bit;
  int finish_tx = 0;
  guint finish_generation = 0;
  if (iq == NULL || frames <= 0 || sample_rate <= 0) { return; }
  rtty_engine_init();
  g_mutex_lock(&rtty_mutex);
  if (active && txmode != modeDIGL && txmode != modeDIGU) {
    active = 0;
    g_atomic_int_set(&active_atomic, 0);
    stopping = 0;
    draining = 0;
    tx_phase = PHASE_IDLE;
    q_in = q_out = 0;
    frame_active = 0;
    frame_bit = 0;
    frame_is_fill = 0;
    timing_samples = 0.0;
    mark_samples = 0.0;
    idle_samples = 0.0;
    shape_initialized = 0;
    memset(iq, 0, (size_t)frames * 2U * sizeof(double));
    finish_generation = tx_generation;
    g_mutex_unlock(&rtty_mutex);
    g_idle_add(rtty_mox_off_cb, GUINT_TO_POINTER(finish_generation));
    return;
  }
  samples_per_bit = (double)sample_rate / cfg_baud;
  if (samples_per_bit < 1.0) { samples_per_bit = 1.0; }
  for (int n = 0; n < frames; n++) {
    int logical_mark = 1;
    int tx_mark;
    double freq = 0.0;
    if (!active) {
      iq[2 * n] = 0.0;
      iq[2 * n + 1] = 0.0;
      continue;
    }
    if (tx_phase == PHASE_PREFIX_MARK) {
      if (prefix_pending) {
        mark_samples = 0.300 * (double)sample_rate;
        prefix_pending = 0;
      }
      logical_mark = 1;
      mark_samples -= 1.0;
      if (mark_samples <= 0.0) {
        tx_phase = PHASE_STREAM;
      }
    } else if (tx_phase == PHASE_FILL_MARK) {
      /*
       * Plain MARK fill is an explicit logical MARK state, not an implicit
       * consequence of an empty stream.  REVERSE is applied later, exactly
       * as it is for every other logical MARK/SPACE state.
       * If no new text arrives, automatically leave TX after the configured
       * idle timeout.  rtty_engine_queue_text() resets idle_samples.
       */
      logical_mark = 1;
      idle_samples += 1.0;
      if (idle_samples >= (double)cfg_idle_timeout * (double)sample_rate) {
        stopping = 1;
        tx_phase = PHASE_TAIL_MARK;
        mark_samples = 0.0;
        idle_samples = 0.0;
      }
    } else if (tx_phase == PHASE_FILL_SPACE) {
      /*
       * SPACE fill is a continuous logical SPACE carrier.  REVERSE is applied
       * below, exactly as for data and MARK fill.
       */
      logical_mark = 0;
      idle_samples += 1.0;
      if (idle_samples >= (double)cfg_idle_timeout * (double)sample_rate) {
        stopping = 1;
        tx_phase = PHASE_TAIL_MARK;
        mark_samples = 0.0;
        idle_samples = 0.0;
      }
    } else if (tx_phase == PHASE_TAIL_MARK) {
      if (mark_samples <= 0.0) {
        mark_samples = 0.300 * (double)sample_rate;
      }
      logical_mark = 1;
      mark_samples -= 1.0;
      if (mark_samples <= 0.0) {
        /*
         * Hold physical MARK until MOX is really removed by the main thread.
         * Do not clear CAT_rtty_is_active here: transmitter.c uses it to keep
         * selecting the native-I/Q path.
         */
        tx_phase = PHASE_WAIT_RX;
        finish_tx = 1;
        finish_generation = tx_generation;
      }
    } else if (tx_phase == PHASE_WAIT_RX) {
      logical_mark = 1;
    } else {
      if (!frame_active) {
        uint8_t symbol;
        if (prefix_ltrs_pending > 0) {
          prefix_ltrs_pending--;
          load_frame(RTTY_LTRS, 0, samples_per_bit);
        } else if (queue_get(&symbol)) {
          idle_samples = 0.0;
          load_frame(symbol, 0, samples_per_bit);
        } else if (draining) {
          draining = 0;
          stopping = 1;
          tx_phase = PHASE_TAIL_MARK;
          mark_samples = 0.0;
          logical_mark = 1;
        } else if (cfg_fill == RTTY_FILL_LTRS) {
          encoder_shift = SHIFT_LTRS;
          load_frame(RTTY_LTRS, 1, samples_per_bit);
        } else if (cfg_fill == RTTY_FILL_SPACE) {
          tx_phase = PHASE_FILL_SPACE;
          logical_mark = 0;
        } else {
          tx_phase = PHASE_FILL_MARK;
          logical_mark = 1;
        }
      }
      if (tx_phase == PHASE_FILL_MARK) {
        logical_mark = 1;
      } else if (tx_phase == PHASE_FILL_SPACE) {
        logical_mark = 0;
      } else if (frame_active) {
        if (frame_is_fill) {
          idle_samples += 1.0;
          if (idle_samples >= (double)cfg_idle_timeout * (double)sample_rate) {
            stopping = 1;
            frame_active = 0;
            frame_bit = 0;
            frame_is_fill = 0;
            timing_samples = 0.0;
            tx_phase = PHASE_TAIL_MARK;
            mark_samples = 0.0;
            idle_samples = 0.0;
            logical_mark = 1;
          }
        }
        if (tx_phase == PHASE_TAIL_MARK) {
          logical_mark = 1;
        } else if (frame_bit == 0) {
          logical_mark = 0; /* start bit */
        } else if (frame_bit <= 5) {
          logical_mark = (frame_symbol >> (frame_bit - 1)) & 1;
        } else {
          logical_mark = 1; /* stop bits */
        }
        timing_samples -= 1.0;
        if (timing_samples <= 0.0) {
          if (frame_bit < 5) {
            frame_bit++;
            timing_samples += samples_per_bit;
          } else if (frame_bit == 5) {
            frame_bit = 6;
            timing_samples += cfg_stop_bits * samples_per_bit;
          } else {
            frame_active = 0;
            frame_bit = 0;
            if (frame_is_fill && cfg_fill == RTTY_FILL_MARK) {
              timing_samples = 0.0;
            }
            frame_is_fill = 0;
          }
        }
      } else {
        logical_mark = 1; /* MARK idle */
      }
    }
    /*
     * Native-I/Q RF reference, matched to the proven rttyTCI AFSK path.
     *
     * The protocol layer keeps the existing DIGI DUC reference shift:
     *
     *   DIGL: DUC = dial + digi_offset_l
     *   DIGU: DUC = dial - digi_offset_u
     *
     * The direct-I/Q path has the opposite spectral sign from the AFSK audio
     * path, therefore the physical MARK carrier is represented by:
     *
     *   DIGL: +digi_offset_l
     *   DIGU: -digi_offset_u
     *
     * With REVERSE=0 the SPACE tone follows the same RF placement as the
     * established AFSK implementation.  REVERSE swaps logical MARK and SPACE
     * uniformly for data, prefix, fill and tail.  Do not add another
     * 2210/1500-Hz correction anywhere else in the RTTY engine.
     */
    tx_mark = logical_mark;
    if (cfg_reverse) {
      tx_mark = !tx_mark;
    }
    if (active_receiver != NULL && txmode == modeDIGL) {
      const double mark_hz = (double)active_receiver->digi_offset_l;
      const double space_hz = mark_hz + (double)cfg_shift;
      freq = tx_mark ? mark_hz : space_hz;
    } else if (active_receiver != NULL && txmode == modeDIGU) {
      const double mark_hz = -(double)active_receiver->digi_offset_u;
      const double space_hz = mark_hz + (double)cfg_shift;
      freq = tx_mark ? mark_hz : space_hz;
    } else {
      /*
       * The active-engine mode guard above makes this unreachable during
       * Native RTTY TX.  Keep silence as a defensive fallback only.
       */
      freq = 0.0;
    }
    freq = shaped_frequency(freq, sample_rate);
    phase_acc += RTTY_TWO_PI * freq / (double)sample_rate;
    while (phase_acc >= RTTY_TWO_PI) { phase_acc -= RTTY_TWO_PI; }
    while (phase_acc < 0.0) { phase_acc += RTTY_TWO_PI; }
    iq[2 * n] = cos(phase_acc);
    iq[2 * n + 1] = sin(phase_acc);
  }
  g_mutex_unlock(&rtty_mutex);
  if (finish_tx) {
    g_idle_add(rtty_mox_off_cb, GUINT_TO_POINTER(finish_generation));
  }
}
