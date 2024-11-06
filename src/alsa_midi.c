/* Copyright (C)
* 2019 - Christoph van Wüllen, DL1YCF
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

/*
 * MIDI support for pihpsdr
 *
 * This is the "Layer-1" for ALSA-MIDI (Linux)
 * For further comments see file mac_midi.c
 */

/*
 * ALSA: MIDI devices are sub-devices to sound cards.
 *       Therefore we have to loop through the sound cards
 *       and then, for each sound card, through the
 *       sub-devices until we have found "our" MIDI
 *       input device.
 *
 *       The procedure how to find and talk with
 *       a MIDI device is taken from the sample
 *       program amidi.c in alsautils.
 */

#include <gtk/gtk.h>

#include "actions.h"
#include "midi.h"
#include "midi_menu.h"
#include "alsa_midi.h"
#include "message.h"

#ifndef __APPLE__

#include <pthread.h>
#include <alsa/asoundlib.h>

MIDI_DEVICE midi_devices[MAX_MIDI_DEVICES];
int n_midi_devices;

//
// The following must not reside in midi_devices since it
// needs special #includes
//
static pthread_t midi_thread_id[MAX_MIDI_DEVICES];
static char *midi_port[MAX_MIDI_DEVICES];
static snd_rawmidi_t *midi_input[MAX_MIDI_DEVICES];

static void* midi_thread(void *);

static enum {
  STATE_SKIP,             // skip bytes
  STATE_ARG1,             // one arg byte to come
  STATE_ARG2,             // two arg bytes to come
} state = STATE_SKIP;

static enum {
  CMD_NOTEON,
  CMD_NOTEOFF,
  CMD_CTRL,
  CMD_PITCH,
} command;

static gboolean configure = FALSE;

void configure_midi_device(gboolean state) {
  configure = state;
}

static void *midi_thread(void *arg) {
  int index = (int) (uintptr_t) arg;
  snd_rawmidi_t *input = midi_input[index];
  char *port = midi_port[index];
  int npfds;
  //struct pollfd *pfds;
  unsigned char buf[32];
  unsigned char byte;
  unsigned short revents;
  int i;
  int chan = 0, arg1 = 0, arg2;
  npfds = snd_rawmidi_poll_descriptors_count(input);
  // replaced alloca by variable-length array
  struct pollfd pfds[npfds];
  //pfds = alloca(npfds * sizeof(struct pollfd));
  snd_rawmidi_poll_descriptors(input, pfds, npfds);

  for (;;) {
    int ret = poll(pfds, npfds, 250);

    if (!midi_devices[index].active) { break; }

    if (ret < 0) {
      t_print("%s: poll failed: %s\n", __FUNCTION__, strerror(errno));
      // Do not give up, but also do not fire too rapidly
      usleep(250000);
    }

    if (ret <= 0) { continue; }  // nothing arrived, do next poll()

    if ((ret = snd_rawmidi_poll_descriptors_revents(input, pfds, npfds, &revents)) < 0) {
      t_print("%s: cannot get poll events: %s\n", __FUNCTION__, snd_strerror(errno));
      continue;
    }

    if (revents & (POLLERR | POLLHUP)) { continue; }

    if (!(revents & POLLIN)) { continue; }

    // something has arrived
    ret = snd_rawmidi_read(input, buf, 64);

    if (ret == 0) { continue; }

    if (ret < 0) {
      t_print("%s: cannot read from port \"%s\": %s\n", __FUNCTION__, port, snd_strerror(ret));
      continue;
    }

    // process bytes in buffer. Since they no not necessarily form complete messages
    // we need a state machine here.
    for (i = 0; i < ret; i++) {
      byte = buf[i];

      switch (state) {
      case STATE_SKIP:
        chan = byte & 0x0F;

        switch (byte & 0xF0) {
        case 0x80:      // Note-OFF command
          command = CMD_NOTEOFF;
          state = STATE_ARG2;
          break;

        case 0x90:      // Note-ON command
          command = CMD_NOTEON;
          state = STATE_ARG2;
          break;

        case 0xB0:      // Controller Change
          command = CMD_CTRL;
          state = STATE_ARG2;
          break;

        case 0xE0:      // Pitch Bend
          command = CMD_PITCH;
          state = STATE_ARG2;
          break;

        case 0xA0:      // Polyphonic Pressure
        case 0xC0:      // Program change
        case 0xD0:      // Channel pressure
        case 0xF0:      // System Message: continue waiting for bit7 set
        default:        // Remain in STATE_SKIP until bit7 is set
          break;
        }

        break;

      case STATE_ARG2:
        arg1 = byte;
        state = STATE_ARG1;
        break;

      case STATE_ARG1:
        arg2 = byte;

        // We have a command!
        switch (command) {
        case CMD_NOTEON:

          // Hercules MIDI controllers generate NoteOn
          // messages with velocity == 0 when releasing
          // a push-button
          if (arg2 == 0) {
            if (configure) {
              NewMidiConfigureEvent(MIDI_NOTE, chan, arg1, 0);
            } else {
              NewMidiEvent(MIDI_NOTE, chan, arg1, 0);
            }
          } else {
            if (configure) {
              NewMidiConfigureEvent(MIDI_NOTE, chan, arg1, 1);
            } else {
              NewMidiEvent(MIDI_NOTE, chan, arg1, 1);
            }
          }

          break;

        case CMD_NOTEOFF:
          if (configure) {
            NewMidiConfigureEvent(MIDI_NOTE, chan, arg1, 0);
          } else {
            NewMidiEvent(MIDI_NOTE, chan, arg1, 0);
          }

          break;

        case CMD_CTRL:

          //
          // When ignoring "controller pairs", all ControllerChange events
          // for controllers 32...63 are ignored
          //
          if (!midiIgnoreCtrlPairs || arg1 < 32 || arg1 >= 64) {
            if (configure) {
              NewMidiConfigureEvent(MIDI_CTRL, chan, arg1, arg2);
            } else {
              NewMidiEvent(MIDI_CTRL, chan, arg1, arg2);
            }
          }

          break;

        case CMD_PITCH:
          if (configure) {
            NewMidiConfigureEvent(MIDI_PITCH, chan, 0, arg1 + 128 * arg2);
          } else {
            NewMidiEvent(MIDI_PITCH, chan, 0, arg1 + 128 * arg2);
          }

          break;
        }

        state = STATE_SKIP;
        break;
      }
    }
  }

  return NULL;
}

void register_midi_device(int index) {
  int ret = 0;

  if (index < 0 || index >= n_midi_devices) { return; }

  t_print("%s: open MIDI device %d\n", __FUNCTION__, index);

  if ((ret = snd_rawmidi_open(&midi_input[index], NULL, midi_port[index], SND_RAWMIDI_NONBLOCK)) < 0) {
    t_print("%s: cannot open port \"%s\": %s\n", __FUNCTION__, midi_port[index], snd_strerror(ret));
    return;
  }

  snd_rawmidi_read(midi_input[index], NULL, 0); /* trigger reading */
  ret = pthread_create(&midi_thread_id[index], NULL, midi_thread, (void *) (uintptr_t) index);

  if (ret < 0) {
    t_print("%s: Failed to create MIDI read thread\n", __FUNCTION__);

    if ((ret = snd_rawmidi_close(midi_input[index])) < 0) {
      t_print("%s: cannot close port: %s\n", __FUNCTION__, snd_strerror(ret));
    }

    return;
  }

  midi_devices[index].active = 1;
  return;
}

void close_midi_device(int index) {
  int ret;
  t_print("%s: index=%d\n", __FUNCTION__, index);

  if (index < 0 || index >= MAX_MIDI_DEVICES) { return; }

  if (midi_devices[index].active == 0) { return; }

  //
  // Note that if this is called from get_midi_devices(),
  // the port and device names do exist but may be wrong.
  //
  // Tell thread to stop
  //
  midi_devices[index].active = 0;
  //
  // wait for thread to complete
  //
  ret = pthread_join(midi_thread_id[index], NULL);

  if (ret  != 0)  {
    t_print("%s: cannot join: %s\n", __FUNCTION__, strerror(ret));
  }

  //
  // Close MIDI device
  if ((ret = snd_rawmidi_close(midi_input[index])) < 0) {
    t_print("%s: cannot close port: %s\n", __FUNCTION__, snd_strerror(ret));
  }
}

void get_midi_devices() {
  snd_ctl_t *ctl;
  snd_rawmidi_info_t *info;
  int card, device, subs, sub, ret;
  const char *devnam, *subnam;
  char portname[64];
  n_midi_devices = 0;
  card = -1;

  if ((ret = snd_card_next(&card)) < 0) {
    t_print("%s: cannot determine card number: %s\n", __FUNCTION__, snd_strerror(ret));
    return;
  }

  while (card >= 0) {
    //t_print("%s: Found Sound Card=%d\n", __FUNCTION__, card);
    snprintf(portname, 64, "hw:%d", card);

    if ((ret = snd_ctl_open(&ctl, portname, 0)) < 0) {
      t_print("%s: cannot open control for card %d: %s\n", __FUNCTION__, card, snd_strerror(ret));
      return;
    }

    device = -1;

    // loop through devices of the card
    for (;;) {
      if ((ret = snd_ctl_rawmidi_next_device(ctl, &device)) < 0) {
        t_print("%s: cannot determine device number: %s\n", __FUNCTION__, snd_strerror(ret));
        break;
      }

      if (device < 0) { break; }

      //t_print("%s: Found Device=%d on Card=%d\n", __FUNCTION__, device, card);
      // found sub-device
      snd_rawmidi_info_alloca(&info);
      snd_rawmidi_info_set_device(info, device);
      snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
      ret = snd_ctl_rawmidi_info(ctl, info);

      if (ret >= 0) {
        subs = snd_rawmidi_info_get_subdevices_count(info);
      } else {
        subs = 0;
      }

      //t_print("%s: Number of MIDI input devices: %d\n", __FUNCTION__, subs);
      if (!subs) { break; }

      // subs: number of sub-devices to device on card
      for (sub = 0; sub < subs; ++sub) {
        snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
        snd_rawmidi_info_set_subdevice(info, sub);
        ret = snd_ctl_rawmidi_info(ctl, info);

        if (ret < 0) {
          t_print("%s: cannot get rawmidi information %d:%d:%d: %s\n",
                  __FUNCTION__, card, device, sub, snd_strerror(ret));
          break;
        }

        devnam = snd_rawmidi_info_get_name(info);
        subnam = snd_rawmidi_info_get_subdevice_name(info);

        // If there is only one sub-device and it has no name, we  use
        // devnam for comparison and make a portname of form "hw:x,y",
        // else we use subnam for comparison and make a portname of form "hw:x,y,z".
        if (sub == 0 && subnam[0] == '\0') {
          snprintf(portname, 64, "hw:%d,%d", card, device);
        } else {
          snprintf(portname, 64, "hw:%d,%d,%d", card, device, sub);
          devnam = subnam;
        }

        //
        // If the name was already present at the same position, just keep
        // it and do nothing.
        // If the names do not match and the slot is occupied by a opened device,
        // close it first
        //
        int match = 1;

        if (midi_devices[n_midi_devices].name == NULL) {
          midi_devices[n_midi_devices].name = g_strdup(devnam);
          match = 0;
        } else {
          if (strcmp(devnam, midi_devices[n_midi_devices].name)) {
            g_free(midi_devices[n_midi_devices].name);
            midi_devices[n_midi_devices].name = g_strdup(devnam);
            match = 0;
          }
        }

        if (midi_port[n_midi_devices] == NULL) {
          midi_port[n_midi_devices] = g_strdup(portname);
          match = 0;
        } else {
          if (strcmp(midi_port[n_midi_devices], portname)) {
            g_free(midi_port[n_midi_devices]);
            midi_port[n_midi_devices] = g_strdup(portname);
            match = 0;
          }
        }

        //
        // Close MIDI device if it was open, except if the device is
        // the same as before. In this case, just let the thread
        // proceed
        //
        if (match == 0 && midi_devices[n_midi_devices].active) {
          close_midi_device(n_midi_devices);
        }

        n_midi_devices++;
      }
    }

    snd_ctl_close(ctl);

    // next card
    if ((ret = snd_card_next(&card)) < 0) {
      t_print("%s: cannot determine card number: %s\n", __FUNCTION__, snd_strerror(ret));
      break;
    }
  }

  for (int i = 0; i < n_midi_devices; i++) {
    t_print("%s: %d: %s %s\n", __FUNCTION__, i, midi_devices[i].name, midi_port[i]);
  }
}

#endif
