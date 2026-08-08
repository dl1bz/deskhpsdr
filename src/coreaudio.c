/* Copyright (C)
* 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
* Native CoreAudio output backend.
*
* Only local RX/CW playback is handled here. The microphone input remains
* on PortAudio at this stage.
*/

#ifdef NATIVE_COREAUDIO_OUTPUT

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "coreaudio.h"
#include "message.h"

#define COREAUDIO_SAMPLE_RATE 48000.0

typedef struct {
  AudioComponentInstance unit;
  AudioDeviceID device;
  RECEIVER *rx;
  int channels;
} COREAUDIO_OUTPUT;

static int coreaudio_output_channels(AudioDeviceID device) {
  AudioObjectPropertyAddress address = {
    kAudioDevicePropertyStreamConfiguration,
    kAudioDevicePropertyScopeOutput,
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
    if (coreaudio_output_channels(devices[i]) <= 0) {
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

  int device_channels = coreaudio_output_channels(device);
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

#endif
