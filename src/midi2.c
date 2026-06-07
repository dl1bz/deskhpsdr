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

/*
 * Layer-2 of MIDI support
 *
 * Using the data in MIDICommandsTable, this subroutine translates the low-level
 * MIDI events into MIDI actions in the SDR console.
 */

#include <gtk/gtk.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#ifdef __APPLE__
  #include "MacOS.h"  // emulate clock_gettime on old MacOS systems
#endif

#include "receiver.h"
#include "discovered.h"
#include "adc.h"
#include "dac.h"
#include "transmitter.h"
#include "radio.h"
#include "main.h"
#include "actions.h"
#include "midi_layer.h"
#include "alsa_midi.h"
#include "message.h"

struct desc *MidiCommandsTable[129];

void NewMidiEvent(enum MIDIevent event, int channel, int note, int val) {
  struct desc *desc;
  int new;
#ifdef MIDIDEBUG
  t_print("%s:EVENT=%d CHAN=%d NOTE=%d VAL=%d\n", __func__, event, channel, note, val);
#endif
  //
  // Sometimes a "heart beat" from a device might be useful. Therefore, we resert
  // channel=16 note=0 for this purpose and filter this out here
  //
  if (event == MIDI_NOTE && channel == 15 && note == 0) {
    return;
  }
  if (event == MIDI_PITCH) {
    desc = MidiCommandsTable[128];
  } else {
    desc = MidiCommandsTable[note];
  }
  //t_print("%s: init DESC=%p\n",__func__,desc);
  while (desc) {
    //t_print("%s: DESC=%p next=%p CHAN=%d EVENT=%d\n",__func__,desc,desc->next,desc->channel,desc->event);
    if ((desc->channel == channel || desc->channel == -1) && (desc->event == event)) {
      // Found matching entry
      switch (desc->event) {
      case EVENT_NONE:
        // this cannot happen
        t_print("%s: Unknown Event\n", __func__);
        break;
      case MIDI_NOTE:
        DoTheMidi(desc->action, desc->type, val);
        break;
      case MIDI_CTRL:
        if (desc->type == MIDI_KNOB) {
          // CHANGED Jan 2024: report the "raw" value (0-127) upstream
          DoTheMidi(desc->action, desc->type, val);
        } else if (desc->type == MIDI_WHEEL) {
          // translate value to direction/speed
          new = 0;
          if ((val >= desc->vfl1) && (val <= desc->vfl2)) { new = -16; }
          if ((val >= desc-> fl1) && (val <= desc-> fl2)) { new = -4; }
          if ((val >= desc->lft1) && (val <= desc->lft2)) { new = -1; }
          if ((val >= desc->rgt1) && (val <= desc->rgt2)) { new = 1; }
          if ((val >= desc-> fr1) && (val <= desc-> fr2)) { new = 4; }
          if ((val >= desc->vfr1) && (val <= desc->vfr2)) { new = 16; }
          //                      t_print("%s: WHEEL PARAMS: val=%d new=%d thrs=%d/%d, %d/%d, %d/%d, %d/%d, %d/%d, %d/%d\n",
          //                               __func__,
          //                               val, new, desc->vfl1, desc->vfl2, desc->fl1, desc->fl2, desc->lft1, desc->lft2,
          //                               desc->rgt1, desc->rgt2, desc->fr1, desc->fr2, desc->vfr1, desc->vfr2);
          if (new != 0) { DoTheMidi(desc->action, desc->type, new); }
        }
        break;
      case MIDI_PITCH:
        if (desc->type == MIDI_KNOB) {
          // use upper 7  bits
          DoTheMidi(desc->action, desc->type, val >> 7);
        }
        break;
      }
      break;
    } else {
      desc = desc->next;
    }
  }
  if (!desc) {
    // Nothing found. This is nothing to worry about, but log the key to stderr
    if (event == MIDI_PITCH) { t_print("%s: Unassigned PitchBend Value=%d\n", __func__, val); }
    if (event == MIDI_NOTE) { t_print("%s: Unassigned Key Note=%d Val=%d\n", __func__, note, val); }
    if (event == MIDI_CTRL) { t_print("%s: Unassigned Controller Ctl=%d Val=%d\n", __func__, note, val); }
  }
}

/*
 * Release data from MidiCommandsTable
 */

void MidiReleaseCommands(void) {
  int i;
  struct desc *loop, *new;
  for (i = 0; i < 129; i++) {
    loop = MidiCommandsTable[i];
    while (loop != NULL) {
      new = loop->next;
      free(loop);
      loop = new;
    }
    MidiCommandsTable[i] = NULL;
  }
}

/*
 * Add a command to MidiCommandsTable
 */

void MidiAddCommand(int note, struct desc *desc) {
  struct desc *loop;
  if (note < 0 || note > 128) { return; }
  //
  // Actions with channel == -1 (ANY) must go to the end of the list
  //
  if (MidiCommandsTable[note] == NULL) {
    // initialize linked list
    MidiCommandsTable[note] = desc;
  } else if (desc->channel >= 0) {
    // add to top of the list
    desc->next = MidiCommandsTable[note];
    MidiCommandsTable[note] = desc;
  } else {
    // add to tail of the list
    loop = MidiCommandsTable[note];
    while (loop->next != NULL) {
      loop = loop->next;
    }
    loop->next = desc;
  }
}
