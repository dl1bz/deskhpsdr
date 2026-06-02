/* Copyright (C)
* 2024,2025, 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
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

#include <glib.h>
#include <math.h>
#include <string.h>

#include "message.h"

#include "receiver.h"
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
static guint64 tci_tx_audio_frames = 0;
static gint64 tci_tx_audio_last_frame_us = 0;
static int tci_tx_audio_enabled = 0;
static TCI_AUDIO_WAKEUP_CALLBACK tci_audio_wakeup_callback = NULL;
static TCI_AUDIO_TX_CHRONO_CALLBACK tci_audio_tx_chrono_callback = NULL;

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

static void tci_audio_monitor_push (float left, float right) {
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

void tci_audio_tx_reset (void) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  g_mutex_lock (&ring->mutex);
  ring->write_count = 0;
  ring->read_count = 0;
  ring->dropped = 0;
  memset (ring->samples, 0, sizeof (ring->samples));
  tci_tx_audio_last_frame_us = 0;
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

double tci_get_next_mic_sample (void) {
  static unsigned long underruns = 0;
  static double last_sample = 0.0;
  static int tx_audio_active = 0;
  static int prebuffering = 1;
  static float cache[TCI_TX_AUDIO_FRAME_FRAMES];
  static guint cache_len = 0;
  static guint cache_pos = 0;
  static int resampler_valid = 0;
  static double resampler_phase = 0.0;
  static double resampler_s0 = 0.0;
  static double resampler_s1 = 0.0;
  static guint chrono_sample_count = 0;
  static guint64 diag_samples = 0;
  static guint64 diag_prebuffer_returns = 0;
  static guint64 diag_underruns = 0;
  static guint64 diag_cache_refills = 0;
  static guint64 diag_cache_empty = 0;
  static guint64 diag_chrono = 0;
  static guint64 diag_min_available = (guint64) -1;
  static guint64 diag_max_available = 0;
  const guint prebuffer_frames = 4096;
  const double base_step = (double) TCI_AUDIO_SAMPLE_RATE / 48000.0;
  const double target = 4096.0;
  const double max_adjust = 0.005;
  const double gain = 0.000001;
  const double tx_gain = 0.707;
  double sample = 0.0;
  if (!tci_audio_tx_enabled()) {
    tx_audio_active = 0;
    prebuffering = 1;
    last_sample = 0.0;
    cache_len = 0;
    cache_pos = 0;
    resampler_valid = 0;
    resampler_phase = 0.0;
    resampler_s0 = 0.0;
    resampler_s1 = 0.0;
    chrono_sample_count = 0;
    diag_samples = 0;
    diag_prebuffer_returns = 0;
    diag_underruns = 0;
    diag_cache_refills = 0;
    diag_cache_empty = 0;
    diag_chrono = 0;
    diag_min_available = (guint64) -1;
    diag_max_available = 0;
    return 0.0;
  }
  diag_samples++;
  chrono_sample_count++;
  if (chrono_sample_count >= TCI_TX_AUDIO_FRAME_FRAMES) {
    chrono_sample_count -= TCI_TX_AUDIO_FRAME_FRAMES;
    diag_chrono++;
    if (tci_audio_tx_chrono_callback != NULL) {
      tci_audio_tx_chrono_callback();
    }
  }
  guint cache_available = (cache_len > cache_pos) ? (cache_len - cache_pos) : 0;
  guint64 ring_available = tci_audio_tx_available();
  guint64 total_available = ring_available + cache_available;
  if (total_available < diag_min_available) {
    diag_min_available = total_available;
  }
  if (total_available > diag_max_available) {
    diag_max_available = total_available;
  }
  if (prebuffering) {
    if (total_available < prebuffer_frames) {
      diag_prebuffer_returns++;
      return 0.0;
    }
    prebuffering = 0;
  }
  while (!resampler_valid || resampler_phase >= 1.0) {
    float tci_sample = 0.0f;
    if (cache_pos >= cache_len) {
      cache_len = tci_audio_tx_read (cache, (guint) (sizeof (cache) / sizeof (cache[0])));
      cache_pos = 0;
      if (cache_len > 0) {
        diag_cache_refills++;
      } else {
        diag_cache_empty++;
      }
    }
    if (cache_pos < cache_len) {
      resampler_s0 = resampler_s1;
      tci_sample = cache[cache_pos++];
      resampler_s1 = (double) tci_sample;
      resampler_valid = 1;
      if (resampler_phase >= 1.0) {
        resampler_phase -= 1.0;
      }
    } else if (tx_audio_active) {
      underruns++;
      diag_underruns++;
      if ((underruns % 1000) == 0) {
        t_print ("TCI TX audio underruns=%lu diag_underruns=%llu prebuffer_returns=%llu cache_empty=%llu cache_refills=%llu available_min=%llu available_max=%llu chrono=%llu\n",
                 underruns,
                 (unsigned long long) diag_underruns,
                 (unsigned long long) diag_prebuffer_returns,
                 (unsigned long long) diag_cache_empty,
                 (unsigned long long) diag_cache_refills,
                 (unsigned long long) diag_min_available,
                 (unsigned long long) diag_max_available,
                 (unsigned long long) diag_chrono);
      }
      prebuffering = 1;
      cache_len = 0;
      cache_pos = 0;
      resampler_valid = 0;
      resampler_phase = 0.0;
      resampler_s0 = 0.0;
      resampler_s1 = 0.0;
      last_sample *= 0.98;
      return last_sample * tx_gain;
    } else {
      return 0.0;
    }
  }
  if (resampler_valid) {
    guint64 current_available = tci_audio_tx_available() +
                                ((cache_len > cache_pos) ? (cache_len - cache_pos) : 0);
    double available = (double) current_available;
    double adjust = (available - target) * gain;
    if (current_available < diag_min_available) {
      diag_min_available = current_available;
    }
    if (current_available > diag_max_available) {
      diag_max_available = current_available;
    }
    if ((diag_samples % 240000) == 0) {
      t_print ("TCI TX diag samples=%llu underruns=%llu prebuffer_returns=%llu cache_empty=%llu cache_refills=%llu available_min=%llu available_max=%llu chrono=%llu\n",
               (unsigned long long) diag_samples,
               (unsigned long long) diag_underruns,
               (unsigned long long) diag_prebuffer_returns,
               (unsigned long long) diag_cache_empty,
               (unsigned long long) diag_cache_refills,
               (unsigned long long) diag_min_available,
               (unsigned long long) diag_max_available,
               (unsigned long long) diag_chrono);
      diag_min_available = (guint64) -1;
      diag_max_available = 0;
    }
    double step;
    if (adjust > max_adjust) {
      adjust = max_adjust;
    } else if (adjust < -max_adjust) {
      adjust = -max_adjust;
    }
    step = base_step * (1.0 + adjust);
    sample = (resampler_s0 + ((resampler_s1 - resampler_s0) * resampler_phase)) * tx_gain;
    last_sample = sample / tx_gain;
    tx_audio_active = 1;
    resampler_phase += step;
  }
  return sample;
}

static void tci_audio_tx_push_block (const float* samples, guint frames) {
  TCI_TX_AUDIO_RING *ring = &tci_tx_audio_ring;
  if (samples == NULL || frames == 0) { return; }
  g_mutex_lock (&ring->mutex);
  if (!tci_tx_audio_enabled) {
    g_mutex_unlock (&ring->mutex);
    return;
  }
  tci_tx_audio_last_frame_us = g_get_monotonic_time();
  for (guint i = 0; i < frames; i++) {
    guint index;
    if (ring->write_count >= ring->read_count + TCI_TX_AUDIO_RING_FRAMES) {
      ring->read_count = ring->write_count - TCI_TX_AUDIO_RING_FRAMES + 1;
      ring->dropped++;
    }
    index = (guint) (ring->write_count % TCI_TX_AUDIO_RING_FRAMES);
    ring->samples[index] = samples[i];
    ring->write_count++;
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

guint tci_audio_tx_read (float* out, guint frames) {
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

guint tci_audio_monitor_read (float* out, guint frames) {
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
}

void tci_audio_rx_sample (RECEIVER *rx, float left, float right) {
  TCI_RX_AUDIO_RING *ring;
  guint index;
  int do_wakeup = 0;
  int id;
  if (!tci_rx_audio_enabled || rx == NULL) { return; }
  id = rx->id;
  if (id < 0 || id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return; }
  ring = &tci_rx_audio_ring[id];
  if (!g_mutex_trylock (&ring->mutex)) { return; }
  index = (guint) (ring->write_count % TCI_RX_AUDIO_RING_FRAMES);
  ring->samples[ (index * TCI_AUDIO_CHANNELS)] = left;
  ring->samples[ (index * TCI_AUDIO_CHANNELS) + 1] = right;
  ring->write_count++;
  // tci_audio_monitor_push(left, right);
  tci_audio_monitor_push (left * 0.9f, right * 0.9f);
  if ((++tci_rx_audio_wakeup_count % TCI_RX_AUDIO_FRAME_FRAMES) == 0) {
    do_wakeup = 1;
  }
  g_mutex_unlock (&ring->mutex);
  if (do_wakeup && tci_audio_wakeup_callback != NULL) {
    tci_audio_wakeup_callback();
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

static guint tci_audio_copy (int receiver_id, guint64 *read_count, float* out, guint max_frames) {
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
  frames = (available < max_frames) ? (guint) available : max_frames;
  for (guint i = 0; i < frames; i++) {
    guint index = (guint) ((*read_count + i) % TCI_RX_AUDIO_RING_FRAMES);
    out[ (i * TCI_AUDIO_CHANNELS)] = ring->samples[ (index * TCI_AUDIO_CHANNELS)];
    out[ (i * TCI_AUDIO_CHANNELS) + 1] = ring->samples[ (index * TCI_AUDIO_CHANNELS) + 1];
  }
  *read_count += frames;
  g_mutex_unlock (&ring->mutex);
  return frames;
}


guint tci_audio_get_frame (int receiver_id, guint64 *read_count, unsigned char* frame, size_t frame_size,
                           size_t *frame_len) {
  float audio[TCI_RX_AUDIO_FRAME_FRAMES * TCI_AUDIO_CHANNELS];
  TCI_STREAM_HEADER header;
  guint frames;
  size_t data_bytes;
  if (frame_len != NULL) {
    *frame_len = 0;
  }
  if (read_count == NULL || frame == NULL || frame_len == NULL) { return 0; }
  if (frame_size < TCI_AUDIO_RX_FRAME_MAX_BYTES) { return 0; }
  frames = tci_audio_copy (receiver_id, read_count, audio, TCI_RX_AUDIO_FRAME_FRAMES);
  if (frames == 0) { return 0; }
  memset (&header, 0, sizeof (header));
  header.receiver = (uint32_t) receiver_id;
  header.sample_rate = TCI_AUDIO_SAMPLE_RATE;
  header.format = TCI_AUDIO_FORMAT_FLOAT32;
  header.length = (uint32_t) (frames * TCI_AUDIO_CHANNELS);
  header.type = TCI_STREAM_RX_AUDIO;
  header.channels = TCI_AUDIO_CHANNELS;
  data_bytes = (size_t) frames * TCI_AUDIO_CHANNELS * sizeof (float);
  memcpy (frame, &header, sizeof (header));
  memcpy (frame + sizeof (header), audio, data_bytes);
  *frame_len = sizeof (header) + data_bytes;
  return frames;
}

void tci_audio_handle_tx_frame (const unsigned char* data, size_t len) {
  TCI_STREAM_HEADER header;
  size_t payload_bytes;
  size_t sample_count;
  int channels;
  float peak = 0.0f;
  if (data == NULL || len < 64) { return; }
  memcpy (&header, data, sizeof (header));
  if (header.type != TCI_STREAM_TX_AUDIO) { return; }
  payload_bytes = len - 64;
  if (payload_bytes < sizeof (float)) { return; }
  if (header.length <= 0 || (64 + ((size_t) header.length * sizeof (float))) > len) {
    return;
  }
  sample_count = (size_t) header.length;
  channels = 2;
  if (sample_count < 2) { return; }
  float samples[TCI_TX_AUDIO_FRAME_FRAMES];
  guint frames = (guint) (sample_count / 2);
  if (frames > TCI_TX_AUDIO_FRAME_FRAMES) {
    frames = TCI_TX_AUDIO_FRAME_FRAMES;
  }
  for (guint i = 0; i < frames; i++) {
    float left;
    float right;
    float vleft;
    float vright;
    memcpy (&left, data + 64 + (((size_t) i * 2) * sizeof (float)), sizeof (left));
    memcpy (&right, data + 64 + ((((size_t) i * 2) + 1) * sizeof (float)), sizeof (right));
    samples[i] = left;
    vleft = fabsf (left);
    vright = fabsf (right);
    if (vleft > peak) {
      peak = vleft;
    }
    if (vright > peak) {
      peak = vright;
    }
    if (tci_audio_monitor_is_active()) {
      tci_audio_monitor_push (left * 0.01f, right * 0.01f);
    }
  }
  tci_audio_tx_push_block (samples, frames);
  tci_tx_audio_frames++;
  if ((tci_tx_audio_frames % 100) == 0) {
    t_print ("TCI TX audio frames=%llu channels=%d samples=%zu frames=%u peak=%e\n",
             (unsigned long long) tci_tx_audio_frames,
             channels,
             sample_count,
             frames,
             peak);
  }
}
