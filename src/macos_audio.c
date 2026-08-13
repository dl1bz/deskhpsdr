/*  Copyright (C)
*   2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
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

//
// Native macOS audio engine using CoreAudio/AUHAL.
//

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>

#include "coreaudio.h"

#include "radio.h"
#include "receiver.h"
#include "mode.h"
#include "audio.h"
#include "message.h"
#include "sliders.h"
#include "vfo.h"
#include "tci_audio.h"

static void *coreaudio_input_handle = NULL;

int n_input_devices;
AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
int n_output_devices;
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

GMutex audio_mutex;
static volatile gint audio_xrun_count = 0;
static volatile gint output_ring_primed[8] = { 0 };
static volatile gint output_ring_starved[8] = { 0 };
static atomic_uint rx_ring_diag_underruns[8];

guint64 audio_get_xrun_count(void) {
  return (guint64) g_atomic_int_get(&audio_xrun_count);
}


//
// We now use callback functions to provide the "headphone" audio data,
// and therefore can control the latency.
// RX audio samples are put into a ring buffer and "fetched" therefreom
// by the CoreAudio "headphone" callback.
//
// RX audio and CW sidetone use separate ring buffers. On an RX/TX transition
// the RX ring is no longer discarded: its WDSP-slewed tail is allowed to drain
// naturally while the sidetone starts from its own low-latency ring.
// The sidetone filling is kept close to an explicit low-latency target to
// reduce underrun risk and avoid larger latency swings.
// Of course, a small CoreAudio audio buffer size (128 sample) helps
// keeping the latency small. The CW buffer is kept around CW_LAT_TARGET
// with a narrow correction window to reduce occasional underruns/clicks.
//
// Experiments indicate that we can indeed keep the ring buffer about half filling
// during RX and quite empty during CW-TX.
//
//

#define MY_RING_BUFFER_SIZE 48000

#define RX_LAT_LOW            768
#define RX_LAT_TARGET        1536
#define RX_LAT_HIGH_BASE     2304
#define RX_LAT_GUARD          128

static inline void rx_audio_latency_limits(int *low, int *target, int *high_base) {
  *low = RX_LAT_LOW;
  *target = RX_LAT_TARGET;
  *high_base = RX_LAT_HIGH_BASE;
  if (protocol != NEW_PROTOCOL ||
      !g_atomic_int_get(&rx_audio_network_reserve_enabled)) {
    return;
  }
  int reserve_ms = g_atomic_int_get(&rx_audio_network_reserve_ms);
  if (reserve_ms < 5) {
    reserve_ms = 5;
  } else if (reserve_ms > 500) {
    reserve_ms = 500;
  }
  /*
   * Local RX audio is delivered to CoreAudio at 48 kHz.  Above the
   * normal low-latency operating point, use 2/3 of the requested
   * reserve as low-water and 5/3 as base high-water.
   * Example: 150 ms -> LOW 100 ms, TARGET 150 ms, HIGH 250 ms.
   */
  int reserve_target = reserve_ms * 48;
  if (reserve_target <= RX_LAT_TARGET) {
    return;
  }
  *target = reserve_target;
  *low = (reserve_target * 2) / 3;
  *high_base = (reserve_target * 5) / 3;
  if (*high_base >= MY_RING_BUFFER_SIZE) {
    *high_base = MY_RING_BUFFER_SIZE - RX_LAT_GUARD - 1;
  }
}



#define CW_LAT_LOW            128
#define CW_LAT_TARGET         256
#define CW_LAT_HIGH           384

//
// Ring buffer for "local microphone" samples stored locally here.
// NOTE: lead large buffer for some "loopback" devices which produce
//       samples in large chunks if fed from digimode programs.
//
static float *mic_ring_buffer = NULL;
static atomic_int mic_ring_outpt;
static atomic_int mic_ring_inpt;
static atomic_int mic_ring_reset_pending;
static atomic_int mic_ring_reset_frames;
static atomic_int mic_ring_silence_frames;
static atomic_uint mic_ring_diag_generation;
static atomic_uint mic_ring_underruns;
static atomic_uint mic_ring_overruns;
static atomic_uint cw_ring_diag_underruns;


int audio_get_rx_buffer_diag(RECEIVER *rx, AUDIO_BUFFER_DIAG *diag) {
  if (rx == NULL || diag == NULL) {
    return 0;
  }
  memset(diag, 0, sizeof(*diag));
  diag->capacity = MY_RING_BUFFER_SIZE;
  rx_audio_latency_limits(&diag->low, &diag->target, &diag->high);
  if (rx->local_audio_buffer == NULL || rx->coreaudio_output_handle == NULL) {
    return 1;
  }

  int inpt = atomic_load_explicit(&rx->local_audio_buffer_inpt, memory_order_acquire);
  int outpt = atomic_load_explicit(&rx->local_audio_buffer_outpt, memory_order_acquire);
  int queued = inpt - outpt;
  if (queued < 0) {
    queued += MY_RING_BUFFER_SIZE;
  }
  diag->queued = queued;
  diag->available = 1;
  return 1;
}

int audio_get_mic_buffer_diag(AUDIO_BUFFER_DIAG *diag) {
  if (diag == NULL) {
    return 0;
  }
  memset(diag, 0, sizeof(*diag));
  diag->capacity = MY_RING_BUFFER_SIZE;
  if (mic_ring_buffer == NULL) {
    return 1;
  }

  int inpt = atomic_load_explicit(&mic_ring_inpt, memory_order_acquire);
  int outpt = atomic_load_explicit(&mic_ring_outpt, memory_order_acquire);
  int queued = inpt - outpt;
  if (queued < 0) {
    queued += MY_RING_BUFFER_SIZE;
  }
  diag->queued = queued;
  diag->available = 1;
  return 1;
}

int audio_get_cw_buffer_diag(RECEIVER *rx, AUDIO_BUFFER_DIAG *diag) {
  if (rx == NULL || diag == NULL) {
    return 0;
  }
  memset(diag, 0, sizeof(*diag));
  diag->capacity = MY_RING_BUFFER_SIZE;
  diag->target = CW_LAT_TARGET;
  if (rx->sidetone_buffer == NULL || rx->coreaudio_output_handle == NULL) {
    return 1;
  }

  int inpt = atomic_load_explicit(&rx->sidetone_buffer_inpt, memory_order_acquire);
  int outpt = atomic_load_explicit(&rx->sidetone_buffer_outpt, memory_order_acquire);
  int queued = inpt - outpt;
  if (queued < 0) {
    queued += MY_RING_BUFFER_SIZE;
  }
  diag->queued = queued;
  diag->available = 1;
  return 1;
}


//
// Request a ring reset without modifying the consumer-owned output pointer.
// The CoreAudio callback is the producer; the protocol mic path is the
// consumer. The consumer performs the actual flush on its next read and then
// returns the requested amount of silence before consuming new mic samples.
//
static void local_mic_ring_request_reset(int silence_frames) {
  if (silence_frames < 0) {
    silence_frames = 0;
  }
  if (silence_frames >= MY_RING_BUFFER_SIZE) {
    silence_frames = MY_RING_BUFFER_SIZE - 1;
  }
  atomic_store_explicit(&mic_ring_reset_frames, silence_frames, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_reset_pending, 1, memory_order_release);
}

static inline void local_mic_ring_push(float sample) {
  int inpt = atomic_load_explicit(&mic_ring_inpt, memory_order_relaxed);
  int outpt = atomic_load_explicit(&mic_ring_outpt, memory_order_acquire);
  int newpt = inpt + 1;
  if (newpt == MY_RING_BUFFER_SIZE) {
    newpt = 0;
  }
  if (newpt == outpt) {
    atomic_fetch_add_explicit(&mic_ring_overruns, 1U, memory_order_relaxed);
    return;
  }
  mic_ring_buffer[inpt] = sample;
  atomic_store_explicit(&mic_ring_inpt, newpt, memory_order_release);
}

static inline float local_mic_ring_pop(void) {
  int outpt;
  int inpt;
  int newpt;
  float sample;
  if (mic_ring_buffer == NULL) {
    return 0.0f;
  }
  //
  // Producer-to-consumer reset handshake. Only the consumer advances outpt.
  // Flush everything queued before the reset request and provide the desired
  // silence locally, while the producer can already refill the ring.
  //
  if (atomic_exchange_explicit(&mic_ring_reset_pending, 0, memory_order_acq_rel)) {
    int reset_frames = atomic_load_explicit(&mic_ring_reset_frames, memory_order_relaxed);
    inpt = atomic_load_explicit(&mic_ring_inpt, memory_order_acquire);
    atomic_store_explicit(&mic_ring_outpt, inpt, memory_order_release);
    atomic_store_explicit(&mic_ring_silence_frames, reset_frames, memory_order_relaxed);
    atomic_fetch_add_explicit(&mic_ring_diag_generation, 1U, memory_order_release);
  }
  int silence_frames = atomic_load_explicit(&mic_ring_silence_frames, memory_order_relaxed);
  if (silence_frames > 0) {
    atomic_store_explicit(&mic_ring_silence_frames, silence_frames - 1, memory_order_relaxed);
    return 0.0f;
  }
  outpt = atomic_load_explicit(&mic_ring_outpt, memory_order_relaxed);
  inpt = atomic_load_explicit(&mic_ring_inpt, memory_order_acquire);
  if (outpt == inpt) {
    atomic_fetch_add_explicit(&mic_ring_underruns, 1U, memory_order_relaxed);
    return 0.0f;
  }
  sample = mic_ring_buffer[outpt];
  newpt = outpt + 1;
  if (newpt == MY_RING_BUFFER_SIZE) {
    newpt = 0;
  }
  atomic_store_explicit(&mic_ring_outpt, newpt, memory_order_release);
  return sample;
}

static void *coreaudio_tci_monitor_handle = NULL;


static GMutex tci_monitor_mutex;

static gboolean coreaudio_device_watch_cb(gpointer data) {
  (void) data;
  /*
   * DeviceIsAlive is updated by CoreAudio property listeners. All state
   * changes and close/dispose operations are deliberately done here on the
   * GLib main loop, never from a CoreAudio callback.
   */
  for (int i = 0; i < receivers; i++) {
    RECEIVER *rx = receiver[i];
    if (rx == NULL) {
      continue;
    }
    int device_lost;
    g_mutex_lock(&rx->local_audio_mutex);
    device_lost = rx->coreaudio_output_handle != NULL &&
                  !coreaudio_output_is_alive(rx->coreaudio_output_handle);
    g_mutex_unlock(&rx->local_audio_mutex);
    if (device_lost) {
      t_print("%s: CoreAudio output device lost rx=%d name=%s -> Local Audio OFF\n",
              __func__, rx->id, rx->audio_name);
      rx->local_audio = 0;
      audio_close_output(rx);
    }
  }
  int input_lost;
  g_mutex_lock(&audio_mutex);
  input_lost = coreaudio_input_handle != NULL &&
               !coreaudio_input_is_alive(coreaudio_input_handle);
  g_mutex_unlock(&audio_mutex);
  if (input_lost) {
    t_print("%s: CoreAudio input device lost name=%s -> Local Microphone OFF\n",
            __func__,
            transmitter != NULL ? transmitter->microphone_name : "(unknown)");
    if (transmitter != NULL) {
      transmitter->local_microphone = 0;
    }
    audio_close_input();
    update_slider_local_mic_button();
  }
  int monitor_lost;
  g_mutex_lock(&tci_monitor_mutex);
  monitor_lost = coreaudio_tci_monitor_handle != NULL &&
                 !coreaudio_tci_monitor_is_alive(coreaudio_tci_monitor_handle);
  g_mutex_unlock(&tci_monitor_mutex);
  if (monitor_lost) {
    t_print("%s: CoreAudio TCI monitor device lost -> TCI Audio Monitor OFF\n",
            __func__);
    tci_audio_monitor = 0;
    audio_close_tci_monitor();
  }
  return G_SOURCE_CONTINUE;
}

static void coreaudio_start_device_watch(void) {
  static gsize started = 0;
  if (g_once_init_enter(&started)) {
    g_timeout_add(250, coreaudio_device_watch_cb, NULL);
    g_once_init_leave(&started, 1);
  }
}


void audio_release_cards(void) {
  audio_close_tci_monitor();
  g_mutex_lock(&audio_mutex);
  for (int i = 0; i < n_input_devices; i++) {
    g_free(input_devices[i].name);
    g_free(input_devices[i].description);
  }
  for (int i = 0; i < n_output_devices; i++) {
    g_free(output_devices[i].name);
    g_free(output_devices[i].description);
  }
  n_input_devices  = 0;
  n_output_devices = 0;
  memset(input_devices, 0, sizeof(input_devices));
  memset(output_devices, 0, sizeof(output_devices));
  g_mutex_unlock(&audio_mutex);
}

//
// AUDIO_GET_CARDS
//
// Enumerate suitable native CoreAudio input and output devices.
//
void audio_get_cards(void) {
  static gsize mutex_inited = 0;
  if (g_once_init_enter(&mutex_inited)) {
    g_mutex_init(&audio_mutex);
    g_mutex_init(&tci_monitor_mutex);
    g_once_init_leave(&mutex_inited, 1);
  }
  coreaudio_start_device_watch();
  t_print("%s: native CoreAudio call audio_get_cards\n", __func__);
  if (coreaudio_get_cards() != 0) {
    t_print("%s: native CoreAudio device enumeration failed\n", __func__);
  }
}


//
// AUDIO_OPEN_INPUT
//
// Open native CoreAudio input connected to the TX microphone.
//


int audio_open_input(void) {
  t_print("%s: native CoreAudio call audio_open_input\n", __func__);
  if (!can_transmit) {
    return -1;
  }
  if (transmitter == NULL || transmitter->microphone_name[0] == '\0') {
    return -1;
  }
  g_mutex_lock(&audio_mutex);
  if (coreaudio_input_handle != NULL || mic_ring_buffer != NULL) {
    g_mutex_unlock(&audio_mutex);
    return 0;
  }
  mic_ring_buffer = (float *) g_new(float, MY_RING_BUFFER_SIZE);
  if (mic_ring_buffer == NULL) {
    g_mutex_unlock(&audio_mutex);
    t_print("%s: alloc buffer failed.\n", __func__);
    return -1;
  }
  atomic_store_explicit(&mic_ring_outpt, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_inpt, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_reset_pending, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_reset_frames, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_silence_frames, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_underruns, 0U, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_overruns, 0U, memory_order_relaxed);
  g_mutex_unlock(&audio_mutex);
  void *handle = coreaudio_input_open(transmitter->microphone_name);
  if (handle == NULL) {
    g_mutex_lock(&audio_mutex);
    g_free(mic_ring_buffer);
    mic_ring_buffer = NULL;
    atomic_store_explicit(&mic_ring_outpt, 0, memory_order_relaxed);
    atomic_store_explicit(&mic_ring_inpt, 0, memory_order_relaxed);
    atomic_store_explicit(&mic_ring_reset_frames, 0, memory_order_relaxed);
    atomic_store_explicit(&mic_ring_silence_frames, 0, memory_order_relaxed);
    g_mutex_unlock(&audio_mutex);
    return -1;
  }
  g_mutex_lock(&audio_mutex);
  coreaudio_input_handle = handle;
  g_mutex_unlock(&audio_mutex);
  t_print("%s: native CoreAudio input name=%s\n", __func__, transmitter->microphone_name);
  return 0;
}


//
//
int audio_open_tci_monitor(const char *audio_name) {
  if (audio_name == NULL || audio_name[0] == '\0') {
    return -1;
  }
  g_mutex_lock(&tci_monitor_mutex);
  if (coreaudio_tci_monitor_handle != NULL) {
    g_mutex_unlock(&tci_monitor_mutex);
    return 0;
  }
  g_mutex_unlock(&tci_monitor_mutex);
  //
  // Enable/reset the producer before CoreAudio starts consuming.
  //
  tci_audio_monitor_set_active(1);
  int channels = 0;
  void *handle = coreaudio_tci_monitor_open(audio_name, &channels);
  if (handle == NULL) {
    tci_audio_monitor_set_active(0);
    return -1;
  }
  g_mutex_lock(&tci_monitor_mutex);
  coreaudio_tci_monitor_handle = handle;
  g_mutex_unlock(&tci_monitor_mutex);
  t_print("%s: opened native CoreAudio TCI monitor name=%s channels=%d\n",
          __func__, audio_name, channels);
  return 0;
}


void audio_close_tci_monitor(void) {
  void *handle = NULL;
  g_mutex_lock(&tci_monitor_mutex);
  handle = coreaudio_tci_monitor_handle;
  coreaudio_tci_monitor_handle = NULL;
  g_mutex_unlock(&tci_monitor_mutex);
  //
  // Stop the RT consumer first, then disable/reset the producer ring.
  //
  coreaudio_tci_monitor_close(handle);
  tci_audio_monitor_set_active(0);
}


//
//
void audio_render_local_output(RECEIVER *rx, float *out, unsigned int frames, int channels) {
  gboolean ring_underrun = FALSE;
  gboolean ring_had_audio = FALSE;
  if (rx == NULL || out == NULL || (channels != 1 && channels != 2)) {
    return;
  }
  gboolean valid_rx_id = rx->id >= 0 && rx->id < (int)(sizeof(output_ring_primed) / sizeof(output_ring_primed[0]));
  gboolean ring_was_primed = valid_rx_id && g_atomic_int_get(&output_ring_primed[rx->id]);
  /*
   * This function deliberately takes no mutex. Ring ownership is SPSC:
   * receiver/CW code advances producer indices, the audio callback advances
   * consumer indices. Buffer lifetime is protected by stopping the backend
   * callback before audio_close_output() frees the rings.
   */
  float *rx_buffer = rx->local_audio_buffer;
  float *st_buffer = rx->sidetone_buffer;
  if (rx_buffer != NULL && st_buffer != NULL) {
    int rx_out = atomic_load_explicit(&rx->local_audio_buffer_outpt, memory_order_relaxed);
    int st_out = atomic_load_explicit(&rx->sidetone_buffer_outpt, memory_order_relaxed);
    for (unsigned int i = 0; i < frames; i++) {
      float left = 0.0f;
      float right = 0.0f;
      float sidetone = 0.0f;
      int rx_in = atomic_load_explicit(&rx->local_audio_buffer_inpt, memory_order_acquire);
      int st_in = atomic_load_explicit(&rx->sidetone_buffer_inpt, memory_order_acquire);
      if (rx_in != rx_out) {
        ring_had_audio = TRUE;
        left = rx_buffer[2 * rx_out];
        right = rx_buffer[2 * rx_out + 1];
        rx_out++;
        if (rx_out >= MY_RING_BUFFER_SIZE) {
          rx_out = 0;
        }
        atomic_store_explicit(&rx->local_audio_buffer_outpt, rx_out, memory_order_release);
      } else if (st_in == st_out) {
        ring_underrun = TRUE;
        if (rx->local_audio_cw_active) {
          atomic_fetch_add_explicit(&cw_ring_diag_underruns, 1U, memory_order_relaxed);
        }
      }
      if (st_in != st_out) {
        ring_had_audio = TRUE;
        sidetone = st_buffer[st_out];
        st_out++;
        if (st_out >= MY_RING_BUFFER_SIZE) {
          st_out = 0;
        }
        atomic_store_explicit(&rx->sidetone_buffer_outpt, st_out, memory_order_release);
      }
      left += sidetone;
      right += sidetone;
      if (left > 1.0f) { left = 1.0f; }
      if (left < -1.0f) { left = -1.0f; }
      if (right > 1.0f) { right = 1.0f; }
      if (right < -1.0f) { right = -1.0f; }
      if (channels == 2) {
        *out++ = left;
        *out++ = right;
      } else {
        float mono;
        switch (rx->audio_channel) {
        case LEFT:
          mono = left;
          break;
        case RIGHT:
          mono = right;
          break;
        case STEREO:
        default:
          mono = 0.5f * (left + right);
          break;
        }
        *out++ = mono;
      }
    }
  } else {
    memset(out, 0, (size_t) frames * (size_t) channels * sizeof(float));
  }
  if (valid_rx_id) {
    if (ring_underrun && ring_was_primed && rx->local_audio
        && !g_atomic_int_get(&output_ring_starved[rx->id])) {
      g_atomic_int_inc(&audio_xrun_count);
      atomic_fetch_add_explicit(&rx_ring_diag_underruns[rx->id], 1U, memory_order_relaxed);
    }
    if (ring_had_audio) {
      g_atomic_int_set(&output_ring_primed[rx->id], 1);
    }
    g_atomic_int_set(&output_ring_starved[rx->id], ring_underrun);
  } else if (ring_underrun && rx->local_audio) {
    g_atomic_int_inc(&audio_xrun_count);
  }
}


//
// Feed native CoreAudio microphone samples into the shared mic ring.
//
void audio_process_local_mic_input(const float *samples, unsigned int frames) {
  static int last_was_tx = 0;
  if (samples == NULL || mic_ring_buffer == NULL) {
    return;
  }
  //
  // Normally there is a slight mis-match between the 48kHz sample
  // rate of the microphone device and the 48kHz rate of the HPSDR
  // device. Keep the existing TX/RX transition reset behaviour.
  //
  if (!radio_is_transmitting()) {
    if (last_was_tx) {
      last_was_tx = 0;
      local_mic_ring_request_reset(960);
    }
  } else {
    if (!last_was_tx) {
      //
      // RX -> TX: discard microphone samples accumulated while receiving.
      // Do not add silence here; TX should start with the freshest available
      // CoreAudio input samples.
      //
      local_mic_ring_request_reset(384);
    }
    last_was_tx = 1;
  }
  for (unsigned int i = 0; i < frames; i++) {
    local_mic_ring_push(samples[i]);
  }
}

//
// Utility function for retrieving mic samples
// from ring buffer
//
float audio_get_next_mic_sample(void) {
  static gint64 next_log_us = 0;
  static int min_queued = MY_RING_BUFFER_SIZE;
  static int max_queued = 0;
  static unsigned int diag_generation_seen = 0;
  float sample = local_mic_ring_pop();
  if (!radio_is_transmitting()) {
    next_log_us = 0;
    min_queued = MY_RING_BUFFER_SIZE;
    max_queued = 0;
    diag_generation_seen =
            atomic_load_explicit(&mic_ring_diag_generation, memory_order_acquire);
    atomic_store_explicit(&mic_ring_underruns, 0U, memory_order_relaxed);
    atomic_store_explicit(&mic_ring_overruns, 0U, memory_order_relaxed);
    return sample;
  }
  unsigned int diag_generation =
          atomic_load_explicit(&mic_ring_diag_generation, memory_order_acquire);
  if (diag_generation != diag_generation_seen) {
    diag_generation_seen = diag_generation;
    next_log_us = 0;
    min_queued = MY_RING_BUFFER_SIZE;
    max_queued = 0;
    atomic_store_explicit(&mic_ring_underruns, 0U, memory_order_relaxed);
    atomic_store_explicit(&mic_ring_overruns, 0U, memory_order_relaxed);
  }
  int outpt = atomic_load_explicit(&mic_ring_outpt, memory_order_acquire);
  int inpt = atomic_load_explicit(&mic_ring_inpt, memory_order_acquire);
  int queued = inpt - outpt;
  if (queued < 0) {
    queued += MY_RING_BUFFER_SIZE;
  }
  if (queued < min_queued) {
    min_queued = queued;
  }
  if (queued > max_queued) {
    max_queued = queued;
  }
  gint64 now_us = g_get_monotonic_time();
  if (next_log_us == 0) {
    next_log_us = now_us + G_USEC_PER_SEC;
  } else if (now_us >= next_log_us) {
    unsigned int underruns = atomic_exchange_explicit(&mic_ring_underruns, 0U, memory_order_relaxed);
    unsigned int overruns = atomic_exchange_explicit(&mic_ring_overruns, 0U, memory_order_relaxed);
    d_print("CoreAudio MIC ring: queued=%d (%.2f ms) min=%d (%.2f ms) max=%d (%.2f ms) underruns=%u overruns=%u\n",
            queued, (double) queued * 1000.0 / 48000.0,
            min_queued, (double) min_queued * 1000.0 / 48000.0,
            max_queued, (double) max_queued * 1000.0 / 48000.0,
            underruns, overruns);
    min_queued = MY_RING_BUFFER_SIZE;
    max_queued = 0;
    next_log_us = now_us + G_USEC_PER_SEC;
  }
  return sample;
}

//
// AUDIO_OPEN_OUTPUT
//
// Open native CoreAudio output for data from one of the RX.
//
int audio_open_output(RECEIVER *rx) {
  if (rx == NULL) {
    return -1;
  }
  /*
   * Allocate and initialize rings before the CoreAudio unit is started.
   * Publish the backend handle only after AudioOutputUnitStart() succeeds.
   */
  g_mutex_lock(&rx->local_audio_mutex);
  rx->coreaudio_output_handle = NULL;
  rx->local_audio_buffer = g_new(float, 2 * MY_RING_BUFFER_SIZE);
  rx->sidetone_buffer = g_new0(float, MY_RING_BUFFER_SIZE);
  atomic_store_explicit(&rx->local_audio_buffer_inpt, 0, memory_order_relaxed);
  atomic_store_explicit(&rx->local_audio_buffer_outpt, 0, memory_order_relaxed);
  atomic_store_explicit(&rx->sidetone_buffer_inpt, 0, memory_order_relaxed);
  atomic_store_explicit(&rx->sidetone_buffer_outpt, 0, memory_order_relaxed);
  rx->local_audio_cw_active = 0;
  if (rx->id >= 0 && rx->id < (int)(sizeof(output_ring_primed) / sizeof(output_ring_primed[0]))) {
    g_atomic_int_set(&output_ring_primed[rx->id], 0);
    g_atomic_int_set(&output_ring_starved[rx->id], 0);
    atomic_store_explicit(&rx_ring_diag_underruns[rx->id], 0U, memory_order_relaxed);
  }
  if (rx->local_audio_buffer == NULL || rx->sidetone_buffer == NULL) {
    g_free(rx->local_audio_buffer);
    g_free(rx->sidetone_buffer);
    rx->local_audio_buffer = NULL;
    rx->sidetone_buffer = NULL;
    g_mutex_unlock(&rx->local_audio_mutex);
    t_print("%s: allocate buffer failed\n", __func__);
    return -1;
  }
  g_mutex_unlock(&rx->local_audio_mutex);
  int channels = 0;
  void *handle = coreaudio_output_open(rx, rx->audio_name, &channels);
  if (handle == NULL) {
    g_mutex_lock(&rx->local_audio_mutex);
    g_free(rx->local_audio_buffer);
    g_free(rx->sidetone_buffer);
    rx->local_audio_buffer = NULL;
    rx->sidetone_buffer = NULL;
    g_mutex_unlock(&rx->local_audio_mutex);
    return -1;
  }
  g_mutex_lock(&rx->local_audio_mutex);
  rx->local_audio_channels = channels;
  rx->coreaudio_output_handle = handle;
  g_mutex_unlock(&rx->local_audio_mutex);
  t_print("%s: native CoreAudio output name=%s channels=%d\n",
          __func__, rx->audio_name, rx->local_audio_channels);
  return 0;
}


//
// AUDIO_CLOSE_INPUT
//
// close a TX microphone stream
//
void audio_close_input(void) {
  t_print("%s: native CoreAudio call audio_close_input\n", __func__);
  if (transmitter != NULL) {
    t_print("%s: micname=%s\n", __func__, transmitter->microphone_name);
  }
  void *handle = NULL;
  g_mutex_lock(&audio_mutex);
  handle = coreaudio_input_handle;
  coreaudio_input_handle = NULL;
  g_mutex_unlock(&audio_mutex);
  //
  // Stop and dispose AUHAL before freeing the lock-free mic ring.
  //
  coreaudio_input_close(handle);
  g_mutex_lock(&audio_mutex);
  if (mic_ring_buffer != NULL) {
    g_free(mic_ring_buffer);
    mic_ring_buffer = NULL;
  }
  atomic_store_explicit(&mic_ring_outpt, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_inpt, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_reset_pending, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_reset_frames, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_silence_frames, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_underruns, 0U, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_overruns, 0U, memory_order_relaxed);
  g_mutex_unlock(&audio_mutex);
}


//
// AUDIO_CLOSE_OUTPUT
//
// shut down the stream connected with audio from one of the RX
//
void audio_close_output(RECEIVER *rx) {
  t_print("%s: device=%s\n", __func__, rx->audio_name);
  /*
   * First prevent producers from entering audio_write()/cw_audio_write().
   * Then stop CoreAudio and wait for its callback to leave. Only after that
   * may the lock-free callback-visible rings be released.
   */
  void *handle;
  g_mutex_lock(&rx->local_audio_mutex);
  handle = rx->coreaudio_output_handle;
  rx->coreaudio_output_handle = NULL;
  g_mutex_unlock(&rx->local_audio_mutex);
  coreaudio_output_close(handle);
  g_mutex_lock(&rx->local_audio_mutex);
  if (rx->id >= 0 && rx->id < (int)(sizeof(output_ring_primed) / sizeof(output_ring_primed[0]))) {
    g_atomic_int_set(&output_ring_primed[rx->id], 0);
    g_atomic_int_set(&output_ring_starved[rx->id], 0);
  }
  g_free(rx->local_audio_buffer);
  g_free(rx->sidetone_buffer);
  rx->local_audio_buffer = NULL;
  rx->sidetone_buffer = NULL;
  atomic_store_explicit(&rx->local_audio_buffer_inpt, 0, memory_order_relaxed);
  atomic_store_explicit(&rx->local_audio_buffer_outpt, 0, memory_order_relaxed);
  atomic_store_explicit(&rx->sidetone_buffer_inpt, 0, memory_order_relaxed);
  atomic_store_explicit(&rx->sidetone_buffer_outpt, 0, memory_order_relaxed);
  rx->local_audio_cw_active = 0;
  g_mutex_unlock(&rx->local_audio_mutex);
}


//
// AUDIO_REPRIME_OUTPUT
//
// Re-prime the normal RX ring before RX is restarted after a TX/TUNE phase.
// rxtx() calls this while the receiver producer is still stopped, so this
// remains a single-producer operation with respect to the CoreAudio consumer.
// Existing queued audio is preserved; only missing samples up to RX_LAT_TARGET
// are added as silence.
//
void audio_reprime_output(RECEIVER *rx) {
  if (rx == NULL || rx->local_audio_buffer == NULL || rx->coreaudio_output_handle == NULL) {
    return;
  }
  int inpt = atomic_load_explicit(&rx->local_audio_buffer_inpt, memory_order_relaxed);
  int outpt = atomic_load_explicit(&rx->local_audio_buffer_outpt, memory_order_acquire);
  int avail = inpt - outpt;
  if (avail < 0) {
    avail += MY_RING_BUFFER_SIZE;
  }
  int rx_lat_low;
  int rx_lat_target;
  int rx_lat_high_base;
  rx_audio_latency_limits(&rx_lat_low, &rx_lat_target, &rx_lat_high_base);
  (void)rx_lat_low;
  (void)rx_lat_high_base;
  if (avail < rx_lat_target) {
    int oldpt = inpt;
    int missing = rx_lat_target - avail;
    for (int i = 0; i < missing; i++) {
      rx->local_audio_buffer[2 * oldpt] = 0.0f;
      rx->local_audio_buffer[2 * oldpt + 1] = 0.0f;
      oldpt++;
      if (oldpt >= MY_RING_BUFFER_SIZE) {
        oldpt = 0;
      }
    }
    atomic_store_explicit(&rx->local_audio_buffer_inpt, oldpt, memory_order_release);
    if (rx->id >= 0 && rx->id < 8) {
      g_atomic_int_set(&output_ring_starved[rx->id], 0);
      atomic_store_explicit(&rx_ring_diag_underruns[rx->id], 0U, memory_order_relaxed);
    }
    d_print("%s: RX%d reprime %d -> %d samples (added %d silence)\n",
            __func__, rx->id, avail, rx_lat_target, missing);
  }
}

//
// AUDIO_WRITE
//
// Store RX audio in the ring consumed by the native CoreAudio callback.
//
// Note that the check on radio_is_transmitting() takes care that "blocking"
// by the mutex can only occur in the moment of a RX/TX transition if
// both audio_write() and cw_audio_write() get a "go".
//
// So mutex locking/unlocking should only cost few CPU cycles in
// normal operation.
//
int audio_write(RECEIVER *rx, float left, float right) {
  static gint64 diag_next_log_us[8] = { 0 };
  static int diag_min_queued[8] = { 0 };
  static int diag_max_queued[8] = { 0 };
  static unsigned int diag_low_corrections[8] = { 0 };
  static unsigned int diag_high_corrections[8] = { 0 };
  int txmode = vfo_get_tx_mode();
  float *buffer = rx->local_audio_buffer;
  if (rx == active_receiver && radio_is_transmitting() && (txmode == modeCWU || txmode == modeCWL)) {
    // Stop producing new RX audio during CW TX. The existing RX tail drains naturally.
    return 0;
  }
  g_mutex_lock(&rx->local_audio_mutex);
  rx->local_audio_cw_active = 0;
  if (rx->coreaudio_output_handle != NULL && buffer != NULL) {
    int inpt = atomic_load_explicit(&rx->local_audio_buffer_inpt, memory_order_relaxed);
    int outpt = atomic_load_explicit(&rx->local_audio_buffer_outpt, memory_order_acquire);
    int avail = inpt - outpt;
    if (avail < 0) { avail += MY_RING_BUFFER_SIZE; }
    int rx_lat_low;
    int rx_lat_target;
    int rx_lat_high_base;
    rx_audio_latency_limits(&rx_lat_low, &rx_lat_target, &rx_lat_high_base);
    if (avail < rx_lat_low) {
      if (rx->id >= 0 && rx->id < 8) {
        diag_low_corrections[rx->id]++;
      }
      int oldpt = inpt;
      for (int i = 0; i < rx_lat_target - avail; i++) {
        buffer[2 * oldpt] = 0.0f;
        buffer[2 * oldpt + 1] = 0.0f;
        oldpt++;
        if (oldpt >= MY_RING_BUFFER_SIZE) { oldpt = 0; }
      }
      atomic_store_explicit(&rx->local_audio_buffer_inpt, oldpt, memory_order_release);
      inpt = oldpt;
      avail = rx_lat_target;
    }
    int rx_lat_high = rx_lat_target + (2 * rx->output_samples) + RX_LAT_GUARD;
    if (rx_lat_high < rx_lat_high_base) {
      rx_lat_high = rx_lat_high_base;
    }
    if (avail > rx_lat_high) {
      if (rx->id >= 0 && rx->id < 8) {
        diag_high_corrections[rx->id]++;
      }
      int oldpt = inpt - avail + rx_lat_target;
      while (oldpt < 0) { oldpt += MY_RING_BUFFER_SIZE; }
      while (oldpt >= MY_RING_BUFFER_SIZE) { oldpt -= MY_RING_BUFFER_SIZE; }
      atomic_store_explicit(&rx->local_audio_buffer_inpt, oldpt, memory_order_release);
      inpt = oldpt;
      avail = rx_lat_target;
      d_print("%s: RX ring high-water correction, reduced to %d samples\n",
              __func__, rx_lat_target);
    }
    if (rx->local_audio_mute) {
      left = 0.0f;
      right = 0.0f;
    }
    int oldpt = inpt;
    int newpt = oldpt + 1;
    if (newpt == MY_RING_BUFFER_SIZE) { newpt = 0; }
    outpt = atomic_load_explicit(&rx->local_audio_buffer_outpt, memory_order_acquire);
    if (newpt != outpt) {
      buffer[2 * oldpt] = left;
      buffer[2 * oldpt + 1] = right;
      atomic_store_explicit(&rx->local_audio_buffer_inpt, newpt, memory_order_release);
      inpt = newpt;
    }
    if (rx->id >= 0 && rx->id < 8) {
      int queued = inpt - outpt;
      if (queued < 0) {
        queued += MY_RING_BUFFER_SIZE;
      }
      if (diag_next_log_us[rx->id] == 0) {
        diag_min_queued[rx->id] = queued;
        diag_max_queued[rx->id] = queued;
        diag_next_log_us[rx->id] = g_get_monotonic_time() + G_USEC_PER_SEC;
      } else {
        if (queued < diag_min_queued[rx->id]) {
          diag_min_queued[rx->id] = queued;
        }
        if (queued > diag_max_queued[rx->id]) {
          diag_max_queued[rx->id] = queued;
        }
        gint64 now_us = g_get_monotonic_time();
        if (now_us >= diag_next_log_us[rx->id]) {
          unsigned int underruns =
                  atomic_exchange_explicit(&rx_ring_diag_underruns[rx->id],
                                           0U, memory_order_relaxed);
          d_print("CoreAudio RX ring: rx=%d queued=%d (%.2f ms) "
                  "min=%d (%.2f ms) max=%d (%.2f ms) "
                  "underruns=%u low_corr=%u high_corr=%u\n",
                  rx->id,
                  queued, (double) queued * 1000.0 / 48000.0,
                  diag_min_queued[rx->id],
                  (double) diag_min_queued[rx->id] * 1000.0 / 48000.0,
                  diag_max_queued[rx->id],
                  (double) diag_max_queued[rx->id] * 1000.0 / 48000.0,
                  underruns,
                  diag_low_corrections[rx->id],
                  diag_high_corrections[rx->id]);
          diag_min_queued[rx->id] = queued;
          diag_max_queued[rx->id] = queued;
          diag_low_corrections[rx->id] = 0;
          diag_high_corrections[rx->id] = 0;
          diag_next_log_us[rx->id] = now_us + G_USEC_PER_SEC;
        }
      }
    }
  }
  g_mutex_unlock(&rx->local_audio_mutex);
  return 0;
}

//
// During CW, between the elements the side tone contains "true" silence.
// We detect a sequence of 16 subsequent zero samples, and insert or delete
// a zero sample depending on the buffer water mark:
// If there are more than two CoreAudio buffers available, delete one sample,
// if it drops down to less than one CoreAudio buffer, insert one sample
//
// Thus we have an active latency management.
//
int cw_audio_write(RECEIVER *rx, float sample) {
  static gint64 diag_next_log_us = 0;
  static int diag_min_avail = MY_RING_BUFFER_SIZE;
  static int diag_max_avail = 0;
  static unsigned int diag_low_corrections = 0;
  static unsigned int diag_high_corrections = 0;
  g_mutex_lock(&rx->local_audio_mutex);
  if (rx->coreaudio_output_handle != NULL && rx->sidetone_buffer != NULL) {
    static int count = 0;
    int inpt = atomic_load_explicit(&rx->sidetone_buffer_inpt, memory_order_relaxed);
    int outpt = atomic_load_explicit(&rx->sidetone_buffer_outpt, memory_order_acquire);
    int avail = inpt - outpt;
    int adjust = 0;
    if (avail < 0) { avail += MY_RING_BUFFER_SIZE; }
    if (!rx->local_audio_cw_active) {
      // Prime only the sidetone ring; keep the RX fade-out tail intact.
      for (int i = 0; i < CW_LAT_TARGET; i++) {
        rx->sidetone_buffer[i] = 0.0f;
      }
      atomic_store_explicit(&rx->sidetone_buffer_outpt, 0, memory_order_relaxed);
      atomic_store_explicit(&rx->sidetone_buffer_inpt, CW_LAT_TARGET, memory_order_release);
      inpt = CW_LAT_TARGET;
      outpt = 0;
      avail = CW_LAT_TARGET;
      count = 0;
      diag_next_log_us = 0;
      diag_min_avail = MY_RING_BUFFER_SIZE;
      diag_max_avail = 0;
      diag_low_corrections = 0;
      diag_high_corrections = 0;
      atomic_store_explicit(&cw_ring_diag_underruns, 0U, memory_order_relaxed);
      rx->local_audio_cw_active = 1;
    }
    if (sample != 0.0f) { count = 0; }
    if (++count >= 16) {
      count = 0;
      if (avail > CW_LAT_HIGH) {
        adjust = 2;
        diag_high_corrections++;
      }
      if (avail < CW_LAT_LOW) {
        adjust = 1;
        diag_low_corrections++;
      }
    }
    if (adjust != 2) {
      int oldpt = inpt;
      int newpt = oldpt + 1;
      if (newpt == MY_RING_BUFFER_SIZE) { newpt = 0; }
      outpt = atomic_load_explicit(&rx->sidetone_buffer_outpt, memory_order_acquire);
      if (newpt != outpt) {
        rx->sidetone_buffer[oldpt] = (adjust == 1) ? 0.0f : sample;
        atomic_store_explicit(&rx->sidetone_buffer_inpt, newpt, memory_order_release);
        if (adjust == 1) {
          oldpt = newpt;
          newpt = oldpt + 1;
          if (newpt == MY_RING_BUFFER_SIZE) { newpt = 0; }
          outpt = atomic_load_explicit(&rx->sidetone_buffer_outpt, memory_order_acquire);
          if (newpt != outpt) {
            rx->sidetone_buffer[oldpt] = 0.0f;
            atomic_store_explicit(&rx->sidetone_buffer_inpt, newpt, memory_order_release);
          }
        }
      }
    }
    inpt = atomic_load_explicit(&rx->sidetone_buffer_inpt, memory_order_relaxed);
    outpt = atomic_load_explicit(&rx->sidetone_buffer_outpt, memory_order_acquire);
    avail = inpt - outpt;
    if (avail < 0) { avail += MY_RING_BUFFER_SIZE; }
    if (avail < diag_min_avail) { diag_min_avail = avail; }
    if (avail > diag_max_avail) { diag_max_avail = avail; }
    gint64 diag_now_us = g_get_monotonic_time();
    if (diag_next_log_us == 0) {
      diag_next_log_us = diag_now_us + G_USEC_PER_SEC;
    } else if (diag_now_us >= diag_next_log_us) {
      unsigned int diag_underruns =
              atomic_exchange_explicit(&cw_ring_diag_underruns, 0U, memory_order_relaxed);
      d_print("CoreAudio CW ring: queued=%d (%.2f ms) min=%d (%.2f ms) max=%d (%.2f ms) underruns=%u low_corr=%u high_corr=%u\n",
              avail, (double) avail * 1000.0 / 48000.0,
              diag_min_avail, (double) diag_min_avail * 1000.0 / 48000.0,
              diag_max_avail, (double) diag_max_avail * 1000.0 / 48000.0,
              diag_underruns, diag_low_corrections, diag_high_corrections);
      diag_min_avail = MY_RING_BUFFER_SIZE;
      diag_max_avail = 0;
      diag_low_corrections = 0;
      diag_high_corrections = 0;
      diag_next_log_us = diag_now_us + G_USEC_PER_SEC;
    }
  }
  g_mutex_unlock(&rx->local_audio_mutex);
  return 0;
}
