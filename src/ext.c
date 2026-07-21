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
#include "filter.h"
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
#include "old_protocol.h"
#include "agc.h"

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
static int vfo_timeout_cb(void *data) {
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
  int state = GPOINTER_TO_INT(data);
  int old_state = radio_get_tune();
  radio_tune_update(state);
  if (device == DEVICE_HERMES_LITE2 && hl2_pico_is_present()) {
    if (!old_state && state) {
      hl2_iob_set_antenna_tuner(1);
    } else if (old_state && !state) {
      hl2_iob_set_antenna_tuner(0);
    }
  }
  update_slider_tune_drive_btn();
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

int ext_mox_update_immediate(void *data) {
  int state = GPOINTER_TO_INT(data);
  if (state) {
    voice_keyer_stop_for_ptt_takeover();
  }
  radio_mox_update_immediate(state);
  return G_SOURCE_REMOVE;
}

int ext_set_vox(void *data) {
  radio_set_vox(GPOINTER_TO_INT(data));
  return G_SOURCE_REMOVE;
}

int ext_set_af_gain(void *data) {
  EXT_AF_GAIN_UPDATE *ag = (EXT_AF_GAIN_UPDATE *) data;
  int receiver_id;
  double value;
  if (ag == NULL) {
    return G_SOURCE_REMOVE;
  }
  receiver_id = ag->receiver_id;
  value = ag->value;
  if (value < -40.0) {
    value = -40.0;
  }
  if (value > 0.0) {
    value = 0.0;
  }
  if (receiver_id >= 0 && receiver_id < receivers && receiver_id < 2 && receiver[receiver_id] != NULL) {
    receiver[receiver_id]->volume = value;
    rx_set_af_gain(receiver[receiver_id]);
    if (display_sliders && active_receiver != NULL && receiver_id == active_receiver->id) {
      update_slider_af_gain_scale();
    }
  }
  g_free(ag);
  return G_SOURCE_REMOVE;
}


int ext_set_agc_gain(void *data) {
  EXT_AGC_GAIN_UPDATE *ag = (EXT_AGC_GAIN_UPDATE *) data;
  int receiver_id;
  double value;
  if (ag == NULL) {
    return G_SOURCE_REMOVE;
  }
  receiver_id = ag->receiver_id;
  value = ag->value;
  if (value < -20.0) {
    value = -20.0;
  }
  if (value > 120.0) {
    value = 120.0;
  }
  if (receiver_id >= 0 && receiver_id < receivers && receiver_id < 2 && receiver[receiver_id] != NULL) {
    receiver[receiver_id]->agc_gain = value;
    rx_set_agc(receiver[receiver_id]);
    if (display_sliders && active_receiver != NULL && receiver_id == active_receiver->id) {
      update_slider_agc_gain_scale();
    }
  }
  g_free(ag);
  return G_SOURCE_REMOVE;
}

int ext_set_agc_mode(void *data) {
  EXT_AGC_MODE_UPDATE *am = (EXT_AGC_MODE_UPDATE *) data;
  int receiver_id;
  int agc;
  if (am == NULL) {
    return G_SOURCE_REMOVE;
  }
  receiver_id = am->receiver_id;
  agc = am->agc;
  if (receiver_id >= 0 && receiver_id < receivers && receiver_id < 2 && receiver[receiver_id] != NULL
      && agc >= 0 && agc < AGC_LAST) {
    receiver[receiver_id]->agc = agc;
    rx_set_agc(receiver[receiver_id]);
    if (display_sliders && active_receiver != NULL && receiver_id == active_receiver->id) {
      update_slider_agc_btn();
    }
    g_idle_add(ext_vfo_update, NULL);
  }
  g_free(am);
  return G_SOURCE_REMOVE;
}

int ext_set_iq_samplerate(void *data) {
  int samplerate = GPOINTER_TO_INT(data);
  if (protocol != ORIGINAL_PROTOCOL && protocol != NEW_PROTOCOL) {
    return G_SOURCE_REMOVE;
  }
  if (samplerate != 48000 && samplerate != 96000 && samplerate != 192000 && samplerate != 384000) {
    return G_SOURCE_REMOVE;
  }
  if (protocol == NEW_PROTOCOL) {
    if (active_receiver != NULL) {
      rx_change_sample_rate(active_receiver, samplerate);
    }
  } else {
    radio_change_sample_rate(samplerate);
  }
  return G_SOURCE_REMOVE;
}

int ext_normalize_rx_filter_band(int mode, int *low, int *high) {
  int a;
  int b;
  int min_abs;
  int max_abs;
  if (low == NULL || high == NULL) {
    return 0;
  }
  switch (mode) {
  case modeLSB:
  case modeDIGL:
    a = abs(*low);
    b = abs(*high);
    min_abs = a < b ? a : b;
    max_abs = a > b ? a : b;
    *low = -max_abs;
    *high = -min_abs;
    break;
  case modeUSB:
  case modeDIGU:
    a = abs(*low);
    b = abs(*high);
    *low = a < b ? a : b;
    *high = a > b ? a : b;
    break;
  default:
    if (*low > *high) {
      int tmp = *low;
      *low = *high;
      *high = tmp;
    }
    break;
  }
  return *low < *high;
}

int ext_rx_filter_update(void *data) {
  EXT_RX_FILTER_UPDATE *fu = (EXT_RX_FILTER_UPDATE *) data;
  int receiver_id;
  int mode;
  int low;
  int high;
  int matched_filter = -1;
  if (fu == NULL) {
    return G_SOURCE_REMOVE;
  }
  receiver_id = fu->receiver_id;
  low = fu->low;
  high = fu->high;
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) {
    g_free(fu);
    return G_SOURCE_REMOVE;
  }
  mode = vfo[receiver_id].mode;
  if (mode < 0 || mode >= MODES) {
    g_free(fu);
    return G_SOURCE_REMOVE;
  }
  if (!ext_normalize_rx_filter_band(mode, &low, &high)) {
    g_free(fu);
    return G_SOURCE_REMOVE;
  }
  for (int i = 0; i < FILTERS; i++) {
    if (filters[mode][i].low == low && filters[mode][i].high == high) {
      matched_filter = i;
      break;
    }
  }
  if (matched_filter >= 0) {
    vfo[receiver_id].filter = matched_filter;
  } else {
    filters[mode][filterVar1].low = low;
    filters[mode][filterVar1].high = high;
    vfo[receiver_id].filter = filterVar1;
  }
  rx_filter_changed(receiver[receiver_id]);
  g_idle_add(ext_vfo_update, NULL);
  g_free(fu);
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

int ext_start_noise(void *data) {
  start_noise();
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

int ext_update_notch(void *data) {
  update_notch();
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
