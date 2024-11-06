/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#include "sliders.h"
#include "filter.h"
#include "receiver.h"
#include "vfo.h"
#include "radio.h"
#include "property.h"
#include "actions.h"
#include "mode.h"
#include "message.h"
#include "ext.h"

//
// mode-specific defaults for the Var1 and Var2 filters
// These are now stored separately to allow for a
// "set variable filters to default" action
//
#define  LSB_VAR1_DEFAULT_LOW  -2850
#define  LSB_VAR1_DEFAULT_HIGH  -150
#define  LSB_VAR2_DEFAULT_LOW  -2850
#define  LSB_VAR2_DEFAULT_HIGH  -150
#define DIGL_VAR1_DEFAULT_LOW  -3000
#define DIGL_VAR1_DEFAULT_HIGH     0
#define DIGL_VAR2_DEFAULT_LOW  -2000
#define DIGL_VAR2_DEFAULT_HIGH -1000
#define  USB_VAR1_DEFAULT_LOW    150
#define  USB_VAR1_DEFAULT_HIGH  2850
#define  USB_VAR2_DEFAULT_LOW    150
#define  USB_VAR2_DEFAULT_HIGH  2850
#define DIGU_VAR1_DEFAULT_LOW      0
#define DIGU_VAR1_DEFAULT_HIGH  3000
#define DIGU_VAR2_DEFAULT_LOW   1000
#define DIGU_VAR2_DEFAULT_HIGH  2000
#define  CWL_VAR1_DEFAULT_LOW   -125
#define  CWL_VAR1_DEFAULT_HIGH   125
#define  CWL_VAR2_DEFAULT_LOW   -250
#define  CWL_VAR2_DEFAULT_HIGH   250
#define  CWU_VAR1_DEFAULT_LOW   -125
#define  CWU_VAR1_DEFAULT_HIGH   125
#define  CWU_VAR2_DEFAULT_LOW   -250
#define  CWU_VAR2_DEFAULT_HIGH   250
#define   AM_VAR1_DEFAULT_LOW  -3300
#define   AM_VAR1_DEFAULT_HIGH  3300
#define   AM_VAR2_DEFAULT_LOW  -3300
#define   AM_VAR2_DEFAULT_HIGH  3300
#define  SAM_VAR1_DEFAULT_LOW  -3300
#define  SAM_VAR1_DEFAULT_HIGH  3300
#define  SAM_VAR2_DEFAULT_LOW  -3300
#define  SAM_VAR2_DEFAULT_HIGH  3300
#define  DSB_VAR1_DEFAULT_LOW  -3300
#define  DSB_VAR1_DEFAULT_HIGH  3300
#define  DSB_VAR2_DEFAULT_LOW  -3300
#define  DSB_VAR2_DEFAULT_HIGH  3300
#define SPEC_VAR1_DEFAULT_LOW  -3300
#define SPEC_VAR1_DEFAULT_HIGH  3300
#define SPEC_VAR2_DEFAULT_LOW  -3300
#define SPEC_VAR2_DEFAULT_HIGH  3300
#define  DRM_VAR1_DEFAULT_LOW  -3300
#define  DRM_VAR1_DEFAULT_HIGH  3300
#define  DRM_VAR2_DEFAULT_LOW  -3300
#define  DRM_VAR2_DEFAULT_HIGH  3300

static FILTER filterLSB[FILTERS] = {
  {-5150, -150, "5.0k"},
  {-4550, -150, "4.4k"},
  {-3950, -150, "3.8k"},
  {-3450, -150, "3.3k"},
  {-3050, -150, "2.9k"},
  {-2850, -150, "2.7k"},
  {-2550, -150, "2.4k"},
  {-2250, -150, "2.1k"},
  {-1950, -150, "1.8k"},
  {-1150, -150, "1.0k"},
  {LSB_VAR1_DEFAULT_LOW, LSB_VAR1_DEFAULT_HIGH, "Var1"},
  {LSB_VAR2_DEFAULT_LOW, LSB_VAR2_DEFAULT_HIGH, "Var2"}
};

static FILTER filterUSB[FILTERS] = {
  {150, 5150, "5.0k"},
  {150, 4550, "4.4k"},
  {150, 3950, "3.8k"},
  {150, 3450, "3.3k"},
  {150, 3050, "2.9k"},
  {150, 2850, "2.7k"},
  {150, 2550, "2.4k"},
  {150, 2250, "2.1k"},
  {150, 1950, "1.8k"},
  {150, 1150, "1.0k"},
  {USB_VAR1_DEFAULT_LOW, USB_VAR1_DEFAULT_HIGH, "Var1"},
  {USB_VAR2_DEFAULT_LOW, USB_VAR2_DEFAULT_HIGH, "Var2"}
};

//
// DigiMode Filters up to 3000 Hz wide are centered
// around 1500, the broader ones start at
// zero (this also holds for DIGU).
//
static FILTER filterDIGL[FILTERS] = {
  {-5000,    0, "5.0k"},
  {-4000,    0, "4.0k"},
  {-3000,    0, "3.0k"},
  {-2750, -250, "2.5k"},
  {-2500, -500, "2.0k"},
  {-2250, -750, "1.5k"},
  {-2000, -1000, "1.0k"},
  {-1875, -1125, "750"},
  {-1750, -1250, "500"},
  {-1625, -1375, "250"},
  {DIGL_VAR1_DEFAULT_LOW, DIGL_VAR1_DEFAULT_HIGH, "Var1"},
  {DIGL_VAR2_DEFAULT_LOW, DIGL_VAR2_DEFAULT_HIGH, "Var2"}
};

static FILTER filterDIGU[FILTERS] = {
  {   0, 5000, "5.0k"},
  {   0, 4000, "4.0k"},
  {   0, 3000, "3.0k"},
  { 250, 2750, "2.5k"},
  { 500, 2500, "2.0k"},
  { 750, 2250, "1.5k"},
  {1000, 2000, "1.0k"},
  {1125, 1875, "750"},
  {1250, 1750, "500"},
  {1375, 1625, "250"},
  {DIGU_VAR1_DEFAULT_LOW, DIGU_VAR1_DEFAULT_HIGH, "Var1"},
  {DIGU_VAR2_DEFAULT_LOW, DIGU_VAR2_DEFAULT_HIGH, "Var2"}
};

//
// CW filter edges refer to a CW signal at zero frequency
//
static FILTER filterCWL[FILTERS] = {
  {-500, 500, "1.0k"},
  {-400, 400, "800"},
  {-375, 375, "750"},
  {-300, 300, "600"},
  {-250, 250, "500"},
  {-200, 200, "400"},
  {-125, 125, "250"},
  {-50, 50, "100"},
  {-25, 25, "50"},
  {-13, 13, "25"},
  {CWL_VAR1_DEFAULT_LOW, CWL_VAR1_DEFAULT_HIGH, "Var1"},
  {CWL_VAR2_DEFAULT_LOW, CWL_VAR2_DEFAULT_HIGH, "Var2"}
};

static FILTER filterCWU[FILTERS] = {
  {-500, 500, "1.0k"},
  {-400, 400, "800"},
  {-375, 375, "750"},
  {-300, 300, "600"},
  {-250, 250, "500"},
  {-200, 200, "400"},
  {-125, 125, "250"},
  {-50, 50, "100"},
  {-25, 25, "50"},
  {-13, 13, "25"},
  {CWU_VAR1_DEFAULT_LOW, CWU_VAR1_DEFAULT_HIGH, "Var1"},
  {CWU_VAR2_DEFAULT_LOW, CWU_VAR2_DEFAULT_HIGH, "Var2"}
};

//
// DSB, AM, SAM, SPEC and DRM  filters normally have low/high edges
// that only differ in sign
//
static FILTER filterDSB[FILTERS] = {
  {-8000, 8000, "16k"},
  {-6000, 6000, "12k"},
  {-5000, 5000, "10k"},
  {-4000, 4000, "8k"},
  {-3300, 3300, "6.6k"},
  {-2600, 2600, "5.2k"},
  {-2000, 2000, "4.0k"},
  {-1550, 1550, "3.1k"},
  {-1450, 1450, "2.9k"},
  {-1200, 1200, "2.4k"},
  {DSB_VAR1_DEFAULT_LOW, DSB_VAR1_DEFAULT_HIGH, "Var1"},
  {DSB_VAR2_DEFAULT_LOW, DSB_VAR2_DEFAULT_HIGH, "Var2"}
};

static FILTER filterAM[FILTERS] = {
  {-8000, 8000, "16k"},
  {-6000, 6000, "12k"},
  {-5000, 5000, "10k"},
  {-4000, 4000, "8k"},
  {-3300, 3300, "6.6k"},
  {-2600, 2600, "5.2k"},
  {-2000, 2000, "4.0k"},
  {-1550, 1550, "3.1k"},
  {-1450, 1450, "2.9k"},
  {-1200, 1200, "2.4k"},
  {AM_VAR1_DEFAULT_LOW, AM_VAR1_DEFAULT_HIGH, "Var1"},
  {AM_VAR2_DEFAULT_LOW, AM_VAR2_DEFAULT_HIGH, "Var2"}
};

static FILTER filterSAM[FILTERS] = {
  {-8000, 8000, "16k"},
  {-6000, 6000, "12k"},
  {-5000, 5000, "10k"},
  {-4000, 4000, "8k"},
  {-3300, 3300, "6.6k"},
  {-2600, 2600, "5.2k"},
  {-2000, 2000, "4.0k"},
  {-1550, 1550, "3.1k"},
  {-1450, 1450, "2.9k"},
  {-1200, 1200, "2.4k"},
  {SAM_VAR1_DEFAULT_LOW, SAM_VAR1_DEFAULT_HIGH, "Var1"},
  {SAM_VAR2_DEFAULT_LOW, SAM_VAR2_DEFAULT_HIGH, "Var2"}
};

static FILTER filterSPEC[FILTERS] = {
  {-8000, 8000, "16k"},
  {-6000, 6000, "12k"},
  {-5000, 5000, "10k"},
  {-4000, 4000, "8k"},
  {-3300, 3300, "6.6k"},
  {-2600, 2600, "5.2k"},
  {-2000, 2000, "4.0k"},
  {-1550, 1550, "3.1k"},
  {-1450, 1450, "2.9k"},
  {-1200, 1200, "2.4k"},
  {SPEC_VAR1_DEFAULT_LOW, SPEC_VAR1_DEFAULT_HIGH, "Var1"},
  {SPEC_VAR2_DEFAULT_LOW, SPEC_VAR2_DEFAULT_HIGH, "Var2"}
};

static FILTER filterDRM[FILTERS] = {
  {-8000, 8000, "16k"},
  {-6000, 6000, "12k"},
  {-5000, 5000, "10k"},
  {-4000, 4000, "8k"},
  {-3300, 3300, "6.6k"},
  {-2600, 2600, "5.2k"},
  {-2000, 2000, "4.0k"},
  {-1550, 1550, "3.1k"},
  {-1450, 1450, "2.9k"},
  {-1200, 1200, "2.4k"},
  {DRM_VAR1_DEFAULT_LOW, DRM_VAR1_DEFAULT_HIGH, "Var1"},
  {DRM_VAR2_DEFAULT_LOW, DRM_VAR2_DEFAULT_HIGH, "Var2"}
};

//
// This FMN filter edges are nowhere used, this data is
// just there to avoid voids.
//
static FILTER filterFMN[FILTERS] = {
  {0, 0, "FM"},
  {0, 0, "FM"},
  {0, 0, "FM"},
  {0, 0, "FM"},
  {0, 0, "FM"},
  {0, 0, "FM"},
  {0, 0, "FM"},
  {0, 0, "FM"},
  {0, 0, "FM"},
  {0, 0, "FM"},
  {0, 0, "FM"},
  {0, 0, "FM"}
};

//
// The filters in this list must be in exactly the same
// order as the modes in enum mode_list (see mode.h)!
//
FILTER *filters[MODES] = {
  filterLSB,
  filterUSB,
  filterDSB,
  filterCWL,
  filterCWU,
  filterFMN,
  filterAM,
  filterDIGU,
  filterSPEC,
  filterDIGL,
  filterSAM,
  filterDRM
};

//
// These arrays contain the default low/high filter edges
// for the Var1/Var2 filters for each mode.
// There is now a "set default" action that restores the
// default.
// The order of modes must be exactly as in the mode_list enum.
//
const int var1_default_low[MODES] = {
  LSB_VAR1_DEFAULT_LOW,
  USB_VAR1_DEFAULT_LOW,
  DSB_VAR1_DEFAULT_LOW,
  CWL_VAR1_DEFAULT_LOW,
  CWU_VAR1_DEFAULT_LOW,
  0,
  AM_VAR1_DEFAULT_LOW,
  DIGU_VAR1_DEFAULT_LOW,
  SPEC_VAR1_DEFAULT_LOW,
  DIGL_VAR1_DEFAULT_LOW,
  SAM_VAR1_DEFAULT_LOW,
  DRM_VAR1_DEFAULT_LOW
};

const int var1_default_high[MODES] = {
  LSB_VAR1_DEFAULT_HIGH,
  USB_VAR1_DEFAULT_HIGH,
  DSB_VAR1_DEFAULT_HIGH,
  CWL_VAR1_DEFAULT_HIGH,
  CWU_VAR1_DEFAULT_HIGH,
  0,
  AM_VAR1_DEFAULT_HIGH,
  DIGU_VAR1_DEFAULT_HIGH,
  SPEC_VAR1_DEFAULT_HIGH,
  DIGL_VAR1_DEFAULT_HIGH,
  SAM_VAR1_DEFAULT_HIGH,
  DRM_VAR1_DEFAULT_HIGH
};

const int var2_default_low[MODES] = {
  LSB_VAR2_DEFAULT_LOW,
  USB_VAR2_DEFAULT_LOW,
  DSB_VAR2_DEFAULT_LOW,
  CWL_VAR2_DEFAULT_LOW,
  CWU_VAR2_DEFAULT_LOW,
  0,
  AM_VAR2_DEFAULT_LOW,
  DIGU_VAR2_DEFAULT_LOW,
  SPEC_VAR2_DEFAULT_LOW,
  DIGL_VAR2_DEFAULT_LOW,
  SAM_VAR2_DEFAULT_LOW,
  DRM_VAR2_DEFAULT_LOW
};

const int var2_default_high[MODES] = {
  LSB_VAR2_DEFAULT_HIGH,
  USB_VAR2_DEFAULT_HIGH,
  DSB_VAR2_DEFAULT_HIGH,
  CWL_VAR2_DEFAULT_HIGH,
  CWU_VAR2_DEFAULT_HIGH,
  0,
  AM_VAR2_DEFAULT_HIGH,
  DIGU_VAR2_DEFAULT_HIGH,
  SPEC_VAR2_DEFAULT_HIGH,
  DIGL_VAR2_DEFAULT_HIGH,
  SAM_VAR2_DEFAULT_HIGH,
  DRM_VAR2_DEFAULT_HIGH
};

void filterSaveState() {
  // save the Var1 and Var2 settings
  SetPropI0("filter.lsb.var1.low",            filterLSB[filterVar1].low);
  SetPropI0("filter.lsb.var1.high",           filterLSB[filterVar1].high);
  SetPropI0("filter.lsb.var2.low",            filterLSB[filterVar2].low);
  SetPropI0("filter.lsb.var2.high",           filterLSB[filterVar2].high);
  SetPropI0("filter.digl.var1.low",           filterDIGL[filterVar1].low);
  SetPropI0("filter.digl.var1.high",          filterDIGL[filterVar1].high);
  SetPropI0("filter.digl.var2.low",           filterDIGL[filterVar2].low);
  SetPropI0("filter.digl.var2.high",          filterDIGL[filterVar2].high);
  SetPropI0("filter.cwl.var1.low",            filterCWL[filterVar1].low);
  SetPropI0("filter.cwl.var1.high",           filterCWL[filterVar1].high);
  SetPropI0("filter.cwl.var2.low",            filterCWL[filterVar2].low);
  SetPropI0("filter.cwl.var2.high",           filterCWL[filterVar2].high);
  SetPropI0("filter.usb.var1.low",            filterUSB[filterVar1].low);
  SetPropI0("filter.usb.var1.high",           filterUSB[filterVar1].high);
  SetPropI0("filter.usb.var2.low",            filterUSB[filterVar2].low);
  SetPropI0("filter.usb.var2.high",           filterUSB[filterVar2].high);
  SetPropI0("filter.digu.var1.low",           filterDIGU[filterVar1].low);
  SetPropI0("filter.digu.var1.high",          filterDIGU[filterVar1].high);
  SetPropI0("filter.digu.var2.low",           filterDIGU[filterVar2].low);
  SetPropI0("filter.digu.var2.high",          filterDIGU[filterVar2].high);
  SetPropI0("filter.cwu.var1.low",            filterCWU[filterVar1].low);
  SetPropI0("filter.cwu.var1.high",           filterCWU[filterVar1].high);
  SetPropI0("filter.cwu.var2.low",            filterCWU[filterVar2].low);
  SetPropI0("filter.cwu.var2.high",           filterCWU[filterVar2].high);
  SetPropI0("filter.am.var1.low",             filterAM[filterVar1].low);
  SetPropI0("filter.am.var1.high",            filterAM[filterVar1].high);
  SetPropI0("filter.am.var2.low",             filterAM[filterVar2].low);
  SetPropI0("filter.am.var2.high",            filterAM[filterVar2].high);
  SetPropI0("filter.sam.var1.low",            filterSAM[filterVar1].low);
  SetPropI0("filter.sam.var1.high",           filterSAM[filterVar1].high);
  SetPropI0("filter.sam.var2.low",            filterSAM[filterVar2].low);
  SetPropI0("filter.sam.var2.high",           filterSAM[filterVar2].high);
  SetPropI0("filter.dsb.var1.low",            filterDSB[filterVar1].low);
  SetPropI0("filter.dsb.var1.high",           filterDSB[filterVar1].high);
  SetPropI0("filter.dsb.var2.low",            filterDSB[filterVar2].low);
  SetPropI0("filter.dsb.var2.high",           filterDSB[filterVar2].high);
}

void filterRestoreState() {
  GetPropI0("filter.lsb.var1.low",            filterLSB[filterVar1].low);
  GetPropI0("filter.lsb.var1.high",           filterLSB[filterVar1].high);
  GetPropI0("filter.lsb.var2.low",            filterLSB[filterVar2].low);
  GetPropI0("filter.lsb.var2.high",           filterLSB[filterVar2].high);
  GetPropI0("filter.digl.var1.low",           filterDIGL[filterVar1].low);
  GetPropI0("filter.digl.var1.high",          filterDIGL[filterVar1].high);
  GetPropI0("filter.digl.var2.low",           filterDIGL[filterVar2].low);
  GetPropI0("filter.digl.var2.high",          filterDIGL[filterVar2].high);
  GetPropI0("filter.cwl.var1.low",            filterCWL[filterVar1].low);
  GetPropI0("filter.cwl.var1.high",           filterCWL[filterVar1].high);
  GetPropI0("filter.cwl.var2.low",            filterCWL[filterVar2].low);
  GetPropI0("filter.cwl.var2.high",           filterCWL[filterVar2].high);
  GetPropI0("filter.usb.var1.low",            filterUSB[filterVar1].low);
  GetPropI0("filter.usb.var1.high",           filterUSB[filterVar1].high);
  GetPropI0("filter.usb.var2.low",            filterUSB[filterVar2].low);
  GetPropI0("filter.usb.var2.high",           filterUSB[filterVar2].high);
  GetPropI0("filter.digu.var1.low",           filterDIGU[filterVar1].low);
  GetPropI0("filter.digu.var1.high",          filterDIGU[filterVar1].high);
  GetPropI0("filter.digu.var2.low",           filterDIGU[filterVar2].low);
  GetPropI0("filter.digu.var2.high",          filterDIGU[filterVar2].high);
  GetPropI0("filter.cwu.var1.low",            filterCWU[filterVar1].low);
  GetPropI0("filter.cwu.var1.high",           filterCWU[filterVar1].high);
  GetPropI0("filter.cwu.var2.low",            filterCWU[filterVar2].low);
  GetPropI0("filter.cwu.var2.high",           filterCWU[filterVar2].high);
  GetPropI0("filter.am.var1.low",             filterAM[filterVar1].low);
  GetPropI0("filter.am.var1.high",            filterAM[filterVar1].high);
  GetPropI0("filter.am.var2.low",             filterAM[filterVar2].low);
  GetPropI0("filter.am.var2.high",            filterAM[filterVar2].high);
  GetPropI0("filter.sam.var1.low",            filterSAM[filterVar1].low);
  GetPropI0("filter.sam.var1.high",           filterSAM[filterVar1].high);
  GetPropI0("filter.sam.var2.low",            filterSAM[filterVar2].low);
  GetPropI0("filter.sam.var2.high",           filterSAM[filterVar2].high);
  GetPropI0("filter.dsb.var1.low",            filterDSB[filterVar1].low);
  GetPropI0("filter.dsb.var1.high",           filterDSB[filterVar1].high);
  GetPropI0("filter.dsb.var2.low",            filterDSB[filterVar2].low);
  GetPropI0("filter.dsb.var2.high",           filterDSB[filterVar2].high);
}

//
// This function is a no-op unless the vfo referenced uses a Var1 or Var2 filter
//
void filter_cut_default(int id) {
  int mode = vfo[id].mode;
  int f = vfo[id].filter;
  FILTER *filter = &(filters[mode][f]);

  switch (f) {
  case filterVar1:
    filter->low = var1_default_low[mode];
    filter->high = var1_default_high[mode];
    vfo_filter_changed(f);
    break;

  case filterVar2:
    filter->low = var2_default_low[mode];
    filter->high = var2_default_high[mode];
    vfo_filter_changed(f);
    break;

  default:
    // do nothing
    break;
  }

  g_idle_add(ext_vfo_update, NULL);
}

//
// This function is a no-op unless the vfo referenced uses a Var1 or Var2 filter
// The notion of "high" and "low" is referenced to the audio, that is,
// they have to be reversed for LSB/DIGU.
//
void filter_cut_changed(int id, int action, int increment) {
  int mode = vfo[id].mode;
  int f = vfo[id].filter;

  if (mode == modeFMN) {
    return;
  }

  if (f == filterVar1 || f == filterVar2) {
    FILTER *filter = &(filters[mode][f]);

    //
    // Note that set_filter_cut_high/low *only* puts a scale on the screen
    //
    switch (action) {
    case FILTER_CUT_HIGH:
      switch (mode) {
      case modeLSB:
      case modeDIGL:
        filter->low -= increment * 25;

        if (filter->low > 0) { filter->low = 0; }

        if (filter->low > filter->high) { filter->low = filter->high; }

        set_filter_cut_high(id, -filter->low);
        break;

      case modeCWL:
        filter->low -= increment * 5;

        if (filter->low > 0) { filter->low = 0; }

        if (filter->low > filter->high) { filter->low = filter->high; }

        set_filter_cut_high(id, -filter->low);
        break;

      case  modeCWU:
        filter->high += increment * 5;

        if (filter->high < 0) { filter->high = 0; }

        if (filter->high < filter->low) { filter->high = filter->low; }

        set_filter_cut_high(id, filter->high);
        break;

      case modeUSB:
      case modeDIGU:
        filter->high += increment * 25;

        if (filter->high < 0) { filter->high = 0; }

        if (filter->high < filter->low) { filter->high = filter->low; }

        set_filter_cut_high(id, filter->high);
        break;

      default:
        filter->high += increment * 50;

        if (filter->high < 0) { filter->high = 0; }

        if (filter->high < filter->low) { filter->high = filter->low; }

        set_filter_cut_high(id, filter->high);
        break;
      }

      break;

    case FILTER_CUT_LOW:
      switch (mode) {
      case modeLSB:
      case modeDIGL:
        filter->high -= increment * 25;

        if (filter->high > 0) { filter->high = 0; }

        if (filter->high < filter->low) { filter->high = filter->low; }

        set_filter_cut_low(id, -filter->high);
        break;

      case modeCWL:
        filter->high -= increment * 5;

        if (filter->high < 0) { filter->high = 0; }

        if (filter->high < filter->low) { filter->high = filter->low; }

        set_filter_cut_low(id, -filter->high);
        break;

      case modeCWU:
        filter->low += increment * 5;

        if (filter->low > 0) { filter->low = 0; }

        if (filter->low > filter->high) { filter->low = filter->high; }

        set_filter_cut_low(id, filter->low);
        break;

      case modeUSB:
      case modeDIGU:
        filter->low += increment * 25;

        if (filter->low < 0) { filter->low = 0; }

        if (filter->low > filter->high) { filter->low = filter->high; }

        set_filter_cut_low(id, filter->low);
        break;

      default:
        filter->low += increment * 50;

        if (filter->low > 0) { filter->low = 0; }

        if (filter->low > filter->high) { filter->low = filter->high; }

        set_filter_cut_low(id, filter->low);
        break;
      }

      break;

    default:
      break;
    }

    vfo_filter_changed(f);
    g_idle_add(ext_vfo_update, NULL);
  }
}

//
// This function is a no-op unless the vfo referenced uses a Var1 or Var2 filter
// Note the changed width affects the low/high cut depending on the mode
// For USB/LSB only the high-audio-frequency cut is affected.
//
void filter_width_changed(int id, int increment) {
  int mode = vfo[id].mode;
  int f = vfo[id].filter;
  FILTER *filter = &(filters[mode][f]);

  if (mode == modeFMN) {
    return;
  }

  if (f == filterVar1 || f == filterVar2) {
    switch (mode) {
    case modeDIGL:
      if (filter->high < -500) {
        // change both high and low
        filter->low -= increment * 13;
        filter->high += increment * 12;

        if (filter->low > filter->high) { filter->low = filter->high; }

        break;
      }

      __attribute__((fallthrough));

    case modeLSB:
      // only change high-audio-cut
      filter->low -= increment * 25;

      if (filter->low > filter->high) { filter->low = filter->high; }

      break;

    case modeDIGU:
      if (filter->low > 500) {
        // change both high and low
        filter->low -= increment * 12;
        filter->high += increment * 13;

        if (filter->high < filter->low) { filter->high = filter->low; }

        break;
      }

      __attribute__((fallthrough));

    case modeUSB:
      // only change high-audio-cut
      filter->high += increment * 25;

      if (filter->high < filter->low) { filter->high = filter->low; }

      break;

    case modeCWL:
    case modeCWU:
      filter->low  -= increment * 5;
      filter->high += increment * 5;

      if (filter->low > filter->high) {
        int mid = (filter->low + filter->high) / 2;
        filter->low = mid;
        filter->high = mid;
      }

      break;

    default:
      filter->low  -= increment * 50;
      filter->high += increment * 50;

      if (filter->low > filter->high) {
        int mid = (filter->low + filter->high) / 2;
        filter->low = mid;
        filter->high = mid;
      }

      break;
    }

    vfo_filter_changed(f);
    // this *only* displays a scale on the screen
    set_filter_width(id, filter->high - filter->low);
    g_idle_add(ext_vfo_update, NULL);
  }
}

//
// This function is a no-op unless the vfo referenced uses a Var1 or Var2 filter
// The shift is defined as the difference of the filter passband mid-point
// w.r.t. 1500 (USB, DIGU), -1500 (LSB, DIGL), SideToneFreq (CWU), -SideToneFreq (CWL)
// or zero (everything else).
//
// This function changes the shift but leaves the width unchanged
//
void filter_shift_changed(int id, int increment) {
  int mode = vfo[id].mode;
  int f = vfo[id].filter;

  if (mode == modeFMN) {
    return;
  }

  if (f == filterVar1 || f == filterVar2) {
    FILTER *filter = &(filters[mode][f]);
    int fac;
    int ref;
    int mid = (filter->high + filter->low) / 2;
    int wid = (filter->high - filter->low);
    int shft;
    int sgn = 1;

    switch (mode) {
    case modeLSB:
    case modeDIGL:
      fac  = 25;
      ref  = -1500;
      sgn  = -1;
      break;

    case modeUSB:
    case modeDIGU:
      fac  = 25;
      ref  = 1500;
      break;

    case modeCWL:
      fac  = 5;
      ref  = 0;
      sgn  = -1;
      break;

    case modeCWU:
      fac  = 5;
      ref = 0;
      break;

    default:
      fac  = 50;
      ref = 0;
      break;
    }

    shft = mid - ref;
    shft += increment * fac * sgn;
    filter->low =  ref + shft - wid / 2;
    filter->high = ref + shft + wid / 2;
    set_filter_shift(id, sgn * shft);
    vfo_filter_changed(f);
    g_idle_add(ext_vfo_update, NULL);
  }
}
