/* Copyright (C)
* 2019 - Christoph van Wüllen, DL1YCF
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

#ifdef PORTAUDIO
//
// Alternate "audio" module using PORTAUDIO instead of ALSA
// (e.g. on MacOS)
//
// If PortAudio is NOT used, this file is empty, and audio.c
// is used instead.
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
#include <portaudio.h>

#ifdef __APPLE__
  #include <pa_mac_core.h>
#endif
#ifdef NATIVE_COREAUDIO_OUTPUT
  #include "coreaudio.h"
#endif

#include "radio.h"
#include "receiver.h"
#include "mode.h"
#include "audio.h"
#include "message.h"
#include "vfo.h"
#include "tci_audio.h"

static PaStream *record_handle = NULL;
#ifdef NATIVE_COREAUDIO_INPUT
static void *coreaudio_input_handle = NULL;
#endif

int n_input_devices;
AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
int n_output_devices;
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

GMutex audio_mutex;
static volatile gint audio_xrun_count = 0;
static volatile gint output_ring_primed[8] = { 0 };
static volatile gint output_ring_starved[8] = { 0 };

guint64 audio_get_xrun_count(void) {
  return (guint64) g_atomic_int_get(&audio_xrun_count);
}


//
// We now use callback functions to provide the "headphone" audio data,
// and therefore can control the latency.
// RX audio samples are put into a ring buffer and "fetched" therefreom
// by the portaudio "headphone" callback.
//
// We choose a ring buffer of 9600 (stereo) samples that is kept about half-full
// during RX (latency: 0.1 sec) which should be more than enough.
// If the buffer falls below 1800, half a buffer length of silence is
// inserted. This usually only happens after TX/RX transitions
//
// RX audio and CW sidetone use separate ring buffers. On an RX/TX transition
// the RX ring is no longer discarded: its WDSP-slewed tail is allowed to drain
// naturally while the sidetone starts from its own low-latency ring.
// The sidetone filling is kept close to an explicit low-latency target to
// reduce underrun risk and avoid larger latency swings.
// Of course, a small portaudio audio buffer size (128 sample) helps
// keeping the latency small. The CW buffer is kept around CW_LAT_TARGET
// with a narrow correction window to reduce occasional underruns/clicks.
//
// Experiments indicate that we can indeed keep the ring buffer about half filling
// during RX and quite empty during CW-TX.
//
//

#define MY_AUDIO_BUFFER_SIZE  128
#define TCI_MONITOR_FRAMES_PER_BUFFER 1024
#define TCI_MONITOR_UNDERRUN_LOG_INTERVAL 100
#define MY_RING_BUFFER_SIZE  9600
#define MY_RING_LOW_WATER     512
#define MY_RING_HIGH_WATER   9000
#define CW_LAT_LOW            224
#define CW_LAT_TARGET         256
#define CW_LAT_HIGH           288

//
// Ring buffer for "local microphone" samples stored locally here.
// NOTE: lead large buffer for some "loopback" devices which produce
//       samples in large chunks if fed from digimode programs.
//
static float *mic_ring_buffer = NULL;
static atomic_int mic_ring_outpt;
static atomic_int mic_ring_inpt;

static void local_mic_ring_reset(int silence_frames) {
  if (mic_ring_buffer == NULL) {
    return;
  }

  if (silence_frames < 0) {
    silence_frames = 0;
  }
  if (silence_frames >= MY_RING_BUFFER_SIZE) {
    silence_frames = MY_RING_BUFFER_SIZE - 1;
  }

  if (silence_frames > 0) {
    bzero(mic_ring_buffer, (size_t)silence_frames * sizeof(float));
  }

  atomic_store_explicit(&mic_ring_outpt, 0, memory_order_release);
  atomic_store_explicit(&mic_ring_inpt, silence_frames, memory_order_release);
}

static inline void local_mic_ring_push(float sample) {
  int inpt = atomic_load_explicit(&mic_ring_inpt, memory_order_relaxed);
  int outpt = atomic_load_explicit(&mic_ring_outpt, memory_order_acquire);
  int newpt = inpt + 1;

  if (newpt == MY_RING_BUFFER_SIZE) {
    newpt = 0;
  }
  if (newpt == outpt) {
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

  outpt = atomic_load_explicit(&mic_ring_outpt, memory_order_relaxed);
  inpt = atomic_load_explicit(&mic_ring_inpt, memory_order_acquire);
  if (outpt == inpt) {
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

static PaStream *tci_monitor_handle = NULL;
#ifdef NATIVE_COREAUDIO_TCI_MONITOR
static void *coreaudio_tci_monitor_handle = NULL;
#endif
static GMutex tci_monitor_mutex;
static int tci_monitor_channels = 2;
static unsigned int tci_monitor_underruns = 0;

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
#ifndef NATIVE_COREAUDIO_ENUMERATION
  /*
   * PortAudio owns device enumeration on non-macOS builds.
   */
  Pa_Terminate();
#endif
}

//
// AUDIO_GET_CARDS
//
// This inits PortAudio and looks for suitable input and output channels
//
void audio_get_cards(void) {
  static gsize mutex_inited = 0;
  if (g_once_init_enter(&mutex_inited)) {
    g_mutex_init(&audio_mutex);
    g_mutex_init(&tci_monitor_mutex);
    g_once_init_leave(&mutex_inited, 1);
  }

#ifdef NATIVE_COREAUDIO_ENUMERATION
  t_print("%s: native CoreAudio call audio_get_cards\n", __func__);
  if (coreaudio_get_cards() != 0) {
    t_print("%s: native CoreAudio device enumeration failed\n", __func__);
  }
#else
  t_print("%s: PORTAUDIO call audio_get_cards\n", __func__);
  int numDevices;
  PaStreamParameters inputParameters, outputParameters;
  PaError err;
  err = Pa_Initialize();
  if (err != paNoError) {
    t_print("%s: init error %s\n", __func__, Pa_GetErrorText(err));
    return;
  }
  numDevices = Pa_GetDeviceCount();
  if (numDevices < 0) { return; }
  g_mutex_lock(&audio_mutex);
  n_input_devices = 0;
  n_output_devices = 0;
  for (int  i = 0; i < numDevices; i++) {
    const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(i);
    inputParameters.device = i;
    inputParameters.channelCount = 1;  // Microphone samples are mono
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = 0; /* ignored by Pa_IsFormatSupported() */
    inputParameters.hostApiSpecificStreamInfo = NULL;
    if (Pa_IsFormatSupported(&inputParameters, NULL, 48000.0) == paFormatIsSupported) {
      if (n_input_devices < MAX_AUDIO_DEVICES) {
        //
        // probably not necessary with portaudio, but to be on the safe side,
        // we copy the device name to local storage. This is referenced both
        // by the name and description element.
        //
        input_devices[n_input_devices].name = g_strdup(deviceInfo->name);
        input_devices[n_input_devices].description = g_strdup(deviceInfo->name);
        input_devices[n_input_devices].index = i;
        n_input_devices++;
      }
      t_print("%s: INPUT DEVICE, No=%d, Name=%s\n", __func__, i, deviceInfo->name);
    }
    outputParameters.device = i;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = 0; /* ignored by Pa_IsFormatSupported() */
    outputParameters.hostApiSpecificStreamInfo = NULL;
    outputParameters.channelCount = 2;  // prefer stereo
    if (Pa_IsFormatSupported(NULL, &outputParameters, 48000.0) == paFormatIsSupported) {
      if (n_output_devices < MAX_AUDIO_DEVICES) {
        output_devices[n_output_devices].name = g_strdup(deviceInfo->name);
        output_devices[n_output_devices].description = g_strdup(deviceInfo->name);
        output_devices[n_output_devices].index = i;
        n_output_devices++;
      }
      t_print("%s: OUTPUT DEVICE, No=%d, Name=%s (stereo)\n", __func__, i, deviceInfo->name);
    } else {
      outputParameters.channelCount = 1;  // mono fallback
      if (Pa_IsFormatSupported(NULL, &outputParameters, 48000.0) == paFormatIsSupported) {
        if (n_output_devices < MAX_AUDIO_DEVICES) {
          output_devices[n_output_devices].name = g_strdup(deviceInfo->name);
          output_devices[n_output_devices].description = g_strdup(deviceInfo->name);
          output_devices[n_output_devices].index = i;
          n_output_devices++;
        }
        t_print("%s: OUTPUT DEVICE, No=%d, Name=%s (mono)\n", __func__, i, deviceInfo->name);
      }
    }
  }
  g_mutex_unlock(&audio_mutex);
#endif
}


//
// AUDIO_OPEN_INPUT
//
// open a PA stream that connects to the TX microphone
// The PA callback function then sends the data to the transmitter
//

int pa_mic_cb(const void *, void *, unsigned long, const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *);
int pa_out_cb(const void *, void *, unsigned long, const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *);
int pa_tci_monitor_cb(const void *, void *, unsigned long, const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags,
                      void *);

int audio_open_input(void) {
#ifdef NATIVE_COREAUDIO_INPUT
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
  atomic_init(&mic_ring_outpt, 0);
  atomic_init(&mic_ring_inpt, 0);
  g_mutex_unlock(&audio_mutex);

  void *handle = coreaudio_input_open(transmitter->microphone_name);
  if (handle == NULL) {
    g_mutex_lock(&audio_mutex);
    g_free(mic_ring_buffer);
    mic_ring_buffer = NULL;
    atomic_store_explicit(&mic_ring_outpt, 0, memory_order_relaxed);
    atomic_store_explicit(&mic_ring_inpt, 0, memory_order_relaxed);
    g_mutex_unlock(&audio_mutex);
    return -1;
  }

  g_mutex_lock(&audio_mutex);
  coreaudio_input_handle = handle;
  g_mutex_unlock(&audio_mutex);

  t_print("%s: native CoreAudio input name=%s\n", __func__, transmitter->microphone_name);
  return 0;
#else

  t_print("%s: PORTAUDIO call audio_open_input\n", __func__);
  PaError err;
  PaStreamParameters inputParameters;
  int i;
  int padev;
  if (!can_transmit) {
    return -1;
  }
  //
  // Look up device name and determine device ID
  //
  padev = -1;
  for (i = 0; i < n_input_devices; i++) {
    if (!strcmp(transmitter->microphone_name, input_devices[i].name)) {
      padev = input_devices[i].index;
      break;
    }
  }
  t_print("%s: name=%s PADEV=%d\n", __func__, transmitter->microphone_name, padev);
  //
  // Device name possibly came from props file and device is no longer there
  //
  if (padev < 0) {
    return -1;
  }
  g_mutex_lock(&audio_mutex);
  bzero(&inputParameters, sizeof(inputParameters));    //not necessary if you are filling in all the fields
  inputParameters.channelCount = 1;   // MONO
  inputParameters.device = padev;
  inputParameters.hostApiSpecificStreamInfo = NULL;
  inputParameters.sampleFormat = paFloat32;
  const PaDeviceInfo *info = Pa_GetDeviceInfo(padev);
  if (info == NULL) {
    g_mutex_unlock(&audio_mutex);
    return -1;
  }
  inputParameters.suggestedLatency = info->defaultLowInputLatency;
  t_print("%s: input device=%s channels=%d sampleFormat=paFloat32 latency=%f samplerate=48000\n",
          __func__, info->name, inputParameters.channelCount, inputParameters.suggestedLatency);
#ifdef __APPLE__
  static PaMacCoreStreamInfo macCoreInfo;
  macCoreInfo.size = sizeof(PaMacCoreStreamInfo);
  macCoreInfo.hostApiType = paCoreAudio;
  macCoreInfo.version = 0x01;
  macCoreInfo.flags = paMacCoreChangeDeviceParameters;
  inputParameters.hostApiSpecificStreamInfo = &macCoreInfo;
#else
  inputParameters.hostApiSpecificStreamInfo = NULL; //See you specific host's API docs for info on using this field
#endif
  err = Pa_OpenStream(&record_handle, &inputParameters, NULL, 48000.0, MY_AUDIO_BUFFER_SIZE,
                      paNoFlag, pa_mic_cb, NULL);
  if (err != paNoError) {
    t_print("%s: open stream error %s\n", __func__, Pa_GetErrorText(err));
    record_handle = NULL;
    g_mutex_unlock(&audio_mutex);
    return -1;
  }
  mic_ring_buffer = (float *) g_new(float, MY_RING_BUFFER_SIZE);
  atomic_init(&mic_ring_outpt, 0);
  atomic_init(&mic_ring_inpt, 0);
  if (mic_ring_buffer == NULL) {
    Pa_CloseStream(record_handle);
    record_handle = NULL;
    t_print("%s: alloc buffer failed.\n", __func__);
    g_mutex_unlock(&audio_mutex);
    return -1;
  }
  err = Pa_StartStream(record_handle);
  if (err != paNoError) {
    t_print("%s: start stream error %s\n", __func__, Pa_GetErrorText(err));
    Pa_CloseStream(record_handle);
    record_handle = NULL;
    g_free(mic_ring_buffer);
    mic_ring_buffer = NULL;
    g_mutex_unlock(&audio_mutex);
    return -1;
  }
  //
  // Finished!
  //
  g_mutex_unlock(&audio_mutex);
  return 0;
#endif
}


//
// PortAudio call-back function for local TCI audio monitor output
//
int pa_tci_monitor_cb(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userdata) {
  float *out = (float *) outputBuffer;
  float buffer[TCI_MONITOR_FRAMES_PER_BUFFER * TCI_AUDIO_CHANNELS];
  guint frames;
  unsigned long requested_frames = framesPerBuffer;
  if (out == NULL) {
    t_print("%s: bogus audio buffer in callback\n", __func__);
    return paContinue;
  }
  if (requested_frames > TCI_MONITOR_FRAMES_PER_BUFFER) {
    requested_frames = TCI_MONITOR_FRAMES_PER_BUFFER;
  }
  frames = tci_audio_monitor_read(buffer, (guint) requested_frames);
  if (frames < requested_frames) {
    tci_monitor_underruns++;
    if ((tci_monitor_underruns % TCI_MONITOR_UNDERRUN_LOG_INTERVAL) == 1) {
      t_print("%s: underrun frames=%u requested=%lu count=%u\n", __func__, frames, requested_frames, tci_monitor_underruns);
    }
  }
  for (unsigned int i = 0; i < framesPerBuffer; i++) {
    float left = 0.0f;
    float right = 0.0f;
    if (i < frames) {
      left = buffer[(i * TCI_AUDIO_CHANNELS)];
      right = buffer[(i * TCI_AUDIO_CHANNELS) + 1];
    }
    if (tci_monitor_channels == 2) {
      *out++ = left;
      *out++ = right;
    } else {
      *out++ = 0.5f * (left + right);
    }
  }
  return paContinue;
}

int audio_open_tci_monitor(const char *audio_name) {
#ifdef NATIVE_COREAUDIO_TCI_MONITOR
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
  tci_monitor_channels = channels;
  tci_monitor_underruns = 0;
  g_mutex_unlock(&tci_monitor_mutex);

  t_print("%s: opened native CoreAudio TCI monitor name=%s channels=%d\n",
          __func__, audio_name, channels);
  return 0;
#else

  PaError err;
  PaStreamParameters outputParameters;
  int padev = -1;
  if (audio_name == NULL) { return -1; }
  for (int i = 0; i < n_output_devices; i++) {
    if (!strcmp(audio_name, output_devices[i].name)) {
      padev = output_devices[i].index;
      break;
    }
  }
  t_print("%s: name=%s PADEV=%d\n", __func__, audio_name, padev);
  if (padev < 0) { return -1; }
  g_mutex_lock(&tci_monitor_mutex);
  if (tci_monitor_handle != NULL) {
    g_mutex_unlock(&tci_monitor_mutex);
    return 0;
  }
  bzero(&outputParameters, sizeof(outputParameters));
  const PaDeviceInfo *info = Pa_GetDeviceInfo(padev);
  if (info == NULL) {
    g_mutex_unlock(&tci_monitor_mutex);
    return -1;
  }
  outputParameters.device = padev;
  outputParameters.hostApiSpecificStreamInfo = NULL;
  outputParameters.sampleFormat = paFloat32;
  outputParameters.suggestedLatency = info->defaultHighOutputLatency;
  t_print("%s: output device=%s sampleFormat=paFloat32 latency=%f samplerate=%d\n",
          __func__, info->name, outputParameters.suggestedLatency, TCI_AUDIO_SAMPLE_RATE);
#ifdef __APPLE__
  static PaMacCoreStreamInfo macCoreInfo;
  macCoreInfo.size = sizeof(PaMacCoreStreamInfo);
  macCoreInfo.hostApiType = paCoreAudio;
  macCoreInfo.version = 0x01;
  macCoreInfo.flags = paMacCoreChangeDeviceParameters;
  outputParameters.hostApiSpecificStreamInfo = &macCoreInfo;
#else
  outputParameters.hostApiSpecificStreamInfo = NULL;
#endif
  outputParameters.channelCount = 2;
  tci_monitor_channels = 2;
  if (Pa_IsFormatSupported(NULL, &outputParameters, TCI_AUDIO_SAMPLE_RATE) != paFormatIsSupported) {
    outputParameters.channelCount = 1;
    tci_monitor_channels = 1;
  }
  tci_monitor_underruns = 0;
  err = Pa_OpenStream(&tci_monitor_handle, NULL, &outputParameters, TCI_AUDIO_SAMPLE_RATE, TCI_MONITOR_FRAMES_PER_BUFFER,
                      paNoFlag, pa_tci_monitor_cb, NULL);
  if (err != paNoError) {
    t_print("%s: open stream error %s\n", __func__, Pa_GetErrorText(err));
    tci_monitor_handle = NULL;
    g_mutex_unlock(&tci_monitor_mutex);
    return -1;
  }
  tci_audio_monitor_set_active(1);
  err = Pa_StartStream(tci_monitor_handle);
  if (err != paNoError) {
    t_print("%s: start stream error %s\n", __func__, Pa_GetErrorText(err));
    Pa_CloseStream(tci_monitor_handle);
    tci_monitor_handle = NULL;
    tci_audio_monitor_set_active(0);
    g_mutex_unlock(&tci_monitor_mutex);
    return -1;
  }
  t_print("%s: opened TCI monitor with %d channel(s), fpb=%d\n", __func__, tci_monitor_channels,
          TCI_MONITOR_FRAMES_PER_BUFFER);
  g_mutex_unlock(&tci_monitor_mutex);
  return 0;
#endif
}


void audio_close_tci_monitor(void) {
#ifdef NATIVE_COREAUDIO_TCI_MONITOR
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
#else

  PaStream *s = NULL;
  g_mutex_lock(&tci_monitor_mutex);
  s = tci_monitor_handle;
  tci_monitor_handle = NULL;
  tci_audio_monitor_set_active(0);
  g_mutex_unlock(&tci_monitor_mutex);
  if (s != NULL) {
    PaError err = Pa_StopStream(s);
    if (err != paNoError) {
      t_print("%s: stop stream error %s\n", __func__, Pa_GetErrorText(err));
    }
    err = Pa_CloseStream(s);
    if (err != paNoError) {
      t_print("%s: close stream error %s\n", __func__, Pa_GetErrorText(err));
    }
  }
#endif
}


//
// PortAudio call-back function for Audio output
//
int pa_out_cb(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
              const PaStreamCallbackTimeInfo* timeInfo,
              PaStreamCallbackFlags statusFlags,
              void *userdata) {
  (void) inputBuffer;
  (void) timeInfo;

  float *out = (float *) outputBuffer;
  RECEIVER *rx = (RECEIVER *) userdata;
  gboolean valid_rx_id = rx->id >= 0 && rx->id < (int)(sizeof(output_ring_primed) / sizeof(output_ring_primed[0]));
  gboolean ring_was_primed = valid_rx_id && g_atomic_int_get(&output_ring_primed[rx->id]);

  if ((statusFlags & paOutputUnderflow) && ring_was_primed && rx->local_audio) {
    g_atomic_int_inc(&audio_xrun_count);
  }
  if (out == NULL) {
    t_print("%s: bogus audio buffer in callback\n", __func__);
    return paContinue;
  }

  /*
   * Keep the existing PortAudio synchronization unchanged. The shared
   * renderer itself is lock-free so CoreAudio can call it from its RT thread.
   */
  g_mutex_lock(&rx->local_audio_mutex);
  audio_render_local_output(rx, out, (unsigned int) framesPerBuffer, rx->local_audio_channels);
  g_mutex_unlock(&rx->local_audio_mutex);

  return paContinue;
}

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
// PortAudio call-back function for Audio input
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
      local_mic_ring_reset(960);
    }
  } else {
    last_was_tx = 1;
  }

  for (unsigned int i = 0; i < frames; i++) {
    local_mic_ring_push(samples[i]);
  }
}

int pa_mic_cb(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
              const PaStreamCallbackTimeInfo* timeInfo,
              PaStreamCallbackFlags statusFlags,
              void *userdata) {
  const float *in = (const float *) inputBuffer;
  (void) outputBuffer;
  (void) timeInfo;
  (void) statusFlags;
  (void) userdata;

  if (in == NULL) {
    // This should not happen, so we do not send silence etc.
    t_print("%s: bogus audio buffer in callback\n", __func__);
    return paContinue;
  }

  audio_process_local_mic_input(in, (unsigned int) framesPerBuffer);
  return paContinue;
}

//
// Utility function for retrieving mic samples
// from ring buffer
//
float audio_get_next_mic_sample(void) {
  return local_mic_ring_pop();
}

//
// AUDIO_OPEN_OUTPUT
//
// open a PA stream for data from one of the RX
//
int audio_open_output(RECEIVER *rx) {
#ifdef NATIVE_COREAUDIO_OUTPUT
  if (rx == NULL) {
    return -1;
  }

  /*
   * Allocate and initialize rings before the CoreAudio unit is started.
   * Publish the backend handle only after AudioOutputUnitStart() succeeds.
   */
  g_mutex_lock(&rx->local_audio_mutex);
  rx->playstream = NULL;
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
#else

  PaError err;
  PaStreamParameters outputParameters;
  int padev;
  int i;
  //
  // Look up device name and determine device ID
  //
  padev = -1;
  for (i = 0; i < n_output_devices; i++) {
    if (!strcmp(rx->audio_name, output_devices[i].name)) {
      padev = output_devices[i].index;
      break;
    }
  }
  t_print("%s: name=%s PADEV=%d\n", __func__, rx->audio_name, padev);
  //
  // Device name possibly came from props file and device is no longer there
  //
  if (padev < 0) {
    return -1;
  }
  g_mutex_lock(&rx->local_audio_mutex);
  bzero(&outputParameters, sizeof(outputParameters));    //not necessary if you are filling in all the fields
  outputParameters.device = padev;
  outputParameters.hostApiSpecificStreamInfo = NULL;
  const PaDeviceInfo *info = Pa_GetDeviceInfo(padev);
  if (info == NULL) {
    g_mutex_unlock(&rx->local_audio_mutex);
    return -1;
  }
  outputParameters.sampleFormat = paFloat32;
  outputParameters.suggestedLatency = info->defaultLowOutputLatency;
  t_print("%s: output device=%s sampleFormat=paFloat32 latency=%f samplerate=48000\n",
          __func__, info->name, outputParameters.suggestedLatency);
#ifdef __APPLE__
  static PaMacCoreStreamInfo macCoreInfo;
  macCoreInfo.size = sizeof(PaMacCoreStreamInfo);
  macCoreInfo.hostApiType = paCoreAudio;
  macCoreInfo.version = 0x01;
  macCoreInfo.flags = paMacCoreChangeDeviceParameters;
  outputParameters.hostApiSpecificStreamInfo = &macCoreInfo;
#else
  outputParameters.hostApiSpecificStreamInfo = NULL; //See you specific host's API docs for info on using this field
#endif
  outputParameters.channelCount = 2;   // prefer stereo
  rx->local_audio_channels = 2;
  if (Pa_IsFormatSupported(NULL, &outputParameters, 48000.0) != paFormatIsSupported) {
    outputParameters.channelCount = 1;
    rx->local_audio_channels = 1;
  }
  err = Pa_OpenStream(& (rx->playstream), NULL, &outputParameters, 48000.0, MY_AUDIO_BUFFER_SIZE,
                      paNoFlag, pa_out_cb, rx);
  t_print("%s: opened output with %d channel(s)\n", __func__, rx->local_audio_channels);
  if (err != paNoError) {
    t_print("%s: open stream error %s\n", __func__, Pa_GetErrorText(err));
    rx->playstream = NULL;
    g_mutex_unlock(&rx->local_audio_mutex);
    return -1;
  }
  //
  // This is now a ring buffer much larger than a single audio buffer
  //
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
  }
  if (rx->local_audio_buffer == NULL || rx->sidetone_buffer == NULL) {
    t_print("%s: allocate buffer failed\n", __func__);
    Pa_CloseStream(rx->playstream);
    rx->playstream = NULL;
    g_free(rx->local_audio_buffer);
    g_free(rx->sidetone_buffer);
    rx->local_audio_buffer = NULL;
    rx->sidetone_buffer = NULL;
    g_mutex_unlock(&rx->local_audio_mutex);
    return -1;
  }
  err = Pa_StartStream(rx->playstream);
  if (err != paNoError) {
    t_print("%s: error starting stream:%s\n", __func__, Pa_GetErrorText(err));
    Pa_CloseStream(rx->playstream);
    rx->playstream = NULL;
    g_free(rx->local_audio_buffer);
    g_free(rx->sidetone_buffer);
    rx->local_audio_buffer = NULL;
    rx->sidetone_buffer = NULL;
    g_mutex_unlock(&rx->local_audio_mutex);
    return -1;
  }
  //
  // Finished!
  //
  g_mutex_unlock(&rx->local_audio_mutex);
  return 0;
#endif
}


//
// AUDIO_CLOSE_INPUT
//
// close a TX microphone stream
//
void audio_close_input(void) {
#ifdef NATIVE_COREAUDIO_INPUT
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
  g_mutex_unlock(&audio_mutex);
#else

  t_print("%s: PORTAUDIO call audio_close_input\n", __func__);
  t_print("%s: micname=%s\n", __func__, transmitter->microphone_name);
  PaStream *s = NULL;
  g_mutex_lock(&audio_mutex);
  s = record_handle;
  record_handle = NULL;
  g_mutex_unlock(&audio_mutex);
  if (s != NULL) {
    PaError err = Pa_StopStream(s);
    if (err != paNoError) {
      t_print("%s: error stopping stream: %s\n", __func__, Pa_GetErrorText(err));
    }
    err = Pa_CloseStream(s);
    if (err != paNoError) {
      t_print("%s: %s\n", __func__, Pa_GetErrorText(err));
    }
  }
  g_mutex_lock(&audio_mutex);
  if (mic_ring_buffer != NULL) {
    g_free(mic_ring_buffer);
    mic_ring_buffer = NULL;
  }
  atomic_store_explicit(&mic_ring_outpt, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_ring_inpt, 0, memory_order_relaxed);
  g_mutex_unlock(&audio_mutex);
#endif
}


//
// AUDIO_CLOSE_OUTPUT
//
// shut down the stream connected with audio from one of the RX
//
void audio_close_output(RECEIVER *rx) {
#ifdef NATIVE_COREAUDIO_OUTPUT
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
#else

  t_print("%s: device=%s\n", __func__, rx->audio_name);
  PaStream *s = NULL;
  g_mutex_lock(&rx->local_audio_mutex);
  s = rx->playstream;
  rx->playstream = NULL;
  g_mutex_unlock(&rx->local_audio_mutex);
  if (s != NULL) {
    PaError err = Pa_StopStream(s);
    if (err != paNoError) {
      t_print("%s: stop stream error %s\n", __func__, Pa_GetErrorText(err));
    }
    err = Pa_CloseStream(s);
    if (err != paNoError) {
      t_print("%s: close stream error %s\n", __func__, Pa_GetErrorText(err));
    }
  }
  g_mutex_lock(&rx->local_audio_mutex);
  if (rx->id >= 0 && rx->id < (int)(sizeof(output_ring_primed) / sizeof(output_ring_primed[0]))) {
    g_atomic_int_set(&output_ring_primed[rx->id], 0);
    g_atomic_int_set(&output_ring_starved[rx->id], 0);
  }
  if (rx->local_audio_buffer != NULL) {
    g_free(rx->local_audio_buffer);
    rx->local_audio_buffer = NULL;
  }
  if (rx->sidetone_buffer != NULL) {
    g_free(rx->sidetone_buffer);
    rx->sidetone_buffer = NULL;
  }
  atomic_store_explicit(&rx->local_audio_buffer_inpt, 0, memory_order_relaxed);
  atomic_store_explicit(&rx->local_audio_buffer_outpt, 0, memory_order_relaxed);
  atomic_store_explicit(&rx->sidetone_buffer_inpt, 0, memory_order_relaxed);
  atomic_store_explicit(&rx->sidetone_buffer_outpt, 0, memory_order_relaxed);
  rx->local_audio_cw_active = 0;
  g_mutex_unlock(&rx->local_audio_mutex);
#endif
}


//
// AUDIO_WRITE
//
// send RX audio data to a PA output stream
// we have to store the data such that the PA callback function
// can access it.
//
// Note that the check on radio_is_transmitting() takes care that "blocking"
// by the mutex can only occur in the moment of a RX/TX transition if
// both audio_write() and cw_audio_write() get a "go".
//
// So mutex locking/unlocking should only cost few CPU cycles in
// normal operation.
//
int audio_write(RECEIVER *rx, float left, float right) {
  int txmode = vfo_get_tx_mode();
  float *buffer = rx->local_audio_buffer;
  if (rx == active_receiver && radio_is_transmitting() && (txmode == modeCWU || txmode == modeCWL)) {
    // Stop producing new RX audio during CW TX. The existing RX tail drains naturally.
    return 0;
  }
  g_mutex_lock(&rx->local_audio_mutex);
  rx->local_audio_cw_active = 0;
#ifdef NATIVE_COREAUDIO_OUTPUT
  if (rx->coreaudio_output_handle != NULL && buffer != NULL) {
#else
  if (rx->playstream != NULL && buffer != NULL) {
#endif
    int inpt = atomic_load_explicit(&rx->local_audio_buffer_inpt, memory_order_relaxed);
    int outpt = atomic_load_explicit(&rx->local_audio_buffer_outpt, memory_order_acquire);
    int avail = inpt - outpt;
    if (avail < 0) { avail += MY_RING_BUFFER_SIZE; }
    if (avail < MY_RING_LOW_WATER) {
      int oldpt = inpt;
      for (int i = 0; i < MY_RING_BUFFER_SIZE / 2 - avail; i++) {
        buffer[2 * oldpt] = 0.0f;
        buffer[2 * oldpt + 1] = 0.0f;
        oldpt++;
        if (oldpt >= MY_RING_BUFFER_SIZE) { oldpt = 0; }
      }
      atomic_store_explicit(&rx->local_audio_buffer_inpt, oldpt, memory_order_release);
      inpt = oldpt;
    }
    if (avail > MY_RING_HIGH_WATER) {
      int oldpt = inpt - avail + MY_RING_BUFFER_SIZE / 2;
      if (oldpt < 0) { oldpt += MY_RING_BUFFER_SIZE; }
      atomic_store_explicit(&rx->local_audio_buffer_inpt, oldpt, memory_order_release);
      inpt = oldpt;
      t_print("%s: buffer was nearly full, deleted audio\n", __func__);
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
    }
  }
  g_mutex_unlock(&rx->local_audio_mutex);
  return 0;
}

//
// During CW, between the elements the side tone contains "true" silence.
// We detect a sequence of 16 subsequent zero samples, and insert or delete
// a zero sample depending on the buffer water mark:
// If there are more than two portaudio buffers available, delete one sample,
// if it drops down to less than one portaudio buffer, insert one sample
//
// Thus we have an active latency management.
//
int cw_audio_write(RECEIVER *rx, float sample) {
  g_mutex_lock(&rx->local_audio_mutex);
#ifdef NATIVE_COREAUDIO_OUTPUT
  if (rx->coreaudio_output_handle != NULL && rx->sidetone_buffer != NULL) {
#else
  if (rx->playstream != NULL && rx->sidetone_buffer != NULL) {
#endif
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
      rx->local_audio_cw_active = 1;
    }
    if (sample != 0.0f) { count = 0; }
    if (++count >= 16) {
      count = 0;
      if (avail > CW_LAT_HIGH) { adjust = 2; }
      if (avail < CW_LAT_LOW) { adjust = 1; }
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
  }
  g_mutex_unlock(&rx->local_audio_mutex);
  return 0;
}

#endif
