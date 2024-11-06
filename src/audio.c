/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT
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
// Some important parameters
// Note that we keep the playback buffers at half-filling so
// we can use a larger latency there.
//
//
// while it is kept above out_low_water
//
static const int inp_latency = 125000;
static const int out_latency = 200000;

static const int mic_buffer_size = 256;
static const int out_buffer_size = 256;

static const int out_buflen = 48 * (out_latency / 1000); // Length of ALSA buffer
static const int out_cw_border = 1536;                // separates CW-TX from other buffer fillings

static const int cw_mid_water  = 1024;                // target buffer filling for CW
static const int cw_low_water  =  896;                // low water mark for CW
static const int cw_high_water = 1152;                // high water mark for CW

#include <gtk/gtk.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <semaphore.h>

#include <alsa/asoundlib.h>

#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "audio.h"
#include "mode.h"
#include "vfo.h"
#include "message.h"

int audio = 0;
GMutex audio_mutex;

static snd_pcm_t *record_handle = NULL;
static snd_pcm_format_t record_audio_format;

static void *mic_buffer = NULL;

static GThread *mic_read_thread_id = NULL;

static int running = FALSE;

//
// TODO: include SND_PCM_FORMAT_IEC958_SUBFRAME_LE, such that ALSA
//       can directly play on HDMI monitors. Implementation is not
//       super-easy since this case must then also be considered in
//       audio_write.
//
#define FORMATS 3
static snd_pcm_format_t formats[3] = {
  SND_PCM_FORMAT_FLOAT_LE,
  SND_PCM_FORMAT_S32_LE,
  SND_PCM_FORMAT_S16_LE
};

static void *mic_read_thread(void *arg);

int n_input_devices;
AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
int n_output_devices;
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

//
// Ring buffer for "local microphone" samples
// NOTE: lead large buffer for some "loopback" devices which produce
//       samples in large chunks if fed from digimode programs.
//
#define MICRINGLEN 6000
float  *mic_ring_buffer = NULL;
int     mic_ring_read_pt = 0;
int     mic_ring_write_pt = 0;

int audio_open_output(RECEIVER *rx) {
  int err;
  unsigned int rate = 48000;
  unsigned int channels = 2;
  int soft_resample = 1;
  t_print("%s: rx=%d %s buffer_size=%d\n", __FUNCTION__, rx->id, rx->audio_name, out_buffer_size);
  int i;
  char hw[128];
  i = 0;

  while (i < 127 && rx->audio_name[i] != ' ') {
    hw[i] = rx->audio_name[i];
    i++;
  }

  hw[i] = '\0';
  t_print("%s: hw=%s\n", __FUNCTION__, hw);

  for (i = 0; i < FORMATS; i++) {
    g_mutex_lock(&rx->local_audio_mutex);

    if ((err = snd_pcm_open (&rx->playback_handle, hw, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
      t_print("%s: cannot open audio device %s (%s)\n",
              __FUNCTION__,
              hw,
              snd_strerror (err));
      g_mutex_unlock(&rx->local_audio_mutex);
      return err;
    }

    t_print("%s: handle=%p\n", __FUNCTION__, rx->playback_handle);
    t_print("%s: trying format %s (%s)\n", __FUNCTION__, snd_pcm_format_name(formats[i]),
            snd_pcm_format_description(formats[i]));

    if ((err = snd_pcm_set_params (rx->playback_handle, formats[i], SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate,
                                   soft_resample, out_latency)) < 0) {
      t_print("%s: snd_pcm_set_params failed: %s\n", __FUNCTION__, snd_strerror(err));
      g_mutex_unlock(&rx->local_audio_mutex);
      audio_close_output(rx);
      continue;
    } else {
      t_print("%s: using format %s (%s)\n", __FUNCTION__, snd_pcm_format_name(formats[i]),
              snd_pcm_format_description(formats[i]));
      rx->local_audio_format = formats[i];
      break;
    }
  }

  if (i >= FORMATS) {
    t_print("%s: cannot find usable format\n", __FUNCTION__);
    return err;
  }

  rx->local_audio_buffer_offset = 0;

  switch (rx->local_audio_format) {
  case SND_PCM_FORMAT_S16_LE:
    t_print("%s: local_audio_buffer: size=%d sample=%ld\n", __FUNCTION__, out_buffer_size, sizeof(int16_t));
    rx->local_audio_buffer = g_new(int16_t, 2 * out_buffer_size);
    break;

  case SND_PCM_FORMAT_S32_LE:
    t_print("%s: local_audio_buffer: size=%d sample=%ld\n", __FUNCTION__, out_buffer_size, sizeof(int32_t));
    rx->local_audio_buffer = g_new(int32_t, 2 * out_buffer_size);
    break;

  case SND_PCM_FORMAT_FLOAT_LE:
    t_print("%s: local_audio_buffer: size=%d sample=%ld\n", __FUNCTION__, out_buffer_size, sizeof(float));
    rx->local_audio_buffer = g_new(float, 2 * out_buffer_size);
    break;

  default:
    t_print("%s: CATASTROPHIC ERROR: unknown sound format\n", __FUNCTION__);
    rx->local_audio_buffer = NULL;
    break;
  }

  t_print("%s: rx=%d audio_device=%d handle=%p buffer=%p size=%d\n", __FUNCTION__, rx->id, rx->audio_device,
          rx->playback_handle, rx->local_audio_buffer, out_buffer_size);
  g_mutex_unlock(&rx->local_audio_mutex);
  return 0;
}

int audio_open_input() {
  int err;
  unsigned int rate = 48000;
  unsigned int channels = 1;
  int soft_resample = 1;
  char hw[64];
  int i;

  if (!can_transmit) {
    return -1;
  }

  t_print("%s: %s\n", __FUNCTION__, transmitter->microphone_name);
  t_print("%s: mic_buffer_size=%d\n", __FUNCTION__, mic_buffer_size);
  i = 0;

  while (i < 63 && transmitter->microphone_name[i] != ' ') {
    hw[i] = transmitter->microphone_name[i];
    i++;
  }

  hw[i] = '\0';
  t_print("%s: hw=%s\n", __FUNCTION__, hw);

  for (i = 0; i < FORMATS; i++) {
    g_mutex_lock(&audio_mutex);

    if ((err = snd_pcm_open (&record_handle, hw, SND_PCM_STREAM_CAPTURE, SND_PCM_ASYNC)) < 0) {
      t_print("%s: cannot open audio device %s (%s)\n",
              __FUNCTION__,
              hw,
              snd_strerror (err));
      record_handle = NULL;
      g_mutex_unlock(&audio_mutex);
      return err;
    }

    t_print("%s: handle=%p\n", __FUNCTION__, record_handle);
    t_print("%s: trying format %s (%s)\n", __FUNCTION__, snd_pcm_format_name(formats[i]),
            snd_pcm_format_description(formats[i]));

    if ((err = snd_pcm_set_params (record_handle, formats[i], SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate, soft_resample,
                                   inp_latency)) < 0) {
      t_print("%s: snd_pcm_set_params failed: %s\n", __FUNCTION__, snd_strerror(err));
      g_mutex_unlock(&audio_mutex);
      audio_close_input();
      continue;
    } else {
      t_print("%s: using format %s (%s)\n", __FUNCTION__, snd_pcm_format_name(formats[i]),
              snd_pcm_format_description(formats[i]));
      record_audio_format = formats[i];
      break;
    }
  }

  if (i >= FORMATS) {
    t_print("%s: cannot find usable format\n", __FUNCTION__);
    g_mutex_unlock(&audio_mutex);
    audio_close_input();
    return err;
  }

  t_print("%s: format=%d\n", __FUNCTION__, record_audio_format);

  switch (record_audio_format) {
  case SND_PCM_FORMAT_S16_LE:
    t_print("%s: mic_buffer: size=%d channels=%d sample=%ld bytes\n", __FUNCTION__, mic_buffer_size, channels,
            sizeof(int16_t));
    mic_buffer = g_new(int16_t, mic_buffer_size);
    break;

  case SND_PCM_FORMAT_S32_LE:
    t_print("%s: mic_buffer: size=%d channels=%d sample=%ld bytes\n", __FUNCTION__, mic_buffer_size, channels,
            sizeof(int32_t));
    mic_buffer = g_new(int32_t, mic_buffer_size);
    break;

  case SND_PCM_FORMAT_FLOAT_LE:
    t_print("%s: mic_buffer: size=%d channels=%d sample=%ld bytes\n", __FUNCTION__, mic_buffer_size, channels,
            sizeof(float));
    mic_buffer = g_new(float, mic_buffer_size);
    break;

  default:
    t_print("%s: CATASTROPHIC ERROR: unknown sound format\n", __FUNCTION__);
    mic_buffer = NULL;
    break;
  }

  t_print("%s: allocating ring buffer\n", __FUNCTION__);
  mic_ring_buffer = (float *) g_new(float, MICRINGLEN);
  mic_ring_read_pt = mic_ring_write_pt = 0;

  if (mic_ring_buffer == NULL) {
    g_mutex_unlock(&audio_mutex);
    audio_close_input();
    return -1;
  }

  t_print("%s: creating mic_read_thread\n", __FUNCTION__);
  GError *error;
  mic_read_thread_id = g_thread_try_new("microphone", mic_read_thread, NULL, &error);

  if (!mic_read_thread_id ) {
    t_print("g_thread_new failed on mic_read_thread: %s\n", error->message);
    g_mutex_unlock(&audio_mutex);
    audio_close_input();
    return -1;
  }

  g_mutex_unlock(&audio_mutex);
  return 0;
}

void audio_close_output(RECEIVER *rx) {
  t_print("%s: rx=%d handle=%p buffer=%p\n", __FUNCTION__, rx->id, rx->playback_handle, rx->local_audio_buffer);
  g_mutex_lock(&rx->local_audio_mutex);

  if (rx->playback_handle != NULL) {
    snd_pcm_close (rx->playback_handle);
    rx->playback_handle = NULL;
  }

  if (rx->local_audio_buffer != NULL) {
    g_free(rx->local_audio_buffer);
    rx->local_audio_buffer = NULL;
  }

  g_mutex_unlock(&rx->local_audio_mutex);
}

void audio_close_input() {
  t_print("%s: enter\n", __FUNCTION__);
  running = FALSE;
  g_mutex_lock(&audio_mutex);

  if (mic_read_thread_id != NULL) {
    t_print("%s: wait for thread to complete\n", __FUNCTION__);
    g_thread_join(mic_read_thread_id);
    mic_read_thread_id = NULL;
  }

  if (record_handle != NULL) {
    t_print("%s: snd_pcm_close\n", __FUNCTION__);
    snd_pcm_close (record_handle);
    record_handle = NULL;
  }

  if (mic_buffer != NULL) {
    t_print("%s: free mic buffer\n", __FUNCTION__);
    g_free(mic_buffer);
    mic_buffer = NULL;
  }

  if (mic_ring_buffer != NULL) {
    g_free(mic_ring_buffer);
  }

  g_mutex_unlock(&audio_mutex);
}

//
// This is for writing a CW side tone.
// To keep sidetone latencies low, we keep the ALSA buffer
// at low filling, between cw_low_water and cw_high_water.
//
// Note that when sending the buffer, delay "jumps" by the buffer size
//

int cw_audio_write(RECEIVER *rx, float sample) {
  snd_pcm_sframes_t delay;
  g_mutex_lock(&rx->local_audio_mutex);

  if (rx->playback_handle != NULL && rx->local_audio_buffer != NULL) {
    static int count = 0;

    if (snd_pcm_delay(rx->playback_handle, &delay) == 0) {
      if (delay > out_cw_border) {
        //
        // This happens when we come here for the first time after a
        // RX/TX transision. Rewind until we are at target filling for CW
        //
        snd_pcm_rewind(rx->playback_handle, delay - cw_mid_water);
        count = 0;
      }
    }

    //
    // Put sample into buffer
    //
    switch (rx->local_audio_format) {
    case SND_PCM_FORMAT_S16_LE: {
      int16_t *short_buffer = (int16_t *)rx->local_audio_buffer;
      short_buffer[rx->local_audio_buffer_offset * 2] = (int16_t)(sample * 32767.0F);
      short_buffer[(rx->local_audio_buffer_offset * 2) + 1] = (int16_t)(sample * 32767.0F);
    }
    break;

    case SND_PCM_FORMAT_S32_LE: {
      int32_t *long_buffer = (int32_t *)rx->local_audio_buffer;
      long_buffer[rx->local_audio_buffer_offset * 2] = (int32_t)(sample * 4294967295.0F);
      long_buffer[(rx->local_audio_buffer_offset * 2) + 1] = (int32_t)(sample * 4294967295.0F);
    }
    break;

    case SND_PCM_FORMAT_FLOAT_LE: {
      float *float_buffer = (float *)rx->local_audio_buffer;
      float_buffer[rx->local_audio_buffer_offset * 2] = sample;
      float_buffer[(rx->local_audio_buffer_offset * 2) + 1] =  sample;
    }
    break;

    default:
      t_print("%s: CATASTROPHIC ERROR: unknown sound format\n", __FUNCTION__);
      break;
    }

    rx->local_audio_buffer_offset++;

    if (sample != 0.0) { count = 0; } // count upwards during silence

    if (++count >= 16) {
      count = 0;

      //
      // We have just seen 16 zero samples, so this is the right place
      // to adjust the buffer filling.
      // If buffer gets too full   ==> skip the sample
      // If buffer gets too empty ==> insert zero sample
      //
      if (snd_pcm_delay(rx->playback_handle, &delay) == 0) {
        if (delay > cw_high_water && rx->local_audio_buffer_offset > 0) {
          // delete the last sample
          rx->local_audio_buffer_offset--;
        }

        if ((delay < cw_low_water) && (rx->local_audio_buffer_offset < out_buffer_size)) {
          // insert another zero sample
          switch (rx->local_audio_format) {
          case SND_PCM_FORMAT_S16_LE: {
            int16_t *short_buffer = (int16_t *)rx->local_audio_buffer;
            short_buffer[rx->local_audio_buffer_offset * 2] = 0;
            short_buffer[(rx->local_audio_buffer_offset * 2) + 1] = 0;
          }
          break;

          case SND_PCM_FORMAT_S32_LE: {
            int32_t* long_buffer = (int32_t *)rx->local_audio_buffer;
            long_buffer[rx->local_audio_buffer_offset * 2] = 0;
            long_buffer[(rx->local_audio_buffer_offset * 2) + 1] = 0;
          }
          break;

          case SND_PCM_FORMAT_FLOAT_LE: {
            float *float_buffer = (float *)rx->local_audio_buffer;
            float_buffer[rx->local_audio_buffer_offset * 2] = 0.0;
            float_buffer[(rx->local_audio_buffer_offset * 2) + 1] = 0.0;
          }
          break;

          default:
            t_print("%s: CATASTROPHIC ERROR: unknown sound format\n", __FUNCTION__);
            break;
          }

          rx->local_audio_buffer_offset++;
        }
      }
    }

    if (rx->local_audio_buffer_offset >= out_buffer_size) {
      long rc;

      if ((rc = snd_pcm_writei (rx->playback_handle, rx->local_audio_buffer, out_buffer_size)) != out_buffer_size) {
        if (rc < 0) {
          switch (rc) {
          case -EPIPE:
            if ((rc = snd_pcm_prepare (rx->playback_handle)) < 0) {
              t_print("%s: cannot prepare audio interface for use %ld (%s)\n", __FUNCTION__, rc, snd_strerror (rc));
              rx->local_audio_buffer_offset = 0;
              g_mutex_unlock(&rx->local_audio_mutex);
              return rc;
            }

            break;

          default:
            t_print("%s:  write error: %s\n", __FUNCTION__, snd_strerror(rc));
            break;
          }
        } else {
          t_print("%s: short write lost=%d\n", __FUNCTION__, out_buffer_size - (int) rc);
        }
      }

      rx->local_audio_buffer_offset = 0;
    }
  }

  g_mutex_unlock(&rx->local_audio_mutex);
  return 0;
}

//
// if rx == active_receiver and while transmitting, DO NOTHING
// since cw_audio_write may be active
//

int audio_write(RECEIVER *rx, float left_sample, float right_sample) {
  snd_pcm_sframes_t delay;
  int txmode = vfo_get_tx_mode();

  //
  // We have to stop the stream here if a CW side tone may occur.
  // This might cause underflows, but we cannot use audio_write
  // and cw_audio_write simultaneously on the same device.
  // Instead, the side tone version will take over.
  // If *not* doing CW, the stream continues because we might wish
  // to listen to this rx while transmitting.
  //
  if (rx == active_receiver && radio_is_transmitting() && (txmode == modeCWU || txmode == modeCWL)) {
    return 0;
  }

  // lock AFTER checking the "quick return" condition but BEFORE checking the pointers
  g_mutex_lock(&rx->local_audio_mutex);

  if (rx->playback_handle != NULL && rx->local_audio_buffer != NULL) {
    switch (rx->local_audio_format) {
    case SND_PCM_FORMAT_S16_LE: {
      int16_t *short_buffer = (int16_t *)rx->local_audio_buffer;
      short_buffer[rx->local_audio_buffer_offset * 2] = (int16_t)(left_sample * 32767.0F);
      short_buffer[(rx->local_audio_buffer_offset * 2) + 1] = (int16_t)(right_sample * 32767.0F);
    }
    break;

    case SND_PCM_FORMAT_S32_LE: {
      int32_t *long_buffer = (int32_t *)rx->local_audio_buffer;
      long_buffer[rx->local_audio_buffer_offset * 2] = (int32_t)(left_sample * 4294967295.0F);
      long_buffer[(rx->local_audio_buffer_offset * 2) + 1] = (int32_t)(right_sample * 4294967295.0F);
    }
    break;

    case SND_PCM_FORMAT_FLOAT_LE: {
      float *float_buffer = (float *)rx->local_audio_buffer;
      float_buffer[rx->local_audio_buffer_offset * 2] = left_sample;
      float_buffer[(rx->local_audio_buffer_offset * 2) + 1] = right_sample;
    }
    break;

    default:
      t_print("%s: CATASTROPHIC ERROR: unknown sound format\n", __FUNCTION__);
      break;
    }

    rx->local_audio_buffer_offset++;

    if (rx->local_audio_buffer_offset >= out_buffer_size) {
      if (snd_pcm_delay(rx->playback_handle, &delay) == 0) {
        if (delay < out_cw_border) {
          //
          // upon first occurence, or after a TX/RX transition, the buffer
          // is empty (delay == 0), if we just come from CW TXing, delay is below
          // out_cw_border as well.
          // ACTION: fill buffer completely with silence to start output, then
          //         rewind until half-filling. Just filling by half does nothing,
          //         ALSA just does not start playing until the buffer is nearly full.
          //
          void *silence = NULL;
          size_t len;
          int num = (out_buflen - delay);

          switch (rx->local_audio_format) {
          case SND_PCM_FORMAT_S16_LE:
            silence = g_new(int16_t, 2 * num);
            len = 2 * num * sizeof(int16_t);
            break;

          case SND_PCM_FORMAT_S32_LE:
            silence = g_new(int32_t, 2 * num);
            len = 2 * num * sizeof(int32_t);
            break;

          case SND_PCM_FORMAT_FLOAT_LE:
            silence = g_new(float, 2 * num);
            len = 2 * num * sizeof(float);
            break;

          default:
            t_print("%s: CATASTROPHIC ERROR: unknown sound format\n", __FUNCTION__);
            silence = NULL;
            len = 0;
            break;
          }

          if (silence) {
            memset(silence, 0, len);
            snd_pcm_writei (rx->playback_handle, silence, num);
            snd_pcm_rewind (rx->playback_handle, out_buflen / 2);
            g_free(silence);
          }
        }
      }

      long rc;

      if ((rc = snd_pcm_writei (rx->playback_handle, rx->local_audio_buffer, out_buffer_size)) != out_buffer_size) {
        if (rc < 0) {
          switch (rc) {
          case -EPIPE:
            if ((rc = snd_pcm_prepare (rx->playback_handle)) < 0) {
              t_print("%s: cannot prepare audio interface for use %ld (%s)\n", __FUNCTION__, rc, snd_strerror (rc));
              rx->local_audio_buffer_offset = 0;
              g_mutex_unlock(&rx->local_audio_mutex);
              return rc;
            }

            break;

          default:
            t_print("%s:  write error: %s\n", __FUNCTION__, snd_strerror(rc));
            break;
          }
        } else {
          t_print("%s: short write lost=%d\n", __FUNCTION__, out_buffer_size - (int) rc);
        }
      }

      rx->local_audio_buffer_offset = 0;
    }
  }

  g_mutex_unlock(&rx->local_audio_mutex);
  return 0;
}

static void *mic_read_thread(gpointer arg) {
  int rc;
  const float *float_buffer;
  const int32_t *long_buffer;
  const int16_t *short_buffer;
  float sample;
  int i;
  t_print("%s: mic_buffer_size=%d\n", __FUNCTION__, mic_buffer_size);
  t_print("%s: snd_pcm_start\n", __FUNCTION__);

  if ((rc = snd_pcm_start (record_handle)) < 0) {
    t_print("%s: cannot start audio interface for use (%s)\n",
            __FUNCTION__,
            snd_strerror (rc));
    return NULL;
  }

  running = TRUE;

  while (running) {
    if ((rc = snd_pcm_readi (record_handle, mic_buffer, mic_buffer_size)) != mic_buffer_size) {
      if (running) {
        if (rc < 0) {
          t_print("%s: read from audio interface failed (%s)\n",
                  __FUNCTION__,
                  snd_strerror (rc));
          //running=FALSE;
        } else {
          t_print("%s: read %d\n", __FUNCTION__, rc);
        }
      }
    } else {
      int newpt;

      // process the mic input
      for (i = 0; i < mic_buffer_size; i++) {
        switch (record_audio_format) {
        case SND_PCM_FORMAT_S16_LE:
          short_buffer = (int16_t *)mic_buffer;
          sample = (float)short_buffer[i] / 32767.0f;
          break;

        case SND_PCM_FORMAT_S32_LE:
          long_buffer = (int32_t *)mic_buffer;
          sample = (float)long_buffer[i] / 4294967295.0f;
          break;

        case SND_PCM_FORMAT_FLOAT_LE:
          float_buffer = (float *)mic_buffer;
          sample = float_buffer[i];
          break;

        default:
          t_print("%s: CATASTROPHIC ERROR: unknown sound format\n", __FUNCTION__);
          sample = 0.0;
          break;
        }

        //
        // put sample into ring buffer
        // Note check on the mic ring buffer is not necessary
        // since audio_close_input() waits for this thread to
        // complete.
        //
        if (mic_ring_buffer != NULL) {
          // do not increase mic_ring_write_pt *here* since it must
          // not assume an illegal value at any time
          newpt = mic_ring_write_pt + 1;

          if (newpt == MICRINGLEN) { newpt = 0; }

          if (newpt != mic_ring_read_pt) {
            // buffer space available, do the write
            mic_ring_buffer[mic_ring_write_pt] = sample;
            // atomic update of mic_ring_write_pt
            mic_ring_write_pt = newpt;
          }
        }
      }
    }
  }

  t_print("%s: exiting\n", __FUNCTION__);
  return NULL;
}

//
// Utility function for retrieving mic samples
// from ring buffer
//
float audio_get_next_mic_sample() {
  float sample;
  g_mutex_lock(&audio_mutex);

  if ((mic_ring_buffer == NULL) || (mic_ring_read_pt == mic_ring_write_pt)) {
    // no buffer, or nothing in buffer: insert silence
    sample = 0.0;
  } else {
    int newpt = mic_ring_read_pt + 1;

    if (newpt == MICRINGLEN) { newpt = 0; }

    sample = mic_ring_buffer[mic_ring_read_pt];
    // atomic update of read pointer
    mic_ring_read_pt = newpt;
  }

  g_mutex_unlock(&audio_mutex);
  return sample;
}

void audio_get_cards() {
  snd_ctl_card_info_t *info;
  snd_pcm_info_t *pcminfo;
  snd_ctl_card_info_alloca(&info);
  snd_pcm_info_alloca(&pcminfo);
  int i;
  char *device_id;
  int card = -1;
  t_print("%s\n", __FUNCTION__);
  g_mutex_init(&audio_mutex);
  n_input_devices = 0;
  n_output_devices = 0;
  snd_ctl_card_info_alloca(&info);
  snd_pcm_info_alloca(&pcminfo);

  while (snd_card_next(&card) >= 0 && card >= 0) {
    snd_ctl_t *handle;
    char name[20];
    snprintf(name, sizeof(name), "hw:%d", card);

    if (snd_ctl_open(&handle, name, 0) < 0) {
      continue;
    }

    if (snd_ctl_card_info(handle, info) < 0) {
      snd_ctl_close(handle);
      continue;
    }

    int dev = -1;

    while (snd_ctl_pcm_next_device(handle, &dev) >= 0 && dev >= 0) {
      snd_pcm_info_set_device(pcminfo, dev);
      snd_pcm_info_set_subdevice(pcminfo, 0);
      // input devices
      snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_CAPTURE);

      if (snd_ctl_pcm_info(handle, pcminfo) == 0) {
        device_id = g_new(char, 128);
        snprintf(device_id, 128, "plughw:%d,%d %s", card, dev, snd_ctl_card_info_get_name(info));

        if (n_input_devices < MAX_AUDIO_DEVICES) {
          // the two allocated strings will never be free'd
          input_devices[n_input_devices].name = g_strdup(device_id);
          input_devices[n_input_devices].description = g_strdup(device_id);
          input_devices[n_input_devices].index = 0; // not used
          n_input_devices++;
          t_print("input_device: %s\n", device_id);
        }

        g_free(device_id);
      }

      // ouput devices
      snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_PLAYBACK);

      if (snd_ctl_pcm_info(handle, pcminfo) == 0) {
        device_id = g_new(char, 128);
        snprintf(device_id, 128, "plughw:%d,%d %s", card, dev, snd_ctl_card_info_get_name(info));

        if (n_output_devices < MAX_AUDIO_DEVICES) {
          // the two allocated strings will never be free'd
          output_devices[n_output_devices].name = g_strdup(device_id);
          output_devices[n_output_devices].description = g_strdup(device_id);
          input_devices[n_output_devices].index = 0; // not used
          n_output_devices++;
          t_print("output_device: %s\n", device_id);
        }

        g_free(device_id);
      }
    }

    snd_ctl_close(handle);
  }

  // look for dmix and dsnoop
  void **hints, **n;
  char *name, *descr, *io;

  if (snd_device_name_hint(-1, "pcm", &hints) < 0) {
    return;
  }

  n = hints;

  while (*n != NULL) {
    name = snd_device_name_get_hint(*n, "NAME");
    descr = snd_device_name_get_hint(*n, "DESC");
    io = snd_device_name_get_hint(*n, "IOID");

    if (strncmp("dmix:", name, 5) == 0) {
      if (n_output_devices < MAX_AUDIO_DEVICES) {
        output_devices[n_output_devices].name = g_strdup(name);
        output_devices[n_output_devices].description = g_strdup(descr);

        for (i = 0; i < strlen(descr); i++) {
          if (output_devices[n_output_devices].description[i] == '\n') {
            output_devices[n_output_devices].description[i] = '\0';
            break;
          }
        }

        input_devices[n_output_devices].index = 0; // not used
        n_output_devices++;
        t_print("output_device: name=%s descr=%s\n", name, descr);
      }

#ifdef INCLUDE_SNOOP
    } else if (strncmp("dsnoop:", name, 6) == 0) {
      if (n_input_devices < MAX_AUDIO_DEVICES) {
        input_devices[n_input_devices].name = g_strdup(name);

        for (i = 0; i < strlen(descr); i++) {
          if (input_devices[n_input_devices].description[i] == '\n') {
            input_devices[n_input_devices].description[i] = '\0';
            break;
          }
        }

        input_devices[n_input_devices].index = 0; // not used
        n_input_devices++;
        t_print("input_device: name=%s descr=%s\n", name, descr);
      }

#endif
    }

    //
    //  For these three items, use free() instead of g_free(),
    //  since these have been allocated by ALSA via
    //  snd_device_name_get_hint()
    //
    if (name != NULL) {
      free(name);
    }

    if (descr != NULL) {
      free(descr);
    }

    if (io != NULL) {
      free(io);
    }

    n++;
  }

  snd_device_name_free_hint(hints);
}
