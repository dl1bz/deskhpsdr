/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <gtk/gtk.h>
#include "main.h"
#include "discovery.h"
#include "receiver.h"
#include "sliders.h"
#include "toolbar.h"
#include "vfo.h"
#include "radio.h"
#include "radio_menu.h"
#include "new_menu.h"
#include "noise_menu.h"
#include "ext.h"
#include "zoompan.h"
#include "equalizer_menu.h"
#include "voice_keyer.h"

//
// The following calls functions can be called usig g_idle_add
// Their return value is G_SOURCE_REMOVE so they will be called only
// once.
//

// cppcheck-suppress constParameterPointer
int ext_start_radio(void *data) {
  radio_start_radio();
  return G_SOURCE_REMOVE;
}

//
// ALL calls to vfo_update should go through g_idle_add(ext_vfo_update)
// Here we take care that vfo_update() is called at most every 100 msec,
// but that also after a g_idle_add(ext_vfo_update) the vfo_update is
// called in the next 100 msec.
//
static guint vfo_timeout = 0;

// cppcheck-suppress constParameterCallback
static int vfo_timeout_cb(void * data) {
  vfo_timeout = 0;
  vfo_update();
  return G_SOURCE_REMOVE;
}

int ext_vfo_update(void *data) {
  //
  // If no timeout is pending, then a vfo_update() is to
  // be scheduled soon.
  //
  if (vfo_timeout == 0) {
    vfo_timeout = g_timeout_add(100, vfo_timeout_cb, NULL);
  }

  return G_SOURCE_REMOVE;
}

int ext_tune_update(void *data) {
  radio_tune_update(GPOINTER_TO_INT(data));
  return G_SOURCE_REMOVE;
}

int ext_mox_update(void *data) {
  int state = GPOINTER_TO_INT(data);
  /* Nur bei externer PTT ON: VK abbrechen, Mic übernimmt */
  if (state) {
    voice_keyer_stop_for_ptt_takeover();
  }
  radio_mox_update(state);
  return G_SOURCE_REMOVE;
}

int ext_set_vox(void *data) {
  radio_set_vox(GPOINTER_TO_INT(data));
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_start_band(void *data) {
  start_band();
  return G_SOURCE_REMOVE;
}

int ext_start_vfo(void *data) {
  int val = GPOINTER_TO_INT(data);
  start_vfo(val);
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_start_rx(void *data) {
  start_rx();
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_start_tx(void *data) {
  start_tx();
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_update_noise(void *data) {
  update_noise();
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_update_eq(void *data) {
  update_eq();
  return G_SOURCE_REMOVE;
}

// cppcheck-suppress constParameterPointer
int ext_set_duplex(void *data) {
  setDuplex();
  return G_SOURCE_REMOVE;
}
