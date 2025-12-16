/* Copyright (C)
* 2019 - John Melton, G0ORX/N6LYT
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
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <pulse/simple.h>

#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "audio.h"
#include "mode.h"
#include "vfo.h"
#include "message.h"

//
// Used fixed buffer sizes.
// The extremely large standard RX buffer size (2048)
// does no good when combined with pulseaudio's internal
// buffering
//
static const int out_buffer_size = 512;
static const int mic_buffer_size = 512;

int n_input_devices;
AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
int n_output_devices;
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

GMutex audio_mutex;
GMutex mic_ring_mutex;
static GMutex enum_mutex;
static GCond  enum_cond;
static GMutex op_mutex;

// One-time init for mutexes/conds used across multiple entry points.
static gsize mutexes_inited = 0;
static void audio_init_mutexes_once(void) {
  if (g_once_init_enter(&mutexes_inited)) {
    g_mutex_init(&audio_mutex);
    g_mutex_init(&mic_ring_mutex);
    g_mutex_init(&enum_mutex);
    g_mutex_init(&op_mutex);
    g_cond_init(&enum_cond);
    g_once_init_leave(&mutexes_inited, 1);
  }
}

//
// Ring buffer for "local microphone" samples
// NOTE: need large buffer for some "loopback" devices which produce
//       samples in large chunks if fed from digimode programs.
//
#define MICRINGLEN 6000
static float  *mic_ring_buffer = NULL;
static int     mic_ring_read_pt = 0;
static int     mic_ring_write_pt = 0;
static guint64  mic_overrun_drops = 0;   // Anzahl verworfener Samples wegen vollem Ring
static guint64  mic_overrun_events = 0;  // Anzahl Overrun-Situationen (mind. 1 Drop)

// Device enumeration sync (avoid blocking audio_mutex forever)
static int    enum_done = 0;
static int    enum_ok = 0;

static pa_glib_mainloop *main_loop;
static pa_mainloop_api *main_loop_api;
static pa_operation *op;
static pa_context *pa_ctx;
// protect 'op' against concurrent set/unref
static pa_simple* microphone_stream;
static int local_microphone_buffer_offset;
static float *local_microphone_buffer = NULL;
static GThread *mic_read_thread_id = 0;
static gint running;   // atomic: use g_atomic_int_get/set

static void source_list_cb(pa_context *context, const pa_source_info *s, int eol, void *data) {
  audio_init_mutexes_once();

  if (eol > 0) {
    g_mutex_lock(&audio_mutex);

    for (int i = 0; i < n_input_devices; i++) {
      t_print("Input: %d: %s (%s)\n", input_devices[i].index, input_devices[i].name, input_devices[i].description);
    }

    g_mutex_unlock(&audio_mutex);
    g_mutex_lock(&enum_mutex);
    enum_done = 1;
    enum_ok = 1;
    g_cond_signal(&enum_cond);
    g_mutex_unlock(&enum_mutex);
    return;
  }

  // eol == 0: valid source entry
  if (!s) { return; }

  g_mutex_lock(&audio_mutex);

  if (n_input_devices < MAX_AUDIO_DEVICES) {
    input_devices[n_input_devices].name = g_strdup(s->name);
    input_devices[n_input_devices].description = g_strdup(s->description);
    input_devices[n_input_devices].index = s->index;
    n_input_devices++;
  }

  g_mutex_unlock(&audio_mutex);
}

static void sink_list_cb(pa_context *context, const pa_sink_info *s, int eol, void *data) {
  audio_init_mutexes_once();

  if (eol > 0) {
    g_mutex_lock(&audio_mutex);

    for (int i = 0; i < n_output_devices; i++) {
      t_print("Output: %d: %s (%s)\n",
              output_devices[i].index,
              output_devices[i].name,
              output_devices[i].description);
    }

    g_mutex_unlock(&audio_mutex);
    // replace op safely
    pa_operation *newop = pa_context_get_source_info_list(pa_ctx, source_list_cb, NULL);
    g_mutex_lock(&op_mutex);

    if (op != NULL) {
      pa_operation_unref(op);
      op = NULL;
    }

    op = newop;
    g_mutex_unlock(&op_mutex);

    if (op == NULL) {
      g_mutex_lock(&enum_mutex);
      enum_done = 1;
      enum_ok = 0;
      g_cond_signal(&enum_cond);
      g_mutex_unlock(&enum_mutex);
    }

    return;
  }

  // eol == 0: valid sink entry
  if (!s) { return; }

  g_mutex_lock(&audio_mutex);

  if (n_output_devices < MAX_AUDIO_DEVICES) {
    output_devices[n_output_devices].name = g_strdup(s->name);
    output_devices[n_output_devices].description = g_strdup(s->description);
    output_devices[n_output_devices].index = s->index;
    n_output_devices++;
  }

  g_mutex_unlock(&audio_mutex);
}

static void state_cb(pa_context *c, void *userdata) {
  pa_context_state_t state;
  state = pa_context_get_state(c);
  t_print("%s: %d\n", __FUNCTION__, state);

  switch  (state) {
  // There are just here for reference
  case PA_CONTEXT_UNCONNECTED:
    t_print("audio: state_cb: PA_CONTEXT_UNCONNECTED\n");
    break;

  case PA_CONTEXT_CONNECTING:
    t_print("audio: state_cb: PA_CONTEXT_CONNECTING\n");
    break;

  case PA_CONTEXT_AUTHORIZING:
    t_print("audio: state_cb: PA_CONTEXT_AUTHORIZING\n");
    break;

  case PA_CONTEXT_SETTING_NAME:
    t_print("audio: state_cb: PA_CONTEXT_SETTING_NAME\n");
    break;

  case PA_CONTEXT_FAILED:
    t_print("audio: state_cb: PA_CONTEXT_FAILED\n");
    g_mutex_lock(&enum_mutex);
    enum_done = 1;
    enum_ok = 0;
    g_cond_signal(&enum_cond);
    g_mutex_unlock(&enum_mutex);
    break;

  case PA_CONTEXT_TERMINATED:
    t_print("audio: state_cb: PA_CONTEXT_TERMINATED\n");
    g_mutex_lock(&enum_mutex);
    enum_done = 1;
    enum_ok = 0;
    g_cond_signal(&enum_cond);
    g_mutex_unlock(&enum_mutex);
    break;

  case PA_CONTEXT_READY:
    t_print("audio: state_cb: PA_CONTEXT_READY\n");
    // get a list of the output devices
    g_mutex_lock(&audio_mutex);
    n_input_devices = 0;
    n_output_devices = 0;
    g_mutex_unlock(&audio_mutex);
    // replace op safely
    pa_operation *newop = pa_context_get_sink_info_list(pa_ctx, sink_list_cb, NULL);
    g_mutex_lock(&op_mutex);

    if (op != NULL) {
      pa_operation_unref(op);
      op = NULL;
    }

    op = newop;
    g_mutex_unlock(&op_mutex);

    if (op == NULL) {
      g_mutex_lock(&enum_mutex);
      enum_done = 1;
      enum_ok = 0;
      g_cond_signal(&enum_cond);
      g_mutex_unlock(&enum_mutex);
    }

    break;

  default:
    t_print("audio: state_cb: unknown state %d\n", state);
    break;
  }
}

void audio_release_cards(void) {
  audio_init_mutexes_once();
  // If an enumeration wait is in progress, unblock it.
  g_mutex_lock(&enum_mutex);
  enum_done = 1;
  enum_ok   = 0;
  g_cond_signal(&enum_cond);
  g_mutex_unlock(&enum_mutex);
  // Stoppe ggf. laufende Enumeration-Operation
  g_mutex_lock(&op_mutex);

  if (op != NULL) {
    pa_operation_cancel(op);
    pa_operation_unref(op);
    op = NULL;
  }

  g_mutex_unlock(&op_mutex);

  // Context sauber trennen und freigeben
  if (pa_ctx != NULL) {
    pa_context_set_state_callback(pa_ctx, NULL, NULL);
    pa_context_disconnect(pa_ctx);
    pa_context_unref(pa_ctx);
    pa_ctx = NULL;
  }

  // GLib-Pulse-Mainloop freigeben
  if (main_loop != NULL) {
    pa_glib_mainloop_free(main_loop);
    main_loop = NULL;
    main_loop_api = NULL;
  }

  // Enum-Synchronisationszustand zurücksetzen
  g_mutex_lock(&enum_mutex);
  enum_done = 0;
  enum_ok   = 0;
  g_mutex_unlock(&enum_mutex);
  // Device-Listen zurücksetzen (Ownership bleibt wie bisher)
  g_mutex_lock(&audio_mutex);
  n_input_devices  = 0;
  n_output_devices = 0;
  g_mutex_unlock(&audio_mutex);
}

void audio_get_cards() {
  audio_init_mutexes_once();
  g_mutex_lock(&enum_mutex);
  enum_done = 0;
  enum_ok = 0;
  g_mutex_unlock(&enum_mutex);
  main_loop = pa_glib_mainloop_new(NULL);
  main_loop_api = pa_glib_mainloop_get_api(main_loop);
  pa_ctx = pa_context_new(main_loop_api, "deskHPSDR");
  pa_context_set_state_callback(pa_ctx, state_cb, NULL);
  pa_context_connect(pa_ctx, NULL, 0, NULL);
  // Wait for enumeration to complete, but never block indefinitely.
  // IMPORTANT: pump GLib main context so PulseAudio callbacks can run.
  gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
  g_mutex_lock(&enum_mutex);

  while (!enum_done) {
    g_mutex_unlock(&enum_mutex);

    // Process pending main-context events (PulseAudio GLib mainloop callbacks)
    while (g_main_context_iteration(NULL, FALSE)) { /* drain */ }

    // avoid busy loop
    g_usleep(1000); // 1ms
    g_mutex_lock(&enum_mutex);

    if (g_get_monotonic_time() >= deadline) {
      enum_done = 1;
      enum_ok = 0;
      break;
    }
  }

  int ok = enum_ok;
  g_mutex_unlock(&enum_mutex);

  if (!ok) {
    t_print("%s: pulseaudio device enumeration timeout/fail\n", __FUNCTION__);
    // Cleanup on failure/timeout to avoid leaking contexts/mainloops or leaving
    // callbacks running in the background.
    g_mutex_lock(&op_mutex);

    if (op != NULL) {
      pa_operation_cancel(op);
      pa_operation_unref(op);
      op = NULL;
    }

    g_mutex_unlock(&op_mutex);

    if (pa_ctx != NULL) {
      pa_context_set_state_callback(pa_ctx, NULL, NULL);
      pa_context_disconnect(pa_ctx);
      pa_context_unref(pa_ctx);
      pa_ctx = NULL;
    }

    if (main_loop != NULL) {
      pa_glib_mainloop_free(main_loop);
      main_loop = NULL;
      main_loop_api = NULL;
    }
  }
}

int audio_open_output(RECEIVER *rx) {
  int result = 0;
  pa_sample_spec sample_spec;
  int err;

  if (rx == NULL || rx->audio_name[0] == '\0') {
    t_print("%s: no output device selected\n", __FUNCTION__);
    return -1;
  }

  g_mutex_lock(&rx->local_audio_mutex);
  sample_spec.rate = 48000;
  sample_spec.channels = 2;
  sample_spec.format = PA_SAMPLE_FLOAT32NE;
  pa_buffer_attr attr;
  attr.maxlength = (uint32_t) -1;
  // 1 Block = out_buffer_size Frames * 2 Kanäle * float
  const uint32_t one_block_bytes = (uint32_t)(out_buffer_size * 2 * sizeof(float));
  attr.tlength  = one_block_bytes * 4;   // Zielpuffer: 4 Blöcke
  attr.minreq   = one_block_bytes;       // Nachschub in 1-Block-Schritten
  attr.prebuf   = one_block_bytes;       // Prebuffer: 1 Block
  attr.fragsize = (uint32_t) -1;         // bei Playback irrelevant
  char stream_id[16];
  snprintf(stream_id, 16, "RX-%d", rx->id);
  rx->playstream = pa_simple_new(NULL, // Use the default server.
                                 "deskHPSDR",          // Our application's name.
                                 PA_STREAM_PLAYBACK,
                                 rx->audio_name,
                                 stream_id,          // Description of our stream.
                                 &sample_spec,       // Our sample format.
                                 NULL,               // Use default channel map
                                 &attr,              // Use attributes
                                 &err                // error code if returns NULL
                                );

  if (rx->playstream != NULL) {
    rx->local_audio_buffer_offset = 0;
    rx->local_audio_buffer = g_new0(float, 2 * out_buffer_size);
    t_print("%s: allocated local_audio_buffer %p size %ld bytes\n", __FUNCTION__, rx->local_audio_buffer,
            2 * out_buffer_size * sizeof(float));
  } else {
    result = -1;
    t_print("%s: pa-simple_new failed: err=%d\n", __FUNCTION__, err);
  }

  g_mutex_unlock(&rx->local_audio_mutex);
  return result;
}

static void *mic_read_thread(gpointer arg) {
  int err;
  t_print("%s: running=%d\n", __FUNCTION__, g_atomic_int_get(&running));

  while (g_atomic_int_get(&running)) {
    //
    // It is guaranteed that local_microphone_buffer, mic_ring_buffer, and microphone_stream
    // will not be destroyed until this thread has terminated (and waited for via thread joining)
    //
    int rc = pa_simple_read(microphone_stream,
                            local_microphone_buffer,
                            mic_buffer_size * sizeof(float),
                            &err);

    if (rc < 0) {
      g_atomic_int_set(&running, 0);
      t_print("%s: simple_read returned %d error=%d (%s)\n", __FUNCTION__, rc, err, pa_strerror(err));
    } else {
      // If shutdown was requested while we were blocked in pa_simple_read(),
      // do not attempt to take locks or write into buffers.
      if (!g_atomic_int_get(&running)) {
        break;
      }

      // Ringbuffer separat schützen (entkoppelt vom globalen audio_mutex)
      g_mutex_lock(&mic_ring_mutex);

      if (mic_ring_buffer == NULL) { g_mutex_unlock(&mic_ring_mutex); continue; }

      guint64 local_drops = 0;
      int had_overrun = 0;

      for (int i = 0; i < mic_buffer_size; i++) {
        int newpt = mic_ring_write_pt + 1;

        if (newpt == MICRINGLEN) { newpt = 0; }

        if (newpt != mic_ring_read_pt) {
          mic_ring_buffer[mic_ring_write_pt] = local_microphone_buffer[i];
          mic_ring_write_pt = newpt;
        } else {
          // Ring voll -> Sample wird verworfen
          local_drops++;
          had_overrun = 1;
        }
      }

      if (had_overrun) {
        mic_overrun_events++;
        mic_overrun_drops += local_drops;
      }

      g_mutex_unlock(&mic_ring_mutex);

      // Overrun-Telemetrie: nur gelegentlich loggen, damit kein Spam entsteht.
      // Trigger: jede 100. Overrun-Situation
      if (had_overrun && (mic_overrun_events % 100 == 0)) {
        t_print("%s: MIC RING OVERRUN: events=%" G_GUINT64_FORMAT
                " dropped=%" G_GUINT64_FORMAT "\n",
                __FUNCTION__, mic_overrun_events, mic_overrun_drops);
      }
    }
  }

  t_print("%s: exit\n", __FUNCTION__);
  return NULL;
}

int audio_open_input() {
  pa_sample_spec sample_spec;

  if (!can_transmit) {
    return -1;
  }

  if (transmitter == NULL || transmitter->microphone_name[0] == '\0') {
    t_print("%s: no input device selected\n", __FUNCTION__);
    return -1;
  }

  pa_buffer_attr attr;
  attr.maxlength = (uint32_t) -1;
  attr.tlength = (uint32_t) -1;
  attr.prebuf = (uint32_t) -1;
  attr.minreq = (uint32_t) -1;
  attr.fragsize = 512;
  sample_spec.rate = 48000;
  sample_spec.channels = 1;
  sample_spec.format = PA_SAMPLE_FLOAT32NE;
  int err = 0;
  pa_simple *new_stream = pa_simple_new(NULL,      // Use the default server.
                                        "deskHPSDR",                   // Our application's name.
                                        PA_STREAM_RECORD,
                                        transmitter->microphone_name,
                                        "TX",                        // Description of our stream.
                                        &sample_spec,                // Our sample format.
                                        NULL,                        // Use default channel map
                                        &attr,                       // Use default buffering attributes but set fragsize
                                        &err                         // error code
                                       );

  if (new_stream == NULL) {
    t_print("%s: pa_simple_new (RECORD) failed err=%d (%s)\n", __FUNCTION__, err, pa_strerror(err));
    return -1;
  }

  float *new_local_buf = g_new0(float, mic_buffer_size);
  t_print("%s: allocating ring buffer\n", __FUNCTION__);
  float *new_ring_buf = (float *) g_new(float, MICRINGLEN);

  if (new_local_buf == NULL || new_ring_buf == NULL) {
    if (new_local_buf) { g_free(new_local_buf); }

    if (new_ring_buf) { g_free(new_ring_buf); }

    pa_simple_free(new_stream);
    return -1;
  }

  // Kurzer Commit-Block: nur Globals setzen
  g_mutex_lock(&audio_mutex);
  microphone_stream = new_stream;
  local_microphone_buffer = new_local_buf;
  local_microphone_buffer_offset = 0;
  g_atomic_int_set(&running, 1);
  g_mutex_unlock(&audio_mutex);
  g_mutex_lock(&mic_ring_mutex);
  mic_ring_buffer = new_ring_buf;
  mic_ring_read_pt = mic_ring_write_pt = 0;
  mic_overrun_drops = 0;
  mic_overrun_events = 0;
  g_mutex_unlock(&mic_ring_mutex);
  t_print("%s: PULSEAUDIO mic_read_thread\n", __FUNCTION__);
  mic_read_thread_id = g_thread_new("mic_thread", mic_read_thread, NULL);

  if (!mic_read_thread_id) {
    t_print("%s: g_thread_new failed on mic_read_thread\n", __FUNCTION__);
    g_atomic_int_set(&running, 0);
    // Rollback sauber freigeben
    g_mutex_lock(&audio_mutex);

    if (microphone_stream) { pa_simple_free(microphone_stream); microphone_stream = NULL; }

    if (local_microphone_buffer) { g_free(local_microphone_buffer); local_microphone_buffer = NULL; }

    g_mutex_unlock(&audio_mutex);
    // Ringbuffer gehört zur mic_ring_mutex-Lock-Domain
    g_mutex_lock(&mic_ring_mutex);

    if (mic_ring_buffer) {
      g_free(mic_ring_buffer);
      mic_ring_buffer = NULL;
    }

    g_mutex_unlock(&mic_ring_mutex);
    return -1;
  }

  return 0;
}

void audio_close_output(RECEIVER *rx) {
  g_mutex_lock(&rx->local_audio_mutex);

  if (rx->playstream != NULL) {
    pa_simple_free(rx->playstream);
    rx->playstream = NULL;
  }

  if (rx->local_audio_buffer != NULL) {
    g_free(rx->local_audio_buffer);
    rx->local_audio_buffer = NULL;
  }

  g_mutex_unlock(&rx->local_audio_mutex);
}

void audio_close_input() {
  g_atomic_int_set(&running, 0);

  // Join WITHOUT holding audio_mutex to avoid deadlock:
  // mic_read_thread uses mic_ring_mutex while writing into the ringbuffer.
  if (mic_read_thread_id != NULL) {
    t_print("%s: wait for mic thread to complete\n", __FUNCTION__);
    g_thread_join(mic_read_thread_id);
    mic_read_thread_id = NULL;
  }

  g_mutex_lock(&audio_mutex);

  if (microphone_stream != NULL) {
    pa_simple_free(microphone_stream);
    microphone_stream = NULL;
  }

  if (local_microphone_buffer != NULL) {
    g_free(local_microphone_buffer);
    local_microphone_buffer = NULL;
  }

  g_mutex_unlock(&audio_mutex);
  g_mutex_lock(&mic_ring_mutex);

  if (mic_ring_buffer != NULL) {
    g_free(mic_ring_buffer);
    mic_ring_buffer = NULL;
  }

  g_mutex_unlock(&mic_ring_mutex);
  return;
}

//
// Utility function for retrieving mic samples
// from ring buffer
//
float audio_get_next_mic_sample() {
  float sample;
  g_mutex_lock(&mic_ring_mutex);

  if ((mic_ring_buffer == NULL) || (mic_ring_read_pt == mic_ring_write_pt)) {
    // no buffer, or nothing in buffer: insert silence
    //t_print("%s: no samples\n",__FUNCTION__);
    sample = 0.0;
  } else {
    int newpt = mic_ring_read_pt + 1;

    if (newpt == MICRINGLEN) { newpt = 0; }

    sample = mic_ring_buffer[mic_ring_read_pt];
    // update of read pointer (mutex-protected)
    mic_ring_read_pt = newpt;
  }

  g_mutex_unlock(&mic_ring_mutex);
  return sample;
}

int cw_audio_write(RECEIVER *rx, float sample) {
  int result = 0;
  int err;
  g_mutex_lock(&rx->local_audio_mutex);

  if (rx->playstream != NULL && rx->local_audio_buffer != NULL) {
    //
    // Since this is mutex-protected, we know that both rx->playstream
    // and rx->local_audio_buffer will not be destroyes until we
    // are finished here.
    //
    rx->local_audio_buffer[rx->local_audio_buffer_offset * 2] = sample;
    rx->local_audio_buffer[(rx->local_audio_buffer_offset * 2) + 1] = sample;
    rx->local_audio_buffer_offset++;

    if (rx->local_audio_buffer_offset >= out_buffer_size) {
      int rc = pa_simple_write(rx->playstream,
                               rx->local_audio_buffer,
                               out_buffer_size * sizeof(float) * 2,
                               &err);

      if (rc != 0) {
        t_print("%s: simple_write failed err=%d\n", __FUNCTION__, err);
      }

      rx->local_audio_buffer_offset = 0;
    }
  }

  g_mutex_unlock(&rx->local_audio_mutex);
  return result;
}

int audio_write(RECEIVER *rx, float left_sample, float right_sample) {
  int result = 0;
  int err;
  int txmode = vfo_get_tx_mode();

  if (rx == active_receiver && radio_is_transmitting() && (txmode == modeCWU || txmode == modeCWL)) {
    return 0;
  }

  g_mutex_lock(&rx->local_audio_mutex);

  if (rx->playstream != NULL && rx->local_audio_buffer != NULL) {
    //
    // Since this is mutex-protected, we know that both rx->playstream
    // and rx->local_audio_buffer will not be destroyes until we
    // are finished here.
    rx->local_audio_buffer[rx->local_audio_buffer_offset * 2] = left_sample;
    rx->local_audio_buffer[(rx->local_audio_buffer_offset * 2) + 1] = right_sample;
    rx->local_audio_buffer_offset++;

    if (rx->local_audio_buffer_offset >= out_buffer_size) {
      int rc = pa_simple_write(rx->playstream,
                               rx->local_audio_buffer,
                               out_buffer_size * sizeof(float) * 2,
                               &err);

      if (rc != 0) {
        t_print("%s: simple_write failed err=%d\n", __FUNCTION__, err);
      }

      rx->local_audio_buffer_offset = 0;
    }
  }

  g_mutex_unlock(&rx->local_audio_mutex);
  return result;
}
