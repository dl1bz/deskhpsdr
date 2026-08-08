/* Copyright (C)
* 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
* Native CoreAudio output backend.
*
* Native CoreAudio/AUHAL device, input, output and TCI-monitor backend.
*/

#ifdef COREAUDIO

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "coreaudio.h"
#include "message.h"
#ifdef COREAUDIO
#include "tci_audio.h"
#endif

#define COREAUDIO_SAMPLE_RATE 48000.0

#ifdef COREAUDIO
typedef struct {
  AudioComponentInstance unit;
  AudioDeviceID device;
  RECEIVER *rx;
  int channels;
} COREAUDIO_OUTPUT;
#endif

#ifdef COREAUDIO
typedef struct {
  AudioComponentInstance unit;
  AudioDeviceID device;
  int channels;
} COREAUDIO_TCI_MONITOR;
#endif

#ifdef COREAUDIO
typedef struct {
  AudioComponentInstance unit;
  AudioDeviceID device;
  float *buffer;
  UInt32 max_frames;
} COREAUDIO_INPUT;
#endif

static int coreaudio_device_channels(AudioDeviceID device, AudioObjectPropertyScope scope) {
  AudioObjectPropertyAddress address = {
    kAudioDevicePropertyStreamConfiguration,
    scope,
    kAudioObjectPropertyElementMain
  };
  UInt32 size = 0;

  if (AudioObjectGetPropertyDataSize(device, &address, 0, NULL, &size) != noErr || size == 0) {
    return 0;
  }

  AudioBufferList *list = malloc(size);
  if (list == NULL) {
    return 0;
  }

  int channels = 0;
  if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, list) == noErr) {
    for (UInt32 i = 0; i < list->mNumberBuffers; i++) {
      channels += (int) list->mBuffers[i].mNumberChannels;
    }
  }

  free(list);
  return channels;
}

static int coreaudio_device_name(AudioDeviceID device, char *name, size_t name_size) {
  AudioObjectPropertyAddress address = {
    kAudioObjectPropertyName,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };
  CFStringRef cfname = NULL;
  UInt32 size = sizeof(cfname);

  if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &cfname) != noErr || cfname == NULL) {
    return 0;
  }

  Boolean ok = CFStringGetCString(cfname, name, (CFIndex) name_size, kCFStringEncodingUTF8);
  CFRelease(cfname);
  return ok ? 1 : 0;
}

#ifdef COREAUDIO

static void coreaudio_free_device_list(AUDIO_DEVICE *devices, int count) {
  for (int i = 0; i < count; i++) {
    g_free(devices[i].name);
    g_free(devices[i].description);
    devices[i].name = NULL;
    devices[i].description = NULL;
    devices[i].index = -1;
  }
}

int coreaudio_get_cards(void) {
  AudioObjectPropertyAddress address = {
    kAudioHardwarePropertyDevices,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };
  UInt32 size = 0;

  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, NULL, &size) != noErr ||
      size == 0) {
    t_print("%s: cannot enumerate CoreAudio devices\n", __func__);
    return -1;
  }

  AudioDeviceID *devices = malloc(size);
  if (devices == NULL) {
    return -1;
  }

  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, devices) != noErr) {
    free(devices);
    t_print("%s: cannot read CoreAudio device list\n", __func__);
    return -1;
  }

  UInt32 count = size / sizeof(AudioDeviceID);

  /*
   * Replace the current list atomically under audio_mutex. The native
   * CoreAudio open paths use the device name and do not depend on the
   * legacy backend index. We store AudioDeviceID in index
   * for diagnostics and future native-only use.
   */
  g_mutex_lock(&audio_mutex);
  coreaudio_free_device_list(input_devices, n_input_devices);
  coreaudio_free_device_list(output_devices, n_output_devices);
  n_input_devices = 0;
  n_output_devices = 0;
  memset(input_devices, 0, sizeof(input_devices));
  memset(output_devices, 0, sizeof(output_devices));

  for (UInt32 i = 0; i < count; i++) {
    char name[512];
    if (!coreaudio_device_name(devices[i], name, sizeof(name))) {
      continue;
    }

    int input_channels = coreaudio_device_channels(devices[i], kAudioDevicePropertyScopeInput);
    int output_channels = coreaudio_device_channels(devices[i], kAudioDevicePropertyScopeOutput);

    if (input_channels > 0 && n_input_devices < MAX_AUDIO_DEVICES) {
      input_devices[n_input_devices].name = g_strdup(name);
      input_devices[n_input_devices].description = g_strdup(name);
      input_devices[n_input_devices].index = (int) devices[i];
      t_print("%s: INPUT DEVICE, ID=%u, Name=%s, channels=%d\n",
              __func__, (unsigned int) devices[i], name, input_channels);
      n_input_devices++;
    }

    if (output_channels > 0 && n_output_devices < MAX_AUDIO_DEVICES) {
      output_devices[n_output_devices].name = g_strdup(name);
      output_devices[n_output_devices].description = g_strdup(name);
      output_devices[n_output_devices].index = (int) devices[i];
      t_print("%s: OUTPUT DEVICE, ID=%u, Name=%s, channels=%d\n",
              __func__, (unsigned int) devices[i], name, output_channels);
      n_output_devices++;
    }
  }

  g_mutex_unlock(&audio_mutex);
  free(devices);

  t_print("%s: native CoreAudio devices: inputs=%d outputs=%d\n",
          __func__, n_input_devices, n_output_devices);
  return 0;
}

#endif /* COREAUDIO */

#ifdef COREAUDIO
static AudioDeviceID coreaudio_find_output_device(const char *device_name) {
  AudioObjectPropertyAddress address = {
    kAudioHardwarePropertyDevices,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };
  UInt32 size = 0;

  if (device_name == NULL || device_name[0] == '\0') {
    return kAudioObjectUnknown;
  }
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, NULL, &size) != noErr || size == 0) {
    return kAudioObjectUnknown;
  }

  UInt32 count = size / sizeof(AudioDeviceID);
  AudioDeviceID *devices = malloc(size);
  if (devices == NULL) {
    return kAudioObjectUnknown;
  }
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, devices) != noErr) {
    free(devices);
    return kAudioObjectUnknown;
  }

  AudioDeviceID found = kAudioObjectUnknown;
  for (UInt32 i = 0; i < count; i++) {
    if (coreaudio_device_channels(devices[i], kAudioDevicePropertyScopeOutput) <= 0) {
      continue;
    }
    char name[512];
    if (coreaudio_device_name(devices[i], name, sizeof(name)) && strcmp(name, device_name) == 0) {
      found = devices[i];
      break;
    }
  }

  free(devices);
  return found;
}
#endif

#ifdef COREAUDIO
static OSStatus coreaudio_render_cb(void *refcon,
                                    AudioUnitRenderActionFlags *flags,
                                    const AudioTimeStamp *timestamp,
                                    UInt32 bus,
                                    UInt32 frames,
                                    AudioBufferList *io_data) {
  (void) flags;
  (void) timestamp;
  (void) bus;

  COREAUDIO_OUTPUT *output = (COREAUDIO_OUTPUT *) refcon;

  /*
   * Real-time callback contract:
   * no mutexes, no allocation, no logging, no GTK calls.
   */
  if (output == NULL || output->rx == NULL || io_data == NULL ||
      io_data->mNumberBuffers != 1 || io_data->mBuffers[0].mData == NULL) {
    if (io_data != NULL) {
      for (UInt32 i = 0; i < io_data->mNumberBuffers; i++) {
        if (io_data->mBuffers[i].mData != NULL) {
          memset(io_data->mBuffers[i].mData, 0, io_data->mBuffers[i].mDataByteSize);
        }
      }
    }
    return noErr;
  }

  float *buffer = (float *) io_data->mBuffers[0].mData;
  audio_render_local_output(output->rx, buffer, frames, output->channels);
  io_data->mBuffers[0].mDataByteSize = frames * (UInt32) output->channels * sizeof(float);
  return noErr;
}

void *coreaudio_output_open(RECEIVER *rx, const char *device_name, int *channels) {
  if (rx == NULL || device_name == NULL || channels == NULL) {
    return NULL;
  }

  AudioDeviceID device = coreaudio_find_output_device(device_name);
  if (device == kAudioObjectUnknown) {
    t_print("%s: CoreAudio output device not found: %s\n", __func__, device_name);
    return NULL;
  }

  int device_channels = coreaudio_device_channels(device, kAudioDevicePropertyScopeOutput);
  int client_channels = device_channels >= 2 ? 2 : 1;
  if (client_channels < 1) {
    t_print("%s: CoreAudio device has no output channels: %s\n", __func__, device_name);
    return NULL;
  }

  COREAUDIO_OUTPUT *output = calloc(1, sizeof(*output));
  if (output == NULL) {
    return NULL;
  }

  AudioComponentDescription desc = {
    .componentType = kAudioUnitType_Output,
    .componentSubType = kAudioUnitSubType_HALOutput,
    .componentManufacturer = kAudioUnitManufacturer_Apple,
    .componentFlags = 0,
    .componentFlagsMask = 0
  };

  AudioComponent component = AudioComponentFindNext(NULL, &desc);
  if (component == NULL ||
      AudioComponentInstanceNew(component, &output->unit) != noErr ||
      output->unit == NULL) {
    t_print("%s: cannot create AUHAL output unit\n", __func__);
    free(output);
    return NULL;
  }

  UInt32 enable = 1;
  UInt32 disable = 0;
  OSStatus status;

  status = AudioUnitSetProperty(output->unit, kAudioOutputUnitProperty_EnableIO,
                                kAudioUnitScope_Output, 0, &enable, sizeof(enable));
  if (status != noErr) {
    t_print("%s: EnableIO(output) failed status=%d\n", __func__, (int) status);
    goto fail;
  }

  status = AudioUnitSetProperty(output->unit, kAudioOutputUnitProperty_EnableIO,
                                kAudioUnitScope_Input, 1, &disable, sizeof(disable));
  if (status != noErr) {
    t_print("%s: DisableIO(input) failed status=%d\n", __func__, (int) status);
    goto fail;
  }

  status = AudioUnitSetProperty(output->unit, kAudioOutputUnitProperty_CurrentDevice,
                                kAudioUnitScope_Global, 0, &device, sizeof(device));
  if (status != noErr) {
    t_print("%s: CurrentDevice failed status=%d\n", __func__, (int) status);
    goto fail;
  }

  AudioStreamBasicDescription format;
  memset(&format, 0, sizeof(format));
  format.mSampleRate = COREAUDIO_SAMPLE_RATE;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
  format.mBytesPerPacket = (UInt32) client_channels * sizeof(float);
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = (UInt32) client_channels * sizeof(float);
  format.mChannelsPerFrame = (UInt32) client_channels;
  format.mBitsPerChannel = 8 * sizeof(float);

  status = AudioUnitSetProperty(output->unit, kAudioUnitProperty_StreamFormat,
                                kAudioUnitScope_Input, 0, &format, sizeof(format));
  if (status != noErr) {
    t_print("%s: StreamFormat failed status=%d device=%s channels=%d\n",
            __func__, (int) status, device_name, client_channels);
    goto fail;
  }

  output->device = device;
  output->rx = rx;
  output->channels = client_channels;

  AURenderCallbackStruct callback = {
    .inputProc = coreaudio_render_cb,
    .inputProcRefCon = output
  };
  status = AudioUnitSetProperty(output->unit, kAudioUnitProperty_SetRenderCallback,
                                kAudioUnitScope_Input, 0, &callback, sizeof(callback));
  if (status != noErr) {
    t_print("%s: SetRenderCallback failed status=%d\n", __func__, (int) status);
    goto fail;
  }

  status = AudioUnitInitialize(output->unit);
  if (status != noErr) {
    t_print("%s: AudioUnitInitialize failed status=%d\n", __func__, (int) status);
    goto fail;
  }

  status = AudioOutputUnitStart(output->unit);
  if (status != noErr) {
    t_print("%s: AudioOutputUnitStart failed status=%d\n", __func__, (int) status);
    AudioUnitUninitialize(output->unit);
    goto fail;
  }

  *channels = client_channels;
  t_print("%s: opened native CoreAudio output device=%s id=%u channels=%d samplerate=48000\n",
          __func__, device_name, (unsigned int) device, client_channels);
  return output;

fail:
  if (output->unit != NULL) {
    AudioComponentInstanceDispose(output->unit);
  }
  free(output);
  return NULL;
}

void coreaudio_output_close(void *handle) {
  COREAUDIO_OUTPUT *output = (COREAUDIO_OUTPUT *) handle;
  if (output == NULL) {
    return;
  }

  /*
   * AudioOutputUnitStop() is completed before the receiver buffers are freed
   * by audio_close_output(), so the lock-free render callback cannot access
   * released ring-buffer memory.
   */
  if (output->unit != NULL) {
    AudioOutputUnitStop(output->unit);
    AudioUnitUninitialize(output->unit);
    AudioComponentInstanceDispose(output->unit);
  }
  free(output);
}

#endif /* COREAUDIO */

#ifdef COREAUDIO

#define COREAUDIO_TCI_MONITOR_CHUNK 1024

static OSStatus coreaudio_tci_monitor_cb(void *refcon,
                                         AudioUnitRenderActionFlags *flags,
                                         const AudioTimeStamp *timestamp,
                                         UInt32 bus,
                                         UInt32 frames,
                                         AudioBufferList *io_data) {
  (void) flags;
  (void) timestamp;
  (void) bus;

  COREAUDIO_TCI_MONITOR *monitor = (COREAUDIO_TCI_MONITOR *) refcon;
  if (monitor == NULL || io_data == NULL ||
      io_data->mNumberBuffers != 1 || io_data->mBuffers[0].mData == NULL) {
    if (io_data != NULL) {
      for (UInt32 i = 0; i < io_data->mNumberBuffers; i++) {
        if (io_data->mBuffers[i].mData != NULL) {
          memset(io_data->mBuffers[i].mData, 0, io_data->mBuffers[i].mDataByteSize);
        }
      }
    }
    return noErr;
  }

  float *out = (float *) io_data->mBuffers[0].mData;
  UInt32 remaining = frames;

  /*
   * Real-time callback: no mutex, allocation or logging.
   * tci_audio_monitor_read() is a lock-free SPSC consumer.
   */
  while (remaining > 0) {
    UInt32 chunk_frames = remaining > COREAUDIO_TCI_MONITOR_CHUNK ?
                          COREAUDIO_TCI_MONITOR_CHUNK : remaining;
    float samples[COREAUDIO_TCI_MONITOR_CHUNK * TCI_AUDIO_CHANNELS];
    guint got = tci_audio_monitor_read(samples, (guint) chunk_frames);

    for (UInt32 i = 0; i < chunk_frames; i++) {
      float left = i < got ? samples[i * TCI_AUDIO_CHANNELS] : 0.0f;
      float right = i < got ? samples[i * TCI_AUDIO_CHANNELS + 1] : 0.0f;
      if (monitor->channels == 2) {
        *out++ = left;
        *out++ = right;
      } else {
        *out++ = 0.5f * (left + right);
      }
    }
    remaining -= chunk_frames;
  }

  io_data->mBuffers[0].mDataByteSize = frames * (UInt32) monitor->channels * sizeof(float);
  return noErr;
}

void *coreaudio_tci_monitor_open(const char *device_name, int *channels) {
  if (device_name == NULL || channels == NULL) {
    return NULL;
  }

  AudioDeviceID device = coreaudio_find_output_device(device_name);
  if (device == kAudioObjectUnknown) {
    t_print("%s: CoreAudio TCI monitor device not found: %s\n", __func__, device_name);
    return NULL;
  }

  int device_channels = coreaudio_device_channels(device, kAudioDevicePropertyScopeOutput);
  int client_channels = device_channels >= 2 ? 2 : 1;
  if (client_channels < 1) {
    return NULL;
  }

  COREAUDIO_TCI_MONITOR *monitor = calloc(1, sizeof(*monitor));
  if (monitor == NULL) {
    return NULL;
  }

  AudioComponentDescription desc = {
    .componentType = kAudioUnitType_Output,
    .componentSubType = kAudioUnitSubType_HALOutput,
    .componentManufacturer = kAudioUnitManufacturer_Apple,
    .componentFlags = 0,
    .componentFlagsMask = 0
  };

  AudioComponent component = AudioComponentFindNext(NULL, &desc);
  if (component == NULL ||
      AudioComponentInstanceNew(component, &monitor->unit) != noErr ||
      monitor->unit == NULL) {
    free(monitor);
    return NULL;
  }

  UInt32 enable = 1;
  UInt32 disable = 0;
  OSStatus status;

  status = AudioUnitSetProperty(monitor->unit, kAudioOutputUnitProperty_EnableIO,
                                kAudioUnitScope_Output, 0, &enable, sizeof(enable));
  if (status != noErr) { goto fail; }

  status = AudioUnitSetProperty(monitor->unit, kAudioOutputUnitProperty_EnableIO,
                                kAudioUnitScope_Input, 1, &disable, sizeof(disable));
  if (status != noErr) { goto fail; }

  status = AudioUnitSetProperty(monitor->unit, kAudioOutputUnitProperty_CurrentDevice,
                                kAudioUnitScope_Global, 0, &device, sizeof(device));
  if (status != noErr) { goto fail; }

  AudioStreamBasicDescription format;
  memset(&format, 0, sizeof(format));
  format.mSampleRate = TCI_AUDIO_SAMPLE_RATE;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
  format.mBytesPerPacket = (UInt32) client_channels * sizeof(float);
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = (UInt32) client_channels * sizeof(float);
  format.mChannelsPerFrame = (UInt32) client_channels;
  format.mBitsPerChannel = 8 * sizeof(float);

  status = AudioUnitSetProperty(monitor->unit, kAudioUnitProperty_StreamFormat,
                                kAudioUnitScope_Input, 0, &format, sizeof(format));
  if (status != noErr) { goto fail; }

  monitor->device = device;
  monitor->channels = client_channels;

  AURenderCallbackStruct callback = {
    .inputProc = coreaudio_tci_monitor_cb,
    .inputProcRefCon = monitor
  };
  status = AudioUnitSetProperty(monitor->unit, kAudioUnitProperty_SetRenderCallback,
                                kAudioUnitScope_Input, 0, &callback, sizeof(callback));
  if (status != noErr) { goto fail; }

  status = AudioUnitInitialize(monitor->unit);
  if (status != noErr) { goto fail; }

  status = AudioOutputUnitStart(monitor->unit);
  if (status != noErr) {
    AudioUnitUninitialize(monitor->unit);
    goto fail;
  }

  *channels = client_channels;
  t_print("%s: opened native CoreAudio TCI monitor device=%s id=%u channels=%d samplerate=%d\n",
          __func__, device_name, (unsigned int) device, client_channels, TCI_AUDIO_SAMPLE_RATE);
  return monitor;

fail:
  if (monitor->unit != NULL) {
    AudioComponentInstanceDispose(monitor->unit);
  }
  free(monitor);
  return NULL;
}

void coreaudio_tci_monitor_close(void *handle) {
  COREAUDIO_TCI_MONITOR *monitor = (COREAUDIO_TCI_MONITOR *) handle;
  if (monitor == NULL) {
    return;
  }

  if (monitor->unit != NULL) {
    AudioOutputUnitStop(monitor->unit);
    AudioUnitUninitialize(monitor->unit);
    AudioComponentInstanceDispose(monitor->unit);
  }
  free(monitor);
}

#endif /* COREAUDIO */

#ifdef COREAUDIO

static AudioDeviceID coreaudio_find_input_device(const char *device_name) {
  AudioObjectPropertyAddress address = {
    kAudioHardwarePropertyDevices,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };
  UInt32 size = 0;

  if (device_name == NULL || device_name[0] == '\0') {
    return kAudioObjectUnknown;
  }
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, NULL, &size) != noErr || size == 0) {
    return kAudioObjectUnknown;
  }

  UInt32 count = size / sizeof(AudioDeviceID);
  AudioDeviceID *devices = malloc(size);
  if (devices == NULL) {
    return kAudioObjectUnknown;
  }
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, devices) != noErr) {
    free(devices);
    return kAudioObjectUnknown;
  }

  AudioDeviceID found = kAudioObjectUnknown;
  for (UInt32 i = 0; i < count; i++) {
    if (coreaudio_device_channels(devices[i], kAudioDevicePropertyScopeInput) <= 0) {
      continue;
    }

    char name[512];
    if (coreaudio_device_name(devices[i], name, sizeof(name)) && strcmp(name, device_name) == 0) {
      found = devices[i];
      break;
    }
  }

  free(devices);
  return found;
}

static OSStatus coreaudio_input_cb(void *refcon,
                                   AudioUnitRenderActionFlags *flags,
                                   const AudioTimeStamp *timestamp,
                                   UInt32 bus,
                                   UInt32 frames,
                                   AudioBufferList *unused_data) {
  (void) bus;
  (void) unused_data;

  COREAUDIO_INPUT *input = (COREAUDIO_INPUT *) refcon;
  if (input == NULL || input->unit == NULL || input->buffer == NULL ||
      frames == 0 || frames > input->max_frames) {
    return noErr;
  }

  AudioBufferList list;
  memset(&list, 0, sizeof(list));
  list.mNumberBuffers = 1;
  list.mBuffers[0].mNumberChannels = 1;
  list.mBuffers[0].mDataByteSize = frames * sizeof(float);
  list.mBuffers[0].mData = input->buffer;

  OSStatus status = AudioUnitRender(input->unit, flags, timestamp, 1, frames, &list);
  if (status == noErr) {
    audio_process_local_mic_input(input->buffer, frames);
  }
  return noErr;
}

void *coreaudio_input_open(const char *device_name) {
  if (device_name == NULL || device_name[0] == '\0') {
    return NULL;
  }

  AudioDeviceID device = coreaudio_find_input_device(device_name);
  if (device == kAudioObjectUnknown) {
    t_print("%s: CoreAudio input device not found: %s\n", __func__, device_name);
    return NULL;
  }

  COREAUDIO_INPUT *input = calloc(1, sizeof(*input));
  if (input == NULL) {
    return NULL;
  }

  AudioComponentDescription desc = {
    .componentType = kAudioUnitType_Output,
    .componentSubType = kAudioUnitSubType_HALOutput,
    .componentManufacturer = kAudioUnitManufacturer_Apple,
    .componentFlags = 0,
    .componentFlagsMask = 0
  };

  AudioComponent component = AudioComponentFindNext(NULL, &desc);
  if (component == NULL ||
      AudioComponentInstanceNew(component, &input->unit) != noErr ||
      input->unit == NULL) {
    t_print("%s: cannot create AUHAL input unit\n", __func__);
    free(input);
    return NULL;
  }

  UInt32 enable = 1;
  UInt32 disable = 0;
  OSStatus status;

  status = AudioUnitSetProperty(input->unit,
                                kAudioOutputUnitProperty_EnableIO,
                                kAudioUnitScope_Input,
                                1,
                                &enable,
                                sizeof(enable));
  if (status != noErr) {
    t_print("%s: EnableIO(input) failed status=%d\n", __func__, (int) status);
    goto fail;
  }

  status = AudioUnitSetProperty(input->unit,
                                kAudioOutputUnitProperty_EnableIO,
                                kAudioUnitScope_Output,
                                0,
                                &disable,
                                sizeof(disable));
  if (status != noErr) {
    t_print("%s: DisableIO(output) failed status=%d\n", __func__, (int) status);
    goto fail;
  }

  status = AudioUnitSetProperty(input->unit,
                                kAudioOutputUnitProperty_CurrentDevice,
                                kAudioUnitScope_Global,
                                0,
                                &device,
                                sizeof(device));
  if (status != noErr) {
    t_print("%s: CurrentDevice failed status=%d\n", __func__, (int) status);
    goto fail;
  }

  AudioStreamBasicDescription format;
  memset(&format, 0, sizeof(format));
  format.mSampleRate = COREAUDIO_SAMPLE_RATE;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
  format.mBytesPerPacket = sizeof(float);
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = sizeof(float);
  format.mChannelsPerFrame = 1;
  format.mBitsPerChannel = 8 * sizeof(float);

  /*
   * For AUHAL input, the client-side format is set on the OUTPUT scope
   * of input element 1. AudioUnitRender() then delivers mono float32/48 kHz.
   */
  status = AudioUnitSetProperty(input->unit,
                                kAudioUnitProperty_StreamFormat,
                                kAudioUnitScope_Output,
                                1,
                                &format,
                                sizeof(format));
  if (status != noErr) {
    t_print("%s: input StreamFormat failed status=%d device=%s\n",
            __func__, (int) status, device_name);
    goto fail;
  }

  UInt32 size = sizeof(input->max_frames);
  status = AudioUnitGetProperty(input->unit,
                                kAudioUnitProperty_MaximumFramesPerSlice,
                                kAudioUnitScope_Global,
                                0,
                                &input->max_frames,
                                &size);
  if (status != noErr || input->max_frames == 0) {
    input->max_frames = 4096;
  }
  if (input->max_frames < 4096) {
    input->max_frames = 4096;
  }

  input->buffer = calloc(input->max_frames, sizeof(float));
  if (input->buffer == NULL) {
    goto fail;
  }

  input->device = device;

  AURenderCallbackStruct callback = {
    .inputProc = coreaudio_input_cb,
    .inputProcRefCon = input
  };
  status = AudioUnitSetProperty(input->unit,
                                kAudioOutputUnitProperty_SetInputCallback,
                                kAudioUnitScope_Global,
                                0,
                                &callback,
                                sizeof(callback));
  if (status != noErr) {
    t_print("%s: SetInputCallback failed status=%d\n", __func__, (int) status);
    goto fail;
  }

  status = AudioUnitInitialize(input->unit);
  if (status != noErr) {
    t_print("%s: AudioUnitInitialize(input) failed status=%d\n", __func__, (int) status);
    goto fail;
  }

  status = AudioOutputUnitStart(input->unit);
  if (status != noErr) {
    t_print("%s: AudioOutputUnitStart(input) failed status=%d\n", __func__, (int) status);
    AudioUnitUninitialize(input->unit);
    goto fail;
  }

  t_print("%s: opened native CoreAudio input device=%s id=%u channels=1 samplerate=48000 maxframes=%u\n",
          __func__, device_name, (unsigned int) device, (unsigned int) input->max_frames);
  return input;

fail:
  if (input->unit != NULL) {
    AudioComponentInstanceDispose(input->unit);
  }
  free(input->buffer);
  free(input);
  return NULL;
}

void coreaudio_input_close(void *handle) {
  COREAUDIO_INPUT *input = (COREAUDIO_INPUT *) handle;
  if (input == NULL) {
    return;
  }

  /*
   * Stop the AUHAL callback before releasing the callback scratch buffer.
   */
  if (input->unit != NULL) {
    AudioOutputUnitStop(input->unit);
    AudioUnitUninitialize(input->unit);
    AudioComponentInstanceDispose(input->unit);
  }

  free(input->buffer);
  free(input);
}

#endif /* COREAUDIO */

#endif
