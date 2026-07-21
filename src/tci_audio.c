/*  Copyright (C)
*   2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
*   TCI server based on libwebsockets is a complete rebuild for deskHPSDR
*   exclusivly by Heiko Amft, DL1BZ
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
// TCI server based on libwebsockets
// complete rebuild for deskHPSDR by DL1BZ
//

#include <glib.h>
#include <math.h>
#include <string.h>

#include "message.h"
#include <wdsp.h>

#include "receiver.h"
#include "radio.h"
#include "tci_audio.h"

typedef struct _tci_audio_monitor_ring {
  GMutex mutex;
  float samples[TCI_AUDIO_MONITOR_RING_FRAMES * TCI_AUDIO_CHANNELS];
  guint64 write_count;
  guint64 read_count;
  guint dropped;
} TCI_AUDIO_MONITOR_RING;

typedef struct _tci_rx_audio_ring {
  GMutex mutex;
  float samples[TCI_RX_AUDIO_RING_FRAMES * TCI_AUDIO_CHANNELS];
  guint64 write_count;
  guint dropped;
} TCI_RX_AUDIO_RING;

typedef struct _tci_tx_audio_ring {
  GMutex mutex;
  float samples[TCI_TX_AUDIO_RING_FRAMES];
  guint64 write_count;
  guint64 read_count;
  guint dropped;
} TCI_TX_AUDIO_RING;

static TCI_RX_AUDIO_RING tci_rx_audio_ring[TCI_RX_AUDIO_MAX_RECEIVERS];
static TCI_TX_AUDIO_RING tci_tx_audio_ring;
static TCI_AUDIO_MONITOR_RING tci_audio_monitor_ring;
static int tci_audio_monitor_enabled = 0;
static int tci_rx_audio_enabled = 0;
static guint tci_rx_audio_wakeup_count = 0;
static gint64 tci_tx_audio_last_frame_us = 0;
static int tci_tx_audio_enabled = 0;
static volatile gint tci_tx_audio_draining = 0;
static volatile gint tci_tx_audio_pending_frames = 0;
static volatile gint tci_tx_audio_generation = 1;
static TCI_AUDIO_WAKEUP_CALLBACK tci_audio_wakeup_callback = NULL;
static gint tci_rx_audio_wakeup_pending = 0;
static TCI_AUDIO_TX_CHRONO_CALLBACK tci_audio_tx_chrono_callback = NULL;
int tci_audio_normalize_sample_rate (int sample_rate) {
  return (sample_rate == TCI_AUDIO_SAMPLE_RATE_24K) ? TCI_AUDIO_SAMPLE_RATE_24K : TCI_AUDIO_SAMPLE_RATE;
}

guint tci_audio_get_stream_frames (void) {
  return TCI_TX_AUDIO_FRAME_FRAMES;
}

void tci_audio_destroy_tx_resampler (void **resampler) {
  if (resampler != NULL && *resampler != NULL) {
    destroy_resampleFV (*resampler);
    *resampler = NULL;
  }
}

void tci_audio_destroy_rx_resamplers (void **resampler_l, void **resampler_r) {
  if (resampler_l != NULL && *resampler_l != NULL) {
    destroy_resampleFV (*resampler_l);
    *resampler_l = NULL;
  }
  if (resampler_r != NULL && *resampler_r != NULL) {
    destroy_resampleFV (*resampler_r);
    *resampler_r = NULL;
  }
}

static gboolean tci_audio_rx_wakeup_idle (gpointer data) {
  TCI_AUDIO_WAKEUP_CALLBACK callback;
  (void) data;
  callback = tci_audio_wakeup_callback;
  g_atomic_int_set (&tci_rx_audio_wakeup_pending, 0);
  if (callback != NULL) {
    callback();
  }
  return G_SOURCE_REMOVE;
}

static void tci_audio_queue_rx_wakeup (void) {
  if (tci_audio_wakeup_callback == NULL) {
    return;
  }
  if (g_atomic_int_compare_and_exchange (&tci_rx_audio_wakeup_pending, 0, 1)) {
    g_idle_add (tci_audio_rx_wakeup_idle, NULL);
  }
}

void tci_audio_monitor_set_active (int active) {
  TCI_AUDIO_MONITOR_RING *ring = &tci_audio_monitor_ring;
  g_mutex_lock (&ring->mutex);
  tci_audio_monitor_enabled = active ? 1 : 0;
  ring->write_count = 0;
  ring->read_count = 0;
  ring->dropped = 0;
  memset (ring->samples, 0, sizeof (ring->samples));
  g_mutex_unlock (&ring->mutex);
}

int tci_audio_monitor_is_active (void) {
  return tci_audio_monitor_enabled;
}

__attribute__ ((unused)) static void tci_audio_monitor_push (float left, float right) {
  TCI_AUDIO_MONITOR_RING *ring = &tci_audio_monitor_ring;
  guint index;
  if (!tci_audio_monitor_enabled) { return; }
  if (!g_mutex_trylock (&ring->mutex)) { return; }
  if (ring->write_count >= ring->read_count + TCI_AUDIO_MONITOR_RING_FRAMES) {
    ring->read_count = ring->write_count - TCI_AUDIO_MONITOR_RING_FRAMES + 1;
    ring->dropped++;
  }
  index = (guint) (ring->write_count % TCI_AUDIO_MONITOR_RING_FRAMES);
  ring->samples[ (index * TCI_AUDIO_CHANNELS)] = left;
  ring->samples[ (index * TCI_AUDIO_CHANNELS) + 1] = right;
  ring->write_count++;
  g_mutex_unlock (&ring->mutex);
}

__attribute__ ((unused)) static void tci_audio_monitor_push_block (const float *samples, guint frames) {
  TCI_AUDIO_MONITOR_RING *ring = &tci_audio_monitor_ring;
  if (!tci_audio_monitor_enabled || samples == NULL || frames == 0) { return; }
  if (!g_mutex_trylock (&ring->mutex)) { return; }
  for (guint i = 0; i < frames; i++) {
    guint index;
    if (ring->write_count >= ring->read_count + TCI_AUDIO_MONITOR_RING_FRAMES) {
      ring->read_count = ring->write_count - TCI_AUDIO_MONITOR_RING_FRAMES + 1;
      ring->dropped++;
    }
    index = (guint) (ring->write_count % TCI_AUDIO_MONITOR_RING_FRAMES);
    ring->samples[(index * TCI_AUDIO_CHANNELS)] = samples[(i * TCI_AUDIO_CHANNELS)] * 0.9f;
    ring->samples[(index * TCI_AUDIO_CHANNELS) + 1] = samples[(i * TCI_AUDIO_CHANNELS) + 1] * 0.9f;
    ring->write_count++;
  }
  g_mutex_unlock (&ring->mutex);
}

static void tci_audio_monitor_push_mono_block (const float *samples, guint frames, float gain) {
  TCI_AUDIO_MONITOR_RING *ring = &tci_audio_monitor_ring;
  if (!tci_audio_monitor_enabled || samples == NULL || frames == 0) { return; }
  if (!g_mutex_trylock (&ring->mutex)) { return; }
  for (guint i = 0; i < frames; i++) {
    guint index;
    float sample;
    if (ring->write_count >= ring->read_count + TCI_AUDIO_MONITOR_RING_FRAMES) {
      ring->read_count = ring->write_count - TCI_AUDIO_MONITOR_RING_FRAMES + 1;
      ring->dropped++;
    }
    sample = samples[i] * gain;
    index = (guint) (ring->write_count % TCI_AUDIO_MONITOR_RING_FRAMES);
    ring->samples[(index * TCI_AUDIO_CHANNELS)] = sample;
    ring->samples[(index * TCI_AUDIO_CHANNELS) + 1] = sample;
    ring->write_count++;
  }
  g_mutex_unlock (&ring->mutex);
}

void tci_audio_tx_reset (void) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  g_mutex_lock (&ring->mutex);
  ring->write_count = 0;
  ring->read_count = 0;
  ring->dropped = 0;
  memset (ring->samples, 0, sizeof (ring->samples));
  tci_tx_audio_last_frame_us = 0;
  g_atomic_int_set (&tci_tx_audio_pending_frames, 0);
  g_atomic_int_set (&tci_tx_audio_draining, 0);
  g_atomic_int_inc (&tci_tx_audio_generation);
  g_mutex_unlock (&ring->mutex);
}

void tci_audio_set_tx_chrono_callback (TCI_AUDIO_TX_CHRONO_CALLBACK callback) {
  tci_audio_tx_chrono_callback = callback;
}

void tci_audio_tx_set_active (int active) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  g_mutex_lock (&ring->mutex);
  tci_tx_audio_enabled = active ? 1 : 0;
  if (!tci_tx_audio_enabled) {
    tci_tx_audio_last_frame_us = 0;
    g_atomic_int_set (&tci_tx_audio_pending_frames, 0);
    g_atomic_int_set (&tci_tx_audio_draining, 0);
    g_atomic_int_inc (&tci_tx_audio_generation);
  }
  g_mutex_unlock (&ring->mutex);
}

int tci_audio_tx_is_active (void) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  gint64 last_frame_us;
  int enabled;
  g_mutex_lock (&ring->mutex);
  enabled = tci_tx_audio_enabled;
  last_frame_us = tci_tx_audio_last_frame_us;
  g_mutex_unlock (&ring->mutex);
  if (!enabled || last_frame_us == 0) {
    return 0;
  }
  return (g_get_monotonic_time() - last_frame_us) < 250000;
}

int tci_audio_tx_enabled (void) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  int enabled;
  g_mutex_lock (&ring->mutex);
  enabled = tci_tx_audio_enabled;
  g_mutex_unlock (&ring->mutex);
  return enabled;
}

void tci_audio_tx_begin_drain (void) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  g_mutex_lock (&ring->mutex);
  /*
   * Reject every TX_AUDIO frame that reaches the ring after the RX
   * request. Samples already queued, including the local cache in
   * tci_get_next_mic_sample(), remain part of pending_frames and are
   * consumed before MOX is released.
   */
  g_atomic_int_set (&tci_tx_audio_draining, 1);
  g_mutex_unlock (&ring->mutex);
}

void tci_audio_tx_cancel_drain (void) {
  g_atomic_int_set (&tci_tx_audio_draining, 0);
}

int tci_audio_tx_is_draining (void) {
  return g_atomic_int_get (&tci_tx_audio_draining) != 0;
}

guint64 tci_audio_tx_pending (void) {
  gint pending = g_atomic_int_get (&tci_tx_audio_pending_frames);
  return pending > 0 ? (guint64) pending : 0;
}

int tci_audio_tx_is_drained (void) {
  return tci_audio_tx_is_draining() && tci_audio_tx_pending() == 0;
}

static int tci_audio_tx_consume_pending (gint generation) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  int consume = 0;
  g_mutex_lock (&ring->mutex);
  if (tci_tx_audio_enabled &&
      generation == g_atomic_int_get (&tci_tx_audio_generation) &&
      g_atomic_int_get (&tci_tx_audio_pending_frames) > 0) {
    g_atomic_int_add (&tci_tx_audio_pending_frames, -1);
    consume = 1;
  }
  g_mutex_unlock (&ring->mutex);
  return consume;
}

void tci_get_next_mic_sample (double *micsample) {
  static int tx_audio_active = 0;
  static int prebuffering = 1;
  static float cache[TCI_TX_AUDIO_FRAME_FRAMES];
  static guint cache_len = 0;
  static guint cache_pos = 0;
  static guint chrono_sample_count = 0;
  static gint generation = 0;
  const guint prebuffer_frames = 4096;
  // const double tx_gain = 0.707;
  const double tx_gain = (transmitter->tci_tx_audio_gain_db == -3) ? 0.7071067811865476 : 1.0;
  if (micsample == NULL) {
    return;
  }
  gint current_generation = g_atomic_int_get (&tci_tx_audio_generation);
  if (generation != current_generation) {
    generation = current_generation;
    tx_audio_active = 0;
    prebuffering = 1;
    cache_len = 0;
    cache_pos = 0;
    chrono_sample_count = 0;
  }
  if (!tci_audio_tx_enabled()) {
    tx_audio_active = 0;
    prebuffering = 1;
    cache_len = 0;
    cache_pos = 0;
    chrono_sample_count = 0;
    return;
  }
  chrono_sample_count++;
  if (chrono_sample_count >= TCI_TX_AUDIO_FRAME_FRAMES) {
    chrono_sample_count -= TCI_TX_AUDIO_FRAME_FRAMES;
    if (tci_audio_tx_chrono_callback != NULL) {
      tci_audio_tx_chrono_callback();
    }
  }
  guint cache_available = (cache_len > cache_pos) ? (cache_len - cache_pos) : 0;
  guint64 ring_available = tci_audio_tx_available();
  int draining = tci_audio_tx_is_draining();
  if (prebuffering && !draining) {
    if ((ring_available + cache_available) < prebuffer_frames) {
      return;
    }
    prebuffering = 0;
  } else if (draining) {
    /* A final partial TCI block must not remain stranded below 4096 frames. */
    prebuffering = 0;
  }
  if (cache_pos >= cache_len) {
    cache_len = tci_audio_tx_read (cache, (guint) (sizeof (cache) / sizeof (cache[0])));
    cache_pos = 0;
  }
  if (cache_pos < cache_len) {
    float sample = cache[cache_pos++];
    if (tci_audio_tx_consume_pending(generation)) {
      *micsample = (double) sample * tx_gain;
      tx_audio_active = 1;
    } else {
      tx_audio_active = 0;
      prebuffering = 1;
      cache_len = 0;
      cache_pos = 0;
    }
  } else if (tx_audio_active) {
    prebuffering = draining ? 0 : 1;
    cache_len = 0;
    cache_pos = 0;
  }
}

static void tci_audio_tx_push_block (const float *samples, guint frames) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  if (samples == NULL || frames == 0) { return; }
  g_mutex_lock (&ring->mutex);
  if (!tci_tx_audio_enabled || g_atomic_int_get (&tci_tx_audio_draining)) {
    g_mutex_unlock (&ring->mutex);
    return;
  }
  tci_tx_audio_last_frame_us = g_get_monotonic_time();
  for (guint i = 0; i < frames; i++) {
    guint index;
    if (ring->write_count >= ring->read_count + TCI_TX_AUDIO_RING_FRAMES) {
      ring->read_count = ring->write_count - TCI_TX_AUDIO_RING_FRAMES + 1;
      ring->dropped++;
      if (g_atomic_int_get (&tci_tx_audio_pending_frames) > 0) {
        g_atomic_int_add (&tci_tx_audio_pending_frames, -1);
      }
    }
    index = (guint) (ring->write_count % TCI_TX_AUDIO_RING_FRAMES);
    ring->samples[index] = samples[i];
    ring->write_count++;
    g_atomic_int_inc (&tci_tx_audio_pending_frames);
  }
  g_mutex_unlock (&ring->mutex);
}

guint64 tci_audio_tx_available (void) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  guint64 available;
  g_mutex_lock (&ring->mutex);
  available = ring->write_count - ring->read_count;
  g_mutex_unlock (&ring->mutex);
  return available;
}

void tci_audio_tx_debug_snapshot (guint64 *write_count, guint64 *read_count,
                                  guint *dropped, guint64 *available, int *enabled) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  guint64 local_write;
  guint64 local_read;
  g_mutex_lock (&ring->mutex);
  local_write = ring->write_count;
  local_read = ring->read_count;
  if (write_count != NULL) { *write_count = local_write; }
  if (read_count != NULL) { *read_count = local_read; }
  if (dropped != NULL) { *dropped = ring->dropped; }
  if (available != NULL) { *available = local_write - local_read; }
  if (enabled != NULL) { *enabled = tci_tx_audio_enabled; }
  g_mutex_unlock (&ring->mutex);
}

guint tci_audio_tx_read (float *out, guint frames) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  guint copied = 0;
  if (out == NULL || frames == 0) { return 0; }
  memset (out, 0, frames * sizeof (float));
  g_mutex_lock (&ring->mutex);
  while (copied < frames && ring->read_count < ring->write_count) {
    guint index = (guint) (ring->read_count % TCI_TX_AUDIO_RING_FRAMES);
    out[copied++] = ring->samples[index];
    ring->read_count++;
  }
  g_mutex_unlock (&ring->mutex);
  return copied;
}

guint tci_audio_monitor_read (float *out, guint frames) {
  TCI_AUDIO_MONITOR_RING *ring = &tci_audio_monitor_ring;
  guint copied = 0;
  if (out == NULL || frames == 0) { return 0; }
  memset (out, 0, frames * TCI_AUDIO_CHANNELS * sizeof (float));
  if (!tci_audio_monitor_enabled) { return 0; }
  g_mutex_lock (&ring->mutex);
  while (copied < frames && ring->read_count < ring->write_count) {
    guint index = (guint) (ring->read_count % TCI_AUDIO_MONITOR_RING_FRAMES);
    out[ (copied * TCI_AUDIO_CHANNELS)] = ring->samples[ (index * TCI_AUDIO_CHANNELS)];
    out[ (copied * TCI_AUDIO_CHANNELS) + 1] = ring->samples[ (index * TCI_AUDIO_CHANNELS) + 1];
    ring->read_count++;
    copied++;
  }
  g_mutex_unlock (&ring->mutex);
  return copied;
}

void tci_audio_set_active (int active) {
  tci_rx_audio_enabled = active ? 1 : 0;
}

int tci_audio_is_active (void) {
  return tci_rx_audio_enabled;
}

void tci_audio_set_wakeup_callback (TCI_AUDIO_WAKEUP_CALLBACK callback) {
  tci_audio_wakeup_callback = callback;
  if (callback == NULL) {
    g_atomic_int_set (&tci_rx_audio_wakeup_pending, 0);
  }
}

void tci_audio_rx_sample (RECEIVER *rx, float left, float right) {
  float samples[TCI_AUDIO_CHANNELS];
  samples[0] = left;
  samples[1] = right;
  tci_audio_rx_block(rx, samples, 1);
}

void tci_audio_rx_block (RECEIVER *rx, const float *samples, guint frames) {
  TCI_RX_AUDIO_RING *ring;
  int do_wakeup = 0;
  int id;
  if (!tci_rx_audio_enabled || rx == NULL || samples == NULL || frames == 0) { return; }
  id = rx->id;
  if (id < 0 || id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return; }
  ring = &tci_rx_audio_ring[id];
  g_mutex_lock (&ring->mutex);
  for (guint i = 0; i < frames; i++) {
    guint index = (guint) (ring->write_count % TCI_RX_AUDIO_RING_FRAMES);
    ring->samples[(index * TCI_AUDIO_CHANNELS)] = samples[(i * TCI_AUDIO_CHANNELS)];
    ring->samples[(index * TCI_AUDIO_CHANNELS) + 1] = samples[(i * TCI_AUDIO_CHANNELS) + 1];
    ring->write_count++;
  }
  {
    guint old_count = tci_rx_audio_wakeup_count;
    tci_rx_audio_wakeup_count += frames;
    if ((frames >= TCI_RX_AUDIO_FRAME_FRAMES) ||
        ((old_count % TCI_RX_AUDIO_FRAME_FRAMES) + frames >= TCI_RX_AUDIO_FRAME_FRAMES)) {
      do_wakeup = 1;
    }
  }
  g_mutex_unlock (&ring->mutex);
  if (do_wakeup) {
    tci_audio_queue_rx_wakeup();
  }
}

guint64 tci_audio_get_write_count (int receiver_id) {
  guint64 write_count = 0;
  TCI_RX_AUDIO_RING *ring;
  if (receiver_id < 0 || receiver_id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return 0; }
  ring = &tci_rx_audio_ring[receiver_id];
  g_mutex_lock (&ring->mutex);
  write_count = ring->write_count;
  g_mutex_unlock (&ring->mutex);
  return write_count;
}

static guint tci_audio_copy (int receiver_id, guint64 *read_count, float *out, guint max_frames) {
  TCI_RX_AUDIO_RING *ring;
  guint64 available;
  guint frames;
  if (read_count == NULL || out == NULL || receiver_id < 0 || receiver_id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return 0; }
  ring = &tci_rx_audio_ring[receiver_id];
  if (!g_mutex_trylock (&ring->mutex)) { return 0; }
  if (*read_count + TCI_RX_AUDIO_RING_FRAMES < ring->write_count) {
    *read_count = ring->write_count - TCI_RX_AUDIO_RING_FRAMES;
    ring->dropped++;
  }
  available = ring->write_count - *read_count;
  if (available < max_frames) {
    g_mutex_unlock (&ring->mutex);
    return 0;
  }
  frames = max_frames;
  for (guint i = 0; i < frames; i++) {
    guint index = (guint) ((*read_count + i) % TCI_RX_AUDIO_RING_FRAMES);
    out[ (i * TCI_AUDIO_CHANNELS)] = ring->samples[ (index * TCI_AUDIO_CHANNELS)];
    out[ (i * TCI_AUDIO_CHANNELS) + 1] = ring->samples[ (index * TCI_AUDIO_CHANNELS) + 1];
  }
  *read_count += frames;
  g_mutex_unlock (&ring->mutex);
  return frames;
}


guint tci_audio_get_frame (int receiver_id, guint64 *read_count, unsigned char *frame, size_t frame_size,
                           size_t *frame_len, int sample_rate, void **resampler_l, void **resampler_r) {
  float audio[TCI_RX_AUDIO_FRAME_FRAMES * TCI_AUDIO_CHANNELS];
  float audio_l[TCI_RX_AUDIO_FRAME_FRAMES];
  float audio_r[TCI_RX_AUDIO_FRAME_FRAMES];
  float resampled_l[TCI_RX_AUDIO_FRAME_FRAMES];
  float resampled_r[TCI_RX_AUDIO_FRAME_FRAMES];
  TCI_STREAM_HEADER header;
  guint frames;
  guint out_frames;
  int out_l;
  int out_r;
  size_t data_bytes;
  if (frame_len != NULL) {
    *frame_len = 0;
  }
  if (read_count == NULL || frame == NULL || frame_len == NULL) { return 0; }
  if (frame_size < TCI_AUDIO_RX_FRAME_MAX_BYTES) { return 0; }
  frames = tci_audio_copy (receiver_id, read_count, audio, TCI_RX_AUDIO_FRAME_FRAMES);
  if (frames == 0) { return 0; }
  sample_rate = tci_audio_normalize_sample_rate (sample_rate);
  out_frames = frames;
  if (sample_rate == TCI_AUDIO_SAMPLE_RATE_24K) {
    if (resampler_l == NULL || resampler_r == NULL) { return 0; }
    if (*resampler_l == NULL) {
      *resampler_l = create_resampleFV (TCI_AUDIO_SAMPLE_RATE, TCI_AUDIO_SAMPLE_RATE_24K);
    }
    if (*resampler_r == NULL) {
      *resampler_r = create_resampleFV (TCI_AUDIO_SAMPLE_RATE, TCI_AUDIO_SAMPLE_RATE_24K);
    }
    if (*resampler_l == NULL || *resampler_r == NULL) { return 0; }
    for (guint i = 0; i < frames; i++) {
      audio_l[i] = audio[(i * TCI_AUDIO_CHANNELS)];
      audio_r[i] = audio[(i * TCI_AUDIO_CHANNELS) + 1];
    }
    out_l = 0;
    out_r = 0;
    xresampleFV (audio_l, resampled_l, (int) frames, &out_l, *resampler_l);
    xresampleFV (audio_r, resampled_r, (int) frames, &out_r, *resampler_r);
    out_frames = (out_l < out_r) ? (guint) out_l : (guint) out_r;
    if (out_frames > TCI_RX_AUDIO_FRAME_FRAMES) {
      out_frames = TCI_RX_AUDIO_FRAME_FRAMES;
    }
    for (guint i = 0; i < out_frames; i++) {
      audio[(i * TCI_AUDIO_CHANNELS)] = resampled_l[i];
      audio[(i * TCI_AUDIO_CHANNELS) + 1] = resampled_r[i];
    }
  } else {
    tci_audio_destroy_rx_resamplers (resampler_l, resampler_r);
  }
  memset (&header, 0, sizeof (header));
  header.receiver = (uint32_t) receiver_id;
  header.sample_rate = (uint32_t) sample_rate;
  header.format = TCI_AUDIO_FORMAT_FLOAT32;
  header.length = (uint32_t) (out_frames * TCI_AUDIO_CHANNELS);
  header.type = TCI_STREAM_RX_AUDIO;
  header.channels = TCI_AUDIO_CHANNELS;
  data_bytes = (size_t) out_frames * TCI_AUDIO_CHANNELS * sizeof (float);
  memcpy (frame, &header, sizeof (header));
  memcpy (frame + sizeof (header), audio, data_bytes);
  *frame_len = sizeof (header) + data_bytes;
  return out_frames;
}

void tci_audio_handle_tx_frame (const unsigned char *data, size_t len, int client_sample_rate,
                                void **resampler_24_to_48) {
  TCI_STREAM_HEADER header;
  size_t payload_bytes;
  size_t sample_count;
  if (data == NULL || len < 64) { return; }
  memcpy (&header, data, sizeof (header));
  if (header.type != TCI_STREAM_TX_AUDIO) { return; }
  payload_bytes = len - 64;
  if (payload_bytes < sizeof (float)) { return; }
  if (header.length <= 0 || (64 + ((size_t) header.length * sizeof (float))) > len) {
    return;
  }
  sample_count = (size_t) header.length;
  if (sample_count < 2) { return; }
  float samples[TCI_TX_AUDIO_INTERNAL_FRAME_FRAMES];
  float input[TCI_TX_AUDIO_24K_FRAME_FRAMES];
  float output[TCI_TX_AUDIO_INTERNAL_FRAME_FRAMES];
  guint frames = (guint) (sample_count / 2);
  guint push_frames;
  int sample_rate = tci_audio_normalize_sample_rate (client_sample_rate);
  if (sample_rate == TCI_AUDIO_SAMPLE_RATE_24K) {
    if (frames > TCI_TX_AUDIO_24K_FRAME_FRAMES) {
      frames = TCI_TX_AUDIO_24K_FRAME_FRAMES;
    }
  } else if (frames > TCI_TX_AUDIO_FRAME_FRAMES) {
    frames = TCI_TX_AUDIO_FRAME_FRAMES;
  }
  for (guint i = 0; i < frames; i++) {
    float left;
    float right;
    memcpy (&left, data + 64 + (((size_t) i * 2) * sizeof (float)), sizeof (left));
    memcpy (&right, data + 64 + ((((size_t) i * 2) + 1) * sizeof (float)), sizeof (right));
    samples[i] = left;
    if (sample_rate == TCI_AUDIO_SAMPLE_RATE_24K) {
      input[i] = left;
    }
  }
  push_frames = frames;
  if (sample_rate == TCI_AUDIO_SAMPLE_RATE_24K) {
    int out_frames = 0;
    if (resampler_24_to_48 == NULL) { return; }
    if (*resampler_24_to_48 == NULL) {
      *resampler_24_to_48 = create_resampleFV (TCI_AUDIO_SAMPLE_RATE_24K, TCI_AUDIO_SAMPLE_RATE);
    }
    if (*resampler_24_to_48 == NULL) { return; }
    xresampleFV (input, output, (int) frames, &out_frames, *resampler_24_to_48);
    if (out_frames <= 0) { return; }
    push_frames = (guint) out_frames;
    if (push_frames > TCI_TX_AUDIO_INTERNAL_FRAME_FRAMES) {
      push_frames = TCI_TX_AUDIO_INTERNAL_FRAME_FRAMES;
    }
    memcpy (samples, output, push_frames * sizeof (float));
  } else {
    tci_audio_destroy_tx_resampler (resampler_24_to_48);
  }
  if (tci_audio_monitor_is_active()) {
    tci_audio_monitor_push_mono_block (samples, push_frames, 0.01f);
  }
  tci_audio_tx_push_block (samples, push_frames);
}
