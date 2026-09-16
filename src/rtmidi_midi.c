/* Copyright (C)
* 2024-2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
* SPDX-License-Identifier: GPL-3.0-or-later
*
* RtMidi based Layer-1 MIDI backend for deskHPSDR.
*/

#include <gtk/gtk.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "midi_layer.h"
#include "midi_menu.h"
#include "alsa_midi.h"
#include "message.h"
#include "rtmidi_c.h"

MIDI_DEVICE midi_devices[MAX_MIDI_DEVICES];
int n_midi_devices;

static RtMidiInPtr midi_input[MAX_MIDI_DEVICES];
static gboolean configure = FALSE;
static gboolean initialized = FALSE;

static void midi_callback(double timestamp, const unsigned char *message, size_t message_size, void *user_data) {
  int index = GPOINTER_TO_INT(user_data);
  unsigned char status;
  int channel;
  int arg1;
  int arg2;
  (void)timestamp;
  if (index < 0 || index >= MAX_MIDI_DEVICES || !midi_devices[index].active) { return; }
  if (message == NULL || message_size < 1) { return; }
  status = message[0];
  if ((status & 0x80) == 0) { return; }
  channel = status & 0x0f;
  switch (status & 0xf0) {
  case 0x80:
    if (message_size < 3) { return; }
    arg1 = message[1];
    if (configure) {
      NewMidiConfigureEvent(MIDI_NOTE, channel, arg1, 0);
    } else {
      NewMidiEvent(MIDI_NOTE, channel, arg1, 0);
    }
    break;
  case 0x90:
    if (message_size < 3) { return; }
    arg1 = message[1];
    arg2 = message[2];
    if (configure) {
      NewMidiConfigureEvent(MIDI_NOTE, channel, arg1, arg2 == 0 ? 0 : 1);
    } else {
      NewMidiEvent(MIDI_NOTE, channel, arg1, arg2 == 0 ? 0 : 1);
    }
    break;
  case 0xb0:
    if (message_size < 3) { return; }
    arg1 = message[1];
    arg2 = message[2];
    if (!midiIgnoreCtrlPairs || arg1 < 32 || arg1 >= 64) {
      if (configure) {
        NewMidiConfigureEvent(MIDI_CTRL, channel, arg1, arg2);
      } else {
        NewMidiEvent(MIDI_CTRL, channel, arg1, arg2);
      }
    }
    break;
  case 0xe0:
    if (message_size < 3) { return; }
    arg1 = message[1];
    arg2 = message[2];
    if (configure) {
      NewMidiConfigureEvent(MIDI_PITCH, channel, 0, arg1 + 128 * arg2);
    } else {
      NewMidiEvent(MIDI_PITCH, channel, 0, arg1 + 128 * arg2);
    }
    break;
  default:
    break;
  }
}

void configure_midi_device(gboolean state) {
  configure = state;
}

void close_midi_device(int index) {
  t_print("%s: index=%d\n", __func__, index);
  if (index < 0 || index >= MAX_MIDI_DEVICES) { return; }
  midi_devices[index].active = 0;
  if (midi_input[index] != NULL) {
    rtmidi_in_cancel_callback(midi_input[index]);
    rtmidi_close_port(midi_input[index]);
    rtmidi_in_free(midi_input[index]);
    midi_input[index] = NULL;
  }
}

void register_midi_device(int index) {
  RtMidiInPtr input;
  if (index < 0 || index >= n_midi_devices) { return; }
  if (midi_input[index] != NULL) { return; }
  t_print("%s: open MIDI device %d (%s)\n", __func__, index,
          midi_devices[index].name != NULL ? midi_devices[index].name : "unknown");
  input = rtmidi_in_create_default();
  if (input == NULL || input->ptr == NULL || !input->ok) {
    t_print("%s: cannot create RtMidi input%s%s\n", __func__,
            input != NULL && input->msg != NULL ? ": " : "",
            input != NULL && input->msg != NULL ? input->msg : "");
    if (input != NULL) { rtmidi_in_free(input); }
    return;
  }
  rtmidi_in_ignore_types(input, true, true, true);
  rtmidi_in_set_callback(input, midi_callback, GINT_TO_POINTER(index));
  if (!input->ok) {
    t_print("%s: cannot set RtMidi callback%s%s\n", __func__,
            input->msg != NULL ? ": " : "", input->msg != NULL ? input->msg : "");
    rtmidi_in_free(input);
    return;
  }
  rtmidi_open_port(input, (unsigned int)index, "deskHPSDR");
  if (!input->ok) {
    t_print("%s: cannot open MIDI device %d%s%s\n", __func__, index,
            input->msg != NULL ? ": " : "", input->msg != NULL ? input->msg : "");
    rtmidi_in_cancel_callback(input);
    rtmidi_in_free(input);
    return;
  }
  midi_input[index] = input;
  midi_devices[index].active = 1;
}

void get_midi_devices(void) {
  RtMidiInPtr probe;
  unsigned int count;
  int new_count = 0;
  if (!initialized) {
    for (int i = 0; i < MAX_MIDI_DEVICES; i++) {
      midi_devices[i].name = NULL;
      midi_devices[i].active = 0;
      midi_input[i] = NULL;
    }
    initialized = TRUE;
  }
  probe = rtmidi_in_create_default();
  if (probe == NULL || probe->ptr == NULL || !probe->ok) {
    t_print("%s: cannot create RtMidi probe%s%s\n", __func__,
            probe != NULL && probe->msg != NULL ? ": " : "",
            probe != NULL && probe->msg != NULL ? probe->msg : "");
    if (probe != NULL) { rtmidi_in_free(probe); }
    return;
  }
  count = rtmidi_get_port_count(probe);
  if (!probe->ok) {
    t_print("%s: cannot enumerate MIDI devices%s%s\n", __func__,
            probe->msg != NULL ? ": " : "", probe->msg != NULL ? probe->msg : "");
    rtmidi_in_free(probe);
    return;
  }
  if (count > MAX_MIDI_DEVICES) { count = MAX_MIDI_DEVICES; }
  for (unsigned int i = 0; i < count; i++) {
    int len = 0;
    char *name = NULL;
    if (rtmidi_get_port_name(probe, i, NULL, &len) == 0 && len > 0) {
      name = g_malloc0((gsize)len);
      if (rtmidi_get_port_name(probe, i, name, &len) < 0) {
        g_free(name);
        name = NULL;
      }
    }
    if (name == NULL || name[0] == '\0') {
      g_free(name);
      name = g_strdup_printf("NoPort%d", new_count);
    }
    t_print("%s: %s\n", __func__, name);
    if (midi_devices[new_count].name != NULL && strcmp(name, midi_devices[new_count].name) != 0) {
      close_midi_device(new_count);
      g_free(midi_devices[new_count].name);
      midi_devices[new_count].name = NULL;
    }
    if (midi_devices[new_count].name == NULL) {
      midi_devices[new_count].name = g_strdup(name);
      midi_devices[new_count].active = 0;
    }
    g_free(name);
    new_count++;
  }
  rtmidi_in_free(probe);
  n_midi_devices = new_count;
  for (int i = n_midi_devices; i < MAX_MIDI_DEVICES; i++) {
    close_midi_device(i);
    if (midi_devices[i].name != NULL) {
      g_free(midi_devices[i].name);
      midi_devices[i].name = NULL;
    }
  }
  t_print("%s: number of devices=%d\n", __func__, n_midi_devices);
}
