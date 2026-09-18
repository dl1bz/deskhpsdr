/* Copyright (C)
* 2024-2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
* SPDX-License-Identifier: GPL-3.0-or-later
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
*/

#ifdef MINIAUDIO

#include <gtk/gtk.h>

#include <stdlib.h>
#include <string.h>

#include "miniaudio.h"

#include "audio_backend.h"
#include "audio.h"
#include "message.h"
#include "tci_audio.h"

#define MINIAUDIO_SAMPLE_RATE 48000
#define MINIAUDIO_PLAYBACK_PERIOD_FRAMES 256
#define MINIAUDIO_CAPTURE_PERIOD_FRAMES 64
#define MINIAUDIO_TCI_MONITOR_CHUNK 1024

typedef enum {
  MINIAUDIO_STREAM_OUTPUT,
  MINIAUDIO_STREAM_INPUT,
  MINIAUDIO_STREAM_TCI_MONITOR
} MINIAUDIO_STREAM_TYPE;

typedef struct {
  ma_device device;
  MINIAUDIO_STREAM_TYPE type;
  RECEIVER *rx;
  int channels;
} MINIAUDIO_STREAM;

static ma_context miniaudio_context;
static int miniaudio_context_ready = 0;
static GMutex miniaudio_context_mutex;
static gsize miniaudio_context_mutex_ready = 0;

#if defined(__linux__)
  char miniaudio_backend[16] = "auto";
#endif

static void miniaudio_init_mutex(void) {
  if (g_once_init_enter(&miniaudio_context_mutex_ready)) {
    g_mutex_init(&miniaudio_context_mutex);
    g_once_init_leave(&miniaudio_context_mutex_ready, 1);
  }
}

static int miniaudio_ensure_context(void) {
  miniaudio_init_mutex();
  g_mutex_lock(&miniaudio_context_mutex);
  if (!miniaudio_context_ready) {
    ma_result result;
#if defined(__linux__)
    ma_backend backend;
    if (g_ascii_strcasecmp(miniaudio_backend, "pulse") == 0) {
      backend = ma_backend_pulseaudio;
      t_print("%s: requested backend=PulseAudio\n", __func__);
      result = ma_context_init(&backend, 1, NULL, &miniaudio_context);
    } else if (g_ascii_strcasecmp(miniaudio_backend, "alsa") == 0) {
      backend = ma_backend_alsa;
      t_print("%s: requested backend=ALSA\n", __func__);
      result = ma_context_init(&backend, 1, NULL, &miniaudio_context);
    } else {
      t_print("%s: requested backend=auto\n", __func__);
      result = ma_context_init(NULL, 0, NULL, &miniaudio_context);
    }
#else
    result = ma_context_init(NULL, 0, NULL, &miniaudio_context);
#endif
    if (result != MA_SUCCESS) {
      t_print("%s: ma_context_init failed: %s\n", __func__, ma_result_description(result));
      g_mutex_unlock(&miniaudio_context_mutex);
      return -1;
    }
    miniaudio_context_ready = 1;
    t_print("%s: backend=%s\n", __func__, ma_get_backend_name(miniaudio_context.backend));
  }
  g_mutex_unlock(&miniaudio_context_mutex);
  return 0;
}

static int miniaudio_find_device(const char *device_name, ma_device_type type,
                                 ma_device_id *device_id, int *native_channels) {
  ma_device_info *playback_infos = NULL;
  ma_uint32 playback_count = 0;
  ma_device_info *capture_infos = NULL;
  ma_uint32 capture_count = 0;
  ma_result result;
  if (device_name == NULL || device_name[0] == '\0' || device_id == NULL) {
    return -1;
  }
  if (miniaudio_ensure_context() != 0) {
    return -1;
  }
  result = ma_context_get_devices(&miniaudio_context,
                                  &playback_infos, &playback_count,
                                  &capture_infos, &capture_count);
  if (result != MA_SUCCESS) {
    t_print("%s: ma_context_get_devices failed: %s\n", __func__, ma_result_description(result));
    return -1;
  }
  ma_device_info *infos = type == ma_device_type_capture ? capture_infos : playback_infos;
  ma_uint32 count = type == ma_device_type_capture ? capture_count : playback_count;
  for (ma_uint32 i = 0; i < count; i++) {
    if (strcmp(infos[i].name, device_name) == 0) {
      *device_id = infos[i].id;
      if (native_channels != NULL) {
        *native_channels = 0;
        ma_device_info detail;
        memset(&detail, 0, sizeof(detail));
        if (ma_context_get_device_info(&miniaudio_context, type, &infos[i].id, &detail) == MA_SUCCESS) {
          for (ma_uint32 f = 0; f < detail.nativeDataFormatCount; f++) {
            if ((int)detail.nativeDataFormats[f].channels > *native_channels) {
              *native_channels = (int)detail.nativeDataFormats[f].channels;
            }
          }
        }
      }
      return 0;
    }
  }
  return -1;
}

static void miniaudio_data_cb(ma_device *device, void *output, const void *input,
                              ma_uint32 frames) {
  MINIAUDIO_STREAM *stream = (MINIAUDIO_STREAM *)device->pUserData;
  if (stream == NULL) {
    return;
  }
  if (stream->type == MINIAUDIO_STREAM_OUTPUT) {
    if (output != NULL) {
      audio_render_local_output(stream->rx, (float *)output, frames, stream->channels);
    }
    return;
  }
  if (stream->type == MINIAUDIO_STREAM_INPUT) {
    if (input != NULL) {
      audio_process_local_mic_input((const float *)input, frames);
    }
    return;
  }
  if (stream->type == MINIAUDIO_STREAM_TCI_MONITOR && output != NULL) {
    float *out = (float *)output;
    ma_uint32 remaining = frames;
    while (remaining > 0) {
      ma_uint32 chunk = remaining > MINIAUDIO_TCI_MONITOR_CHUNK ?
                        MINIAUDIO_TCI_MONITOR_CHUNK : remaining;
      float samples[MINIAUDIO_TCI_MONITOR_CHUNK * TCI_AUDIO_CHANNELS];
      guint got = tci_audio_monitor_read(samples, (guint)chunk);
      for (ma_uint32 i = 0; i < chunk; i++) {
        float left = i < got ? samples[i * TCI_AUDIO_CHANNELS] : 0.0f;
        float right = i < got ? samples[i * TCI_AUDIO_CHANNELS + 1] : 0.0f;
        if (stream->channels == 2) {
          *out++ = left;
          *out++ = right;
        } else {
          *out++ = 0.5f * (left + right);
        }
      }
      remaining -= chunk;
    }
  }
}

static void *miniaudio_open_playback(const char *device_name, RECEIVER *rx,
                                     MINIAUDIO_STREAM_TYPE type, int *channels) {
  ma_device_id device_id;
  int native_channels = 0;
  if (channels == NULL ||
      miniaudio_find_device(device_name, ma_device_type_playback,
                            &device_id, &native_channels) != 0) {
    t_print("%s: miniaudio playback device not found: %s\n", __func__,
            device_name != NULL ? device_name : "(null)");
    return NULL;
  }
  MINIAUDIO_STREAM *stream = calloc(1, sizeof(*stream));
  if (stream == NULL) {
    return NULL;
  }
  stream->type = type;
  stream->rx = rx;
  stream->channels = native_channels >= 2 ? 2 : 1;
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.pDeviceID = &device_id;
  config.playback.format = ma_format_f32;
  config.playback.channels = (ma_uint32)stream->channels;
  config.sampleRate = MINIAUDIO_SAMPLE_RATE;
  config.periodSizeInFrames = MINIAUDIO_PLAYBACK_PERIOD_FRAMES;
  config.dataCallback = miniaudio_data_cb;
  config.pUserData = stream;
  ma_result result = ma_device_init(&miniaudio_context, &config, &stream->device);
  if (result != MA_SUCCESS) {
    t_print("%s: ma_device_init failed device=%s: %s\n", __func__, device_name,
            ma_result_description(result));
    free(stream);
    return NULL;
  }
  result = ma_device_start(&stream->device);
  if (result != MA_SUCCESS) {
    t_print("%s: ma_device_start failed device=%s: %s\n", __func__, device_name,
            ma_result_description(result));
    ma_device_uninit(&stream->device);
    free(stream);
    return NULL;
  }
  *channels = stream->channels;
  t_print("%s: opened miniaudio playback device=%s channels=%d samplerate=%u period=%u\n",
          __func__, device_name, stream->channels,
          (unsigned int)stream->device.sampleRate,
          (unsigned int)stream->device.playback.internalPeriodSizeInFrames);
  return stream;
}

void *audio_backend_output_open(RECEIVER *rx, const char *device_name, int *channels) {
  return miniaudio_open_playback(device_name, rx, MINIAUDIO_STREAM_OUTPUT, channels);
}

void audio_backend_output_close(void *handle) {
  MINIAUDIO_STREAM *stream = (MINIAUDIO_STREAM *)handle;
  if (stream == NULL) {
    return;
  }
  ma_device_uninit(&stream->device);
  free(stream);
}

void *audio_backend_input_open(const char *device_name) {
  ma_device_id device_id;
  if (miniaudio_find_device(device_name, ma_device_type_capture, &device_id, NULL) != 0) {
    t_print("%s: miniaudio capture device not found: %s\n", __func__,
            device_name != NULL ? device_name : "(null)");
    return NULL;
  }
  MINIAUDIO_STREAM *stream = calloc(1, sizeof(*stream));
  if (stream == NULL) {
    return NULL;
  }
  stream->type = MINIAUDIO_STREAM_INPUT;
  stream->channels = 1;
  ma_device_config config = ma_device_config_init(ma_device_type_capture);
  config.capture.pDeviceID = &device_id;
  config.capture.format = ma_format_f32;
  config.capture.channels = 1;
  config.sampleRate = MINIAUDIO_SAMPLE_RATE;
  config.periodSizeInFrames = MINIAUDIO_CAPTURE_PERIOD_FRAMES;
  config.dataCallback = miniaudio_data_cb;
  config.pUserData = stream;
  ma_result result = ma_device_init(&miniaudio_context, &config, &stream->device);
  if (result != MA_SUCCESS) {
    t_print("%s: ma_device_init failed device=%s: %s\n", __func__, device_name,
            ma_result_description(result));
    free(stream);
    return NULL;
  }
  result = ma_device_start(&stream->device);
  if (result != MA_SUCCESS) {
    t_print("%s: ma_device_start failed device=%s: %s\n", __func__, device_name,
            ma_result_description(result));
    ma_device_uninit(&stream->device);
    free(stream);
    return NULL;
  }
  t_print("%s: opened miniaudio capture device=%s channels=1 samplerate=%u period=%u\n",
          __func__, device_name, (unsigned int)stream->device.sampleRate,
          (unsigned int)stream->device.capture.internalPeriodSizeInFrames);
  return stream;
}

void audio_backend_input_close(void *handle) {
  MINIAUDIO_STREAM *stream = (MINIAUDIO_STREAM *)handle;
  if (stream == NULL) {
    return;
  }
  ma_device_uninit(&stream->device);
  free(stream);
}

void *audio_backend_tci_monitor_open(const char *device_name, int *channels) {
  return miniaudio_open_playback(device_name, NULL, MINIAUDIO_STREAM_TCI_MONITOR, channels);
}

void audio_backend_tci_monitor_close(void *handle) {
  audio_backend_output_close(handle);
}

int audio_backend_output_is_alive(void *handle) {
  MINIAUDIO_STREAM *stream = (MINIAUDIO_STREAM *)handle;
  return stream != NULL && ma_device_get_state(&stream->device) == ma_device_state_started;
}

int audio_backend_input_is_alive(void *handle) {
  return audio_backend_output_is_alive(handle);
}

int audio_backend_tci_monitor_is_alive(void *handle) {
  return audio_backend_output_is_alive(handle);
}

int audio_backend_get_cards(void) {
  ma_device_info *playback_infos = NULL;
  ma_uint32 playback_count = 0;
  ma_device_info *capture_infos = NULL;
  ma_uint32 capture_count = 0;
  if (miniaudio_ensure_context() != 0) {
    return -1;
  }
  ma_result result = ma_context_get_devices(&miniaudio_context,
    &playback_infos, &playback_count,
    &capture_infos, &capture_count);
  if (result != MA_SUCCESS) {
    t_print("%s: ma_context_get_devices failed: %s\n", __func__, ma_result_description(result));
    return -1;
  }
  g_mutex_lock(&audio_mutex);
  for (ma_uint32 i = 0; i < capture_count && n_input_devices < MAX_AUDIO_DEVICES; i++) {
    AUDIO_DEVICE *entry = &input_devices[n_input_devices];
    entry->name = g_strdup(capture_infos[i].name);
    entry->description = g_strdup(capture_infos[i].name);
    entry->index = n_input_devices;
    n_input_devices++;
  }
  for (ma_uint32 i = 0; i < playback_count && n_output_devices < MAX_AUDIO_DEVICES; i++) {
    AUDIO_DEVICE *entry = &output_devices[n_output_devices];
    entry->name = g_strdup(playback_infos[i].name);
    entry->description = g_strdup(playback_infos[i].name);
    entry->index = n_output_devices;
    n_output_devices++;
  }
  g_mutex_unlock(&audio_mutex);
  t_print("%s: miniaudio devices input=%d output=%d\n", __func__,
          n_input_devices, n_output_devices);
  return 0;
}

#endif /* MINIAUDIO */
