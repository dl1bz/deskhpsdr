/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT
* 2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
*   This source code has been forked and was adapted from piHPSDR by DL1YCF to deskHPSDR in October 2024
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

#include <gtk/gtk.h>

#include <math.h>
#include <stdint.h>

#include "radio.h"
#include "transmitter.h"
#include "vox.h"
#include "vfo.h"
#include "ext.h"
#include "mode.h"
#include "message.h"
#include "new_protocol.h"
#include "old_protocol.h"

#define VOX_TIMER_INTERVAL_MS       5
#define VOX_FM_CTCSS_DRAIN_US       350000
#define VOX_FM_CTCSS_CFC_DRAIN_US   800000
#define VOX_DRAIN_MAX_US            2000000
#define VOX_FENCE_TIMEOUT_US  1000000
#define VOX_DRAIN_QUIET_BLOCKS      2
#define VOX_DRAIN_QUIET_LEVEL  1.0e-6
#define VOX_P1_FIFO_GUARD_US    40000
#define VOX_P2_FIFO_GUARD_US    15000

enum vox_state {
  VOX_STATE_IDLE = 0,
  VOX_STATE_HANG,
  VOX_STATE_DRAIN,
  VOX_STATE_FENCE_ARMING,
  VOX_STATE_FENCE,
  VOX_STATE_GUARD
};

static GMutex vox_mutex;
static guint vox_timeout = 0;
static enum vox_state vox_state = VOX_STATE_IDLE;
static gint64 vox_deadline_us = 0;
static gint64 vox_last_activity_us = 0;
static gint64 vox_drain_started_us = 0;
static gint64 vox_fence_started_us = 0;
static gint64 vox_guard_deadline_us = 0;
static uint64_t vox_fence = 0;
static int vox_quiet_blocks = 0;

static double peak = 0.0;

static uint64_t vox_protocol_fence_begin(void) {
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    return old_protocol_tx_fence_begin();
  case NEW_PROTOCOL:
    return new_protocol_tx_fence_begin();
  default:
    return 0;
  }
}

static int vox_protocol_fence_complete(uint64_t fence) {
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    return old_protocol_tx_fence_complete(fence);
  case NEW_PROTOCOL:
    return new_protocol_tx_fence_complete(fence);
  default:
    return 1;
  }
}

static gint64 vox_protocol_fifo_guard_us(void) {
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    return VOX_P1_FIFO_GUARD_US;
  case NEW_PROTOCOL:
    return VOX_P2_FIFO_GUARD_US;
  default:
    return 0;
  }
}

static int vox_timeout_cb(gpointer data) {
  (void) data;
  gint64 now = g_get_monotonic_time();
  g_mutex_lock(&vox_mutex);
  if (vox_state == VOX_STATE_IDLE) {
    vox_timeout = 0;
    g_mutex_unlock(&vox_mutex);
    return G_SOURCE_REMOVE;
  }
  // Voice Keyer replay owns TX. Start a complete VOX hang interval
  // after replay has finished instead of dropping TX at an arbitrary
  // point in the old timer cycle.
  if (capture_state == CAP_XMIT) {
    vox_state = VOX_STATE_HANG;
    vox_last_activity_us = now;
    vox_deadline_us = now + (gint64) vox_hang * 1000;
    vox_drain_started_us = 0;
    vox_fence_started_us = 0;
    vox_guard_deadline_us = 0;
    vox_fence = 0;
    vox_quiet_blocks = 0;
    g_mutex_unlock(&vox_mutex);
    return G_SOURCE_CONTINUE;
  }
  switch (vox_state) {
  case VOX_STATE_HANG:
    if (now >= vox_deadline_us) {
      vox_state = VOX_STATE_DRAIN;
      vox_drain_started_us = now;
      vox_quiet_blocks = 0;
      vox_fence = 0;
    }
    break;
  case VOX_STATE_FENCE:
    if (vox_protocol_fence_complete(vox_fence)) {
      vox_state = VOX_STATE_GUARD;
      vox_guard_deadline_us = now + vox_protocol_fifo_guard_us();
    } else if (now - vox_fence_started_us >= VOX_FENCE_TIMEOUT_US) {
      // Do not leave the transmitter keyed forever after a protocol or
      // network failure. Under normal operation the fence completes in
      // less than the ring-buffer duration.
      t_print("VOX TX drain: protocol fence timeout\n");
      vox_state = VOX_STATE_GUARD;
      vox_guard_deadline_us = now + vox_protocol_fifo_guard_us();
    }
    break;
  case VOX_STATE_GUARD:
    if (now >= vox_guard_deadline_us) {
      vox_state = VOX_STATE_IDLE;
      vox_timeout = 0;
      g_mutex_unlock(&vox_mutex);
      // Do not hold vox_mutex while SetChannelState() performs the WDSP
      // slew-down. A new speech block arriving in this narrow window
      // schedules VOX ON again on the main loop after this transition.
      ext_set_vox(GINT_TO_POINTER(0));
      ext_vfo_update(NULL);
      return G_SOURCE_REMOVE;
    }
    break;
  case VOX_STATE_IDLE:
  case VOX_STATE_DRAIN:
  case VOX_STATE_FENCE_ARMING:
    break;
  }
  g_mutex_unlock(&vox_mutex);
  return G_SOURCE_CONTINUE;
}

double vox_get_peak(void) {
  double result = peak;
  return result;
}

void clear_vox(void) {
  peak = 0.0;
}

void update_vox(TRANSMITTER *tx) {
  // Voice Keyer Replay is handled by the periodic timer above.
  if (capture_state == CAP_XMIT) {
    return;
  }
  // Calculate peak microphone input. The buffer contains interleaved
  // left/right samples; VOX uses the left microphone channel.
  peak = 0.0;
  for (int i = 0; i < tx->buffer_size; i++) {
    double sample = fabs(tx->mic_input_buffer[i * 2]);
    if (sample > peak) {
      peak = sample;
    }
  }
  if (vox_enabled && !mox && !tune && !TxInhibit && peak > vox_threshold) {
    gint64 now = g_get_monotonic_time();
    int start_vox = 0;
    g_mutex_lock(&vox_mutex);
    if (vox_state == VOX_STATE_IDLE) {
      start_vox = 1;
    }
    // A new speech block cancels an in-progress WDSP/protocol drain.
    // The currently processed microphone block is therefore passed to
    // WDSP normally and TX remains active without a TX/RX transition.
    vox_state = VOX_STATE_HANG;
    vox_last_activity_us = now;
    vox_deadline_us = now + (gint64) vox_hang * 1000;
    vox_drain_started_us = 0;
    vox_fence_started_us = 0;
    vox_guard_deadline_us = 0;
    vox_fence = 0;
    vox_quiet_blocks = 0;
    if (vox_timeout == 0) {
      vox_timeout = g_timeout_add(VOX_TIMER_INTERVAL_MS, vox_timeout_cb, NULL);
    }
    g_mutex_unlock(&vox_mutex);
    if (start_vox) {
      g_idle_add(ext_set_vox, GINT_TO_POINTER(1));
      g_idle_add(ext_vfo_update, NULL);
    }
  }
}

int vox_tx_draining(void) {
  int result;
  g_mutex_lock(&vox_mutex);
  result = vox_state >= VOX_STATE_DRAIN;
  g_mutex_unlock(&vox_mutex);
  return result;
}

int vox_tx_output_enabled(void) {
  int result;
  g_mutex_lock(&vox_mutex);
  result = vox_state < VOX_STATE_FENCE_ARMING;
  g_mutex_unlock(&vox_mutex);
  return result;
}

void vox_tx_output_block(const TRANSMITTER *tx) {
  gint64 drain_started;
  g_mutex_lock(&vox_mutex);
  if (vox_state != VOX_STATE_DRAIN) {
    g_mutex_unlock(&vox_mutex);
    return;
  }
  drain_started = vox_drain_started_us;
  g_mutex_unlock(&vox_mutex);
  // Detect modulation rather than absolute RF level. Subtracting the
  // complex block mean also makes the test useful for an AM/FM carrier.
  double mean_i = 0.0;
  double mean_q = 0.0;
  for (int i = 0; i < tx->output_samples; i++) {
    mean_i += tx->iq_output_buffer[2 * i];
    mean_q += tx->iq_output_buffer[2 * i + 1];
  }
  if (tx->output_samples > 0) {
    mean_i /= tx->output_samples;
    mean_q /= tx->output_samples;
  }
  double activity = 0.0;
  for (int i = 0; i < tx->output_samples; i++) {
    double di = fabs(tx->iq_output_buffer[2 * i] - mean_i);
    double dq = fabs(tx->iq_output_buffer[2 * i + 1] - mean_q);
    if (di > activity) { activity = di; }
    if (dq > activity) { activity = dq; }
  }
  gint64 now = g_get_monotonic_time();
  gint64 elapsed = now - drain_started;
  int txmode = vfo_get_tx_mode();
  // A running CTCSS generator produces intentional, continuous FM
  // modulation, so output-level silence detection cannot distinguish it
  // from a speech tail. Only that case needs a measured fixed drain time.
  int fixed_tail_mode = txmode == modeFMN && tx->ctcss_enabled;
  gint64 fixed_tail_us = tx->cfc ? VOX_FM_CTCSS_CFC_DRAIN_US
                         : VOX_FM_CTCSS_DRAIN_US;
  int arm_fence = 0;
  g_mutex_lock(&vox_mutex);
  if (vox_state == VOX_STATE_DRAIN && vox_drain_started_us == drain_started) {
    if (activity <= VOX_DRAIN_QUIET_LEVEL) {
      vox_quiet_blocks++;
    } else {
      vox_quiet_blocks = 0;
    }
    if (fixed_tail_mode) {
      // CTCSS keeps the FM IQ output active indefinitely. Measure this
      // fallback from the point at which zero microphone blocks actually
      // start entering WDSP, not from the last threshold crossing. The
      // longer CFC value covers its substantially deeper FIR pipeline.
      arm_fence = elapsed >= fixed_tail_us;
    } else {
      // For all outputs without continuous CTCSS modulation, wait until
      // the real WDSP output has become quiet. The long timeout is only a
      // fail-safe against an unexpected continuously running generator.
      arm_fence = vox_quiet_blocks >= VOX_DRAIN_QUIET_BLOCKS ||
                  elapsed >= VOX_DRAIN_MAX_US;
    }
    if (arm_fence) {
      vox_state = VOX_STATE_FENCE_ARMING;
    }
  }
  g_mutex_unlock(&vox_mutex);
  if (!arm_fence) {
    return;
  }
  // This runs in the sole TX-IQ producer thread. Pad the current
  // protocol block, append a complete zero block and remember its
  // sequence fence. All speech data is ahead of that fence.
  uint64_t fence = vox_protocol_fence_begin();
  g_mutex_lock(&vox_mutex);
  if (vox_state == VOX_STATE_FENCE_ARMING) {
    if (fence != 0) {
      vox_fence = fence;
      vox_fence_started_us = g_get_monotonic_time();
      vox_state = VOX_STATE_FENCE;
      t_print("VOX TX drain: WDSP tail %.1f ms, fence=%llu\n",
              0.001 * (double) elapsed,
              (unsigned long long) fence);
    } else {
      // The ring was temporarily full. Continue feeding zero blocks and
      // retry after the producer has recovered.
      vox_state = VOX_STATE_DRAIN;
      vox_quiet_blocks = 0;
    }
  }
  g_mutex_unlock(&vox_mutex);
}

//
// If no VOX operation is pending, this function is a no-op.
//
void vox_cancel(void) {
  g_mutex_lock(&vox_mutex);
  if (vox_timeout) {
    // Remove the old source before releasing the lock. Otherwise a new
    // VOX event could allocate a reused source ID which the delayed
    // g_source_remove() would then remove by mistake.
    g_source_remove(vox_timeout);
    vox_timeout = 0;
  }
  vox_state = VOX_STATE_IDLE;
  vox_deadline_us = 0;
  vox_last_activity_us = 0;
  vox_drain_started_us = 0;
  vox_fence_started_us = 0;
  vox_guard_deadline_us = 0;
  vox_fence = 0;
  vox_quiet_blocks = 0;
  g_mutex_unlock(&vox_mutex);
}
