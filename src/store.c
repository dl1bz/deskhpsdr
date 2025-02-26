/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT,  2016 - Steve Wilson, KA6S
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bandstack.h"
#include "band.h"
#include "filter.h"
#include "mode.h"
#include "property.h"
#include "store.h"
#include "store_menu.h"
#include "radio.h"
#include "ext.h"
#include "vfo.h"
#include "message.h"

MEM mem[NUM_OF_MEMORYS];  // This makes it a compile time option

void memSaveState() {
  for (int b = 0; b < NUM_OF_MEMORYS; b++) {
    SetPropI1("mem.%d.sat_mode", b,        mem[b].sat_mode);
    SetPropI1("mem.%d.freqA", b,           mem[b].frequency);
    SetPropI1("mem.%d.freqC", b,           mem[b].ctun_frequency);
    SetPropI1("mem.%d.ctun", b,            mem[b].ctun);
    SetPropI1("mem.%d.mode", b,            mem[b].mode);
    SetPropI1("mem.%d.filter", b,          mem[b].filter);
    SetPropI1("mem.%d.deviation", b,       mem[b].deviation);
    SetPropI1("mem.%d.band", b,            mem[b].bd);
    SetPropI1("mem.%d.altfreqA", b,        mem[b].alt_frequency);
    SetPropI1("mem.%d.altfreqC", b,        mem[b].alt_ctun_frequency);
    SetPropI1("mem.%d.altctun", b,         mem[b].alt_ctun);
    SetPropI1("mem.%d.altmode", b,         mem[b].alt_mode);
    SetPropI1("mem.%d.altfilter", b,       mem[b].alt_filter);
    SetPropI1("mem.%d.altdeviation", b,    mem[b].alt_deviation);
    SetPropI1("mem.%d.altband", b,         mem[b].alt_bd);
    SetPropI1("mem.%d.ctcss_enabled", b,   mem[b].ctcss_enabled);
    SetPropI1("mem.%d.ctcss", b,           mem[b].ctcss);
  }
}

void memRestoreState() {
  for (int b = 0; b < NUM_OF_MEMORYS; b++) {
    //
    // Set defaults
    //
    mem[b].sat_mode           = SAT_NONE;
    mem[b].frequency          = 28010000LL;
    mem[b].ctun_frequency     = 28010000LL;
    mem[b].ctun               = 0;
    mem[b].mode               = modeCWU;
    mem[b].filter             = filterF5;
    mem[b].deviation          = 2500;
    mem[b].bd                 = band10;
    mem[b].alt_frequency      = 28010000LL;
    mem[b].alt_ctun_frequency = 28010000LL;
    mem[b].alt_ctun           = 0;
    mem[b].alt_mode           = modeCWU;
    mem[b].alt_filter         = filterF5;
    mem[b].alt_deviation      = 2500;
    mem[b].alt_bd             = band10;
    mem[b].ctcss_enabled      = 0;
    mem[b].ctcss              = 0;
    //
    // Read from props
    //
    GetPropI1("mem.%d.sat_mode", b,        mem[b].sat_mode);
    GetPropI1("mem.%d.freqA", b,           mem[b].frequency);
    GetPropI1("mem.%d.freqC", b,           mem[b].ctun_frequency);
    GetPropI1("mem.%d.ctun", b,            mem[b].ctun);
    GetPropI1("mem.%d.mode", b,            mem[b].mode);
    GetPropI1("mem.%d.filter", b,          mem[b].filter);
    GetPropI1("mem.%d.deviation", b,       mem[b].deviation);
    GetPropI1("mem.%d.band", b,            mem[b].bd);
    GetPropI1("mem.%d.altfreqA", b,        mem[b].alt_frequency);
    GetPropI1("mem.%d.altfreqC", b,        mem[b].alt_ctun_frequency);
    GetPropI1("mem.%d.altctun", b,         mem[b].alt_ctun);
    GetPropI1("mem.%d.altmode", b,         mem[b].alt_mode);
    GetPropI1("mem.%d.altfilter", b,       mem[b].alt_filter);
    GetPropI1("mem.%d.altdeviation", b,    mem[b].alt_deviation);
    GetPropI1("mem.%d.altband", b,         mem[b].alt_bd);
    GetPropI1("mem.%d.ctcss_enabled", b,   mem[b].ctcss_enabled);
    GetPropI1("mem.%d.ctcss", b,           mem[b].ctcss);
  }
}

void recall_memory_slot(int index) {
  //
  // Recalling a memory slot is essentially the same as recalling a bandstack entry
  // so we just make use of code in vfo_bandstack_changed()
  //
  // If mode has changed: apply the settings stored with
  // the mode but keep the filter from the memory slot. This means
  // recalling a memory slot will change
  //
  // - noise reduction settings
  // - equalizer settings
  // - VFO step size
  // - TX compressor settings
  //
  //
  sat_mode    = mem[index].sat_mode;

  if (sat_mode == SAT_NONE) {
    int id      = active_receiver->id;
    int b       = mem[index].bd;
    int oldmode = vfo[id].mode;
    vfo[id].mode           = mem[index].mode;

    if (oldmode != vfo[id].mode) {
      vfo_apply_mode_settings(active_receiver);
      vfo[id].filter = mem[index].filter;
    }

    const BAND *band = band_get_band(b);
    const BANDSTACK *bandstack = bandstack_get_bandstack(b);
    vfo[id].band           = b;
    vfo[id].bandstack      = bandstack->current_entry;
    vfo[id].frequency      = mem[index].frequency;
    vfo[id].ctun_frequency = mem[index].ctun_frequency;
    vfo[id].ctun           = mem[index].ctun;
    vfo[id].filter         = mem[index].filter;
    vfo[id].deviation      = mem[index].deviation;
    vfo[id].lo             = band->frequencyLO + band->errorLO;
  } else {
    int b                  = mem[index].bd;
    int oldmode            = vfo[VFO_A].mode;
    vfo[VFO_A].mode        = mem[index].mode;

    if (oldmode != vfo[VFO_A].mode) {
      vfo_apply_mode_settings(receiver[0]);
    }

    const BAND *band = band_get_band(b);
    const BANDSTACK *bandstack = bandstack_get_bandstack(b);
    vfo[VFO_A].band           = b;
    vfo[VFO_A].bandstack      = bandstack->current_entry;
    vfo[VFO_A].frequency      = mem[index].frequency;
    vfo[VFO_A].ctun_frequency = mem[index].ctun_frequency;
    vfo[VFO_A].ctun           = mem[index].ctun;
    vfo[VFO_A].filter         = mem[index].filter;
    vfo[VFO_A].deviation      = mem[index].deviation;
    vfo[VFO_A].lo             = band->frequencyLO + band->errorLO;
    b                         = mem[index].alt_bd;
    oldmode                   = vfo[VFO_B].mode;
    vfo[VFO_B].mode           = mem[index].alt_mode;

    if (oldmode != vfo[VFO_B].mode && receivers > 1) {
      vfo_apply_mode_settings(receiver[1]);
    }

    band = band_get_band(b);
    bandstack = bandstack_get_bandstack(b);
    vfo[VFO_B].band           = b;
    vfo[VFO_B].bandstack      = bandstack->current_entry;
    vfo[VFO_B].frequency      = mem[index].alt_frequency;
    vfo[VFO_B].ctun_frequency = mem[index].alt_ctun_frequency;
    vfo[VFO_B].ctun           = mem[index].alt_ctun;
    vfo[VFO_B].filter         = mem[index].alt_filter;
    vfo[VFO_B].deviation      = mem[index].alt_deviation;
    vfo[VFO_B].lo             = band->frequencyLO + band->errorLO;
    // When recalling a SAT/RSAT memory slot, also apply the split and duplex setting
    radio_set_split(mem[index].split);

    if (!radio_is_transmitting()) {
      duplex = mem[index].duplex;
      g_idle_add(ext_set_duplex, NULL);
    }
  }

  if (can_transmit) {
    transmitter->ctcss_enabled = mem[index].ctcss_enabled;
    transmitter->ctcss         = mem[index].ctcss;
    tx_set_ctcss(transmitter);
  }

  vfo_vfos_changed();
}

void store_memory_slot(int index) {
  //
  // Store current frequency, mode, and filter in slot #index
  //
  mem[index].sat_mode = sat_mode;
  mem[index].split = split;
  mem[index].duplex = duplex;

  if (sat_mode == SAT_NONE) {
    int id = active_receiver->id;
    mem[index].frequency          = vfo[id].frequency;
    mem[index].ctun_frequency     = vfo[id].ctun_frequency;
    mem[index].ctun               = vfo[id].ctun;
    mem[index].mode               = vfo[id].mode;
    mem[index].filter             = vfo[id].filter;
    mem[index].deviation          = vfo[id].deviation;
    mem[index].bd                 = vfo[id].band;
  } else {
    mem[index].frequency          = vfo[VFO_A].frequency;
    mem[index].ctun_frequency     = vfo[VFO_A].ctun_frequency;
    mem[index].ctun               = vfo[VFO_A].ctun;
    mem[index].mode               = vfo[VFO_A].mode;
    mem[index].filter             = vfo[VFO_A].filter;
    mem[index].deviation          = vfo[VFO_A].deviation;
    mem[index].bd                 = vfo[VFO_A].band;
    mem[index].alt_frequency      = vfo[VFO_B].frequency;
    mem[index].alt_ctun_frequency = vfo[VFO_B].ctun_frequency;
    mem[index].alt_ctun           = vfo[VFO_B].ctun;
    mem[index].alt_mode           = vfo[VFO_B].mode;
    mem[index].alt_filter         = vfo[VFO_B].filter;
    mem[index].alt_deviation      = vfo[VFO_B].deviation;
    mem[index].alt_bd             = vfo[VFO_B].band;
  }

  if (can_transmit) {
    mem[index].ctcss_enabled = transmitter->ctcss_enabled;
    mem[index].ctcss = transmitter->ctcss;
  }
}
