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

#ifndef _BAND_H
#define _BAND_H

#include <gtk/gtk.h>
#include "bandstack.h"

enum _band_enum {
  band136 = 0,
  band472,
  band160,
  band80,
  band60,
  band40,
  band30,
  band20,
  band17,
  band15,
  band12,
  band10,
  band6,
  band70,
  band144,
  band220,
  band430,
  band902,
  band1240,
  band2300,
  band3400,
  bandAIR,
  bandWWV,
  bandGen,
  BANDS
};

#define XVTRS 10

/* --------------------------------------------------------------------------*/
/**
* @brief Band definition
*/
struct _BAND {
  char title[16];                 // band title
  BANDSTACK *bandstack;           // pointer to band stack
  unsigned char OCrx;             // OC bit pattern for RX
  unsigned char OCtx;             // OC bit pattern for TX
  int gain;                       // band dependent RX gain offset
  int alexRxAntenna;              // if ALEX: RX antenna
  int alexTxAntenna;              // if ALEX: TX antenna
  int alexAttenuation;            // if ALEX: attenuator (0/1/2/3 for 0/10/20/30 dB)
  double pa_calibration;          // PA calibration value for this band
  long long frequencyMin;         // lower band edge
  long long frequencyMax;         // upper band edge
  long long frequencyLO;          // frequency offset
  long long errorLO;              // band dependent LO frequency correction
  int disablePA;                  // if 1, PA is disabled for this band
};

//
// Note that several entries are compile-time constants for non-XVTR bands,
// that is, there is no GUI to change then, and they are not read from the
// props file:
//
// title, frequencyMin, frequencyMax, frequencyLO, errorLO, disablePA, gain
//

typedef struct _BAND BAND;

struct _CHANNEL {
  long long frequency;
  long long width;
};

typedef struct _CHANNEL CHANNEL;

#define UK_CHANNEL_ENTRIES 11
#define OTHER_CHANNEL_ENTRIES 5
#define WRC15_CHANNEL_ENTRIES 1

extern int channel_entries;
extern CHANNEL *band_channels_60m;

//extern CHANNEL band_channels_60m_UK[UK_CHANNEL_ENTRIES];
//extern CHANNEL band_channels_60m_OTHER[OTHER_CHANNEL_ENTRIES];
//extern CHANNEL band_channels_60m_WRC15[WRC15_CHANNEL_ENTRIES];

extern BAND *band_get_band(int b);
extern int get_band_from_frequency(long long f);

extern BANDSTACK *bandstack_get_bandstack(int band);

extern void radio_change_region(int region);

extern void bandSaveState(void);
extern void bandRestoreState(void);

char* getFrequencyInfo(long long frequency, int filter_low, int filter_high);
int TransmitAllowed(void);

extern void band_minus(int id);
extern void band_plus(int id);
#endif
