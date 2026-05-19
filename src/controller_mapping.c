/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>

#include "band.h"
#include "channel.h"
#include "discovered.h"
#include "mode.h"
#include "filter.h"
#include "bandstack.h"
#include "toolbar.h"
#include "radio.h"
#include "toolbar.h"
#include "main.h"
#include "property.h"
#include "vfo.h"
#include "new_menu.h"
#include "encoder_menu.h"
#include "diversity_menu.h"
#include "actions.h"
#include "controller_mapping.h"
#include "ext.h"
#include "sliders.h"
#include "new_protocol.h"
#include "zoompan.h"
#include "iambic.h"
#include "message.h"

///////////////////////////////////////////////////////////////////////////
//
// As of now, the code is not yet instrumented to work with GPIOV2
//
///////////////////////////////////////////////////////////////////////////
//
// for controllers which have spare GPIO lines,
// these lines can be associated to certain
// functions, namely
//
// CWL:      input:  left paddle for internal (iambic) keyer
// CWR:      input:  right paddle for internal (iambic) keyer
// CWKEY:    input:  key-down from external keyer
// PTTIN:    input:  PTT from external keyer or microphone
// PTTOUT:   output: PTT output (indicating TX status)
//
// a value < 0 indicates "do not use". All inputs are active-low,
// but PTTOUT is active-high
//
// Avoid using GPIO lines 18, 19, 20, 21 since they are used for I2S
// by some GPIO-connected audio output "hats"
//
//

enum {
  TOP_ENCODER,
  BOTTOM_ENCODER
};

enum {
  A,
  B
};

#define DIR_NONE 0x0
// Clockwise step.
#define DIR_CW 0x10
// Anti-clockwise step.
#define DIR_CCW 0x20

//
// Encoder states for a "full cycle"
//
#define R_START     0x00
#define R_CW_FINAL  0x01
#define R_CW_BEGIN  0x02
#define R_CW_NEXT   0x03
#define R_CCW_BEGIN 0x04
#define R_CCW_FINAL 0x05
#define R_CCW_NEXT  0x06

//
// Encoder states for a "half cycle"
//
#define R_START1    0x07
#define R_START0    0x08
#define R_CW_BEG1   0x09
#define R_CW_BEG0   0x0A
#define R_CCW_BEG1  0x0B
#define R_CCW_BEG0  0x0C

//
// Few general remarks on the state machine:
// - if the levels do not change, the machinestate does not change
// - if there is bouncing on one input line, the machine oscillates
//   between two "adjacent" states but generates at most one tick
// - if both input lines change level, move to a suitable new
//   starting point but do not generate a tick
// - if one or both of the AB lines are inverted, the same cycles
//   are passed but with a different starting point. Therefore,
//   it still works.
//
guchar encoder_state_table[13][4] = {
  //
  // A "full cycle" has the following state changes
  // (first line: line levels AB, 1=pressed, 0=released,
  //  2nd   line: state names
  //
  // clockwise:  11   -->   10   -->    00    -->    01     -->  11
  //            Start --> CWbeg  -->  CWnext  -->  CWfinal  --> Start
  //
  // ccw:        11   -->   01    -->   00     -->   10      -->  11
  //            Start --> CCWbeg  --> CCWnext  --> CCWfinal  --> Start
  //
  // Emit the "tick" when moving from "final" to "start".
  //
  //                   00           10           01          11
  // -----------------------------------------------------------------------------
  /* R_START     */ {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
  /* R_CW_FINAL  */ {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
  /* R_CW_BEGIN  */ {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
  /* R_CW_NEXT   */ {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
  /* R_CCW_BEGIN */ {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
  /* R_CCW_FINAL */ {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
  /* R_CCW_NEXT  */ {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
  //
  // The same sequence can be interpreted as two "half cycles"
  //
  // clockwise1:   11    -->   10   -->   00
  //             Start1  --> CWbeg1 --> Start0
  //
  // clockwise2:   00    -->   01   -->   11
  //             Start0  --> CWbeg0 --> Start1
  //
  // ccw1:         11    -->   01    -->   00
  //             Start1  --> CCWbeg1 --> Start0
  //
  // ccw2:         00    -->   10    -->   11
  //             Start0  --> CCWbeg0 --> Start1
  //
  // If both lines change, this is interpreted as a two-step move
  // without changing the orientation and without emitting a "tick".
  //
  // Emit the "tick" each time when moving from "beg" to "start".
  //
  //                   00                    10          01         11
  // -----------------------------------------------------------------------------
  /* R_START1    */ {R_START0,           R_CW_BEG1,  R_CCW_BEG1, R_START1},
  /* R_START0    */ {R_START0,           R_CCW_BEG0, R_CW_BEG0,  R_START1},
  /* R_CW_BEG1   */ {R_START0 | DIR_CW,  R_CW_BEG1,  R_CW_BEG0,  R_START1},
  /* R_CW_BEG0   */ {R_START0,           R_CW_BEG1,  R_CW_BEG0,  R_START1 | DIR_CW},
  /* R_CCW_BEG1  */ {R_START0 | DIR_CCW, R_CCW_BEG0, R_CCW_BEG1, R_START1},
  /* R_CCW_BEG0  */ {R_START0,           R_CCW_BEG0, R_CCW_BEG1, R_START1 | DIR_CCW},
};

int I2C_INTERRUPT = 15;

#define MAX_LINES 32
unsigned int monitor_lines[MAX_LINES];
int lines = 0;

long settle_time = 50; // ms

//
// The "static const" data is the DEFAULT assignment for encoders,
// and for Controller2 and G2 front panel switches
// These defaults are read-only and copied to my_encoders and my_switches
// when restoring default values
//
// Controller1 has 3 small encoders + VFO, and  8 switches in 6 layers
// Controller2 has 4 small encoders + VFO, and 16 switches
// G2 panel    has 4 small encoders + VFO, and 16 switches
//
// The controller1 switches are hard-wired to the toolbar buttons
//

//
// RPI5: GPIO line 20 not available, replace "20" by "14" at four places in the following lines
//       and re-wire the controller connection from GPIO20 to GPIO14
//
static const ENCODER encoders_no_controller[MAX_ENCODERS] = {
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
};

static const ENCODER encoders_controller1[MAX_ENCODERS] = {
  {TRUE,  TRUE, 20, 1, 26, 1, 0, AF_GAIN,  R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 25, MENU_BAND,       0L},
  {TRUE,  TRUE, 16, 1, 19, 1, 0, AGC_GAIN, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE,  8, MENU_BANDSTACK,  0L},
  {TRUE,  TRUE,  4, 1, 21, 1, 0, DRIVE,    R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE,  7, MENU_MODE,       0L},
  {TRUE,  TRUE, 18, 1, 17, 1, 0, VFO,      R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE,  0, NO_ACTION,       0L},
  {FALSE, TRUE, 0, 1,  0, 0, 1, NO_ACTION, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE,  0, NO_ACTION,       0L},
};

static const ENCODER encoders_controller2_v1[MAX_ENCODERS] = {
  {TRUE, TRUE, 20, 1, 26, 1, 0, AF_GAIN,  R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 22, MENU_BAND,      0L},
  {TRUE, TRUE,  4, 1, 21, 1, 0, AGC_GAIN, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 27, MENU_BANDSTACK, 0L},
  {TRUE, TRUE, 16, 1, 19, 1, 0, IF_WIDTH, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 23, MENU_MODE,      0L},
  {TRUE, TRUE, 25, 1,  8, 1, 0, RIT,      R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 24, MENU_FREQUENCY, 0L},
  {TRUE, TRUE, 18, 1, 17, 1, 0, VFO,      R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE,  0, NO_ACTION,      0L},
};

static const ENCODER encoders_controller2_v2[MAX_ENCODERS] = {
  {TRUE, TRUE,  5, 1,  6, 1, 0, AGC_GAIN_RX1, R_START1, TRUE,  TRUE, 26, 1, 20, 1, 0, AF_GAIN_RX1, R_START1, TRUE,  TRUE, 22, RX1,            0L}, //ENC2
  {TRUE, TRUE,  9, 1,  7, 1, 0, AGC_GAIN_RX2, R_START1, TRUE,  TRUE, 21, 1,  4, 1, 0, AF_GAIN_RX2, R_START1, TRUE,  TRUE, 27, RX2,            0L}, //ENC3
  {TRUE, TRUE, 11, 1, 10, 1, 0, DIV_GAIN,     R_START1, TRUE,  TRUE, 19, 1, 16, 1, 0, DIV_PHASE,   R_START1, TRUE,  TRUE, 23, DIV,            0L}, //ENC4
  {TRUE, TRUE, 13, 1, 12, 1, 0, XIT,          R_START1, TRUE,  TRUE,  8, 1, 25, 1, 0, RIT,         R_START1, TRUE,  TRUE, 24, MENU_FREQUENCY, 0L}, //ENC5
  {TRUE, TRUE, 18, 1, 17, 1, 0, VFO,          R_START,  FALSE, TRUE,  0, 0,  0, 0, 0, NO_ACTION,   R_START, FALSE,  TRUE,  0, NO_ACTION,      0L}, //ENC1/VFO
};

static const ENCODER encoders_g2_frontpanel[MAX_ENCODERS] = {
  {TRUE, TRUE,  5, 1,  6, 1, 0, DRIVE,    R_START1, TRUE,  TRUE, 26, 1, 20, 1, 0, MIC_GAIN,  R_START1, TRUE,  TRUE, 22, PS,             0L}, //ENC1
  {TRUE, TRUE,  9, 1,  7, 1, 0, AGC_GAIN, R_START1, TRUE,  TRUE, 21, 1,  4, 1, 0, AF_GAIN,   R_START1, TRUE,  TRUE, 27, MUTE,           0L}, //ENC3
  {TRUE, TRUE, 11, 1, 10, 1, 0, DIV_GAIN, R_START1, TRUE,  TRUE, 19, 1, 16, 1, 0, DIV_PHASE, R_START1, TRUE,  TRUE, 23, DIV,            0L}, //ENC7
  {TRUE, TRUE, 13, 1, 12, 1, 0, XIT,      R_START1, TRUE,  TRUE,  8, 1, 25, 1, 0, RIT,       R_START1, TRUE,  TRUE, 24, MENU_FREQUENCY, 0L}, //ENC5
  {TRUE, TRUE, 18, 1, 17, 1, 0, VFO,      R_START,  FALSE, TRUE,  0, 0,  0, 0, 0, 0,         R_START, FALSE,  TRUE,  0, NO_ACTION,      0L}, //VFO
};

static const SWITCH switches_no_controller[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L}
};

SWITCH switches_controller1[MAX_FUNCTIONS][MAX_SWITCHES] = {
  { {TRUE,  TRUE, 27, MOX,            0L},
    {TRUE,  TRUE, 13, MENU_BAND,      0L},
    {TRUE,  TRUE, 12, MENU_BANDSTACK, 0L},
    {TRUE,  TRUE,  6, MENU_MODE,      0L},
    {TRUE,  TRUE,  5, MENU_FILTER,    0L},
    {TRUE,  TRUE, 24, MENU_NOISE,     0L},
    {TRUE,  TRUE, 23, MENU_AGC,       0L},
    {TRUE,  TRUE, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L}
  },
  { {TRUE,  TRUE, 27, MOX,            0L},
    {TRUE,  TRUE, 13, LOCK,           0L},
    {TRUE,  TRUE, 12, CTUN,           0L},
    {TRUE,  TRUE,  6, A_TO_B,         0L},
    {TRUE,  TRUE,  5, B_TO_A,         0L},
    {TRUE,  TRUE, 24, A_SWAP_B,       0L},
    {TRUE,  TRUE, 23, SPLIT,          0L},
    {TRUE,  TRUE, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L}
  },
  { {TRUE,  TRUE, 27, MOX,            0L},
    {TRUE,  TRUE, 13, MENU_FREQUENCY, 0L},
    {TRUE,  TRUE, 12, MENU_MEMORY,    0L},
    {TRUE,  TRUE,  6, RIT_ENABLE,     0L},
    {TRUE,  TRUE,  5, RIT_PLUS,       0L},
    {TRUE,  TRUE, 24, RIT_MINUS,      0L},
    {TRUE,  TRUE, 23, RIT_CLEAR,      0L},
    {TRUE,  TRUE, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L}
  },
  { {TRUE,  TRUE, 27, MOX,            0L},
    {TRUE,  TRUE, 13, MENU_FREQUENCY, 0L},
    {TRUE,  TRUE, 12, MENU_MEMORY,    0L},
    {TRUE,  TRUE,  6, XIT_ENABLE,     0L},
    {TRUE,  TRUE,  5, XIT_PLUS,       0L},
    {TRUE,  TRUE, 24, XIT_MINUS,      0L},
    {TRUE,  TRUE, 23, XIT_CLEAR,      0L},
    {TRUE,  TRUE, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L}
  },
  { {TRUE,  TRUE, 27, MOX,            0L},
    {TRUE,  TRUE, 13, MENU_FREQUENCY, 0L},
    {TRUE,  TRUE, 12, SPLIT,          0L},
    {TRUE,  TRUE,  6, DUPLEX,         0L},
    {TRUE,  TRUE,  5, SAT,            0L},
    {TRUE,  TRUE, 24, RSAT,           0L},
    {TRUE,  TRUE, 23, MENU_BAND,      0L},
    {TRUE,  TRUE, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L}
  },
  { {TRUE,  TRUE, 27, MOX,            0L},
    {TRUE,  TRUE, 13, TUNE,           0L},
    {TRUE,  TRUE, 12, TUNE_FULL,      0L},
    {TRUE,  TRUE,  6, TUNE_MEMORY,    0L},
    {TRUE,  TRUE,  5, MENU_BAND,      0L},
    {TRUE,  TRUE, 24, MENU_MODE,      0L},
    {TRUE,  TRUE, 23, MENU_FILTER,    0L},
    {TRUE,  TRUE, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, NO_ACTION,      0L}
  },

};

static const SWITCH switches_controller2_v1[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, MOX,              0L},
  {FALSE, FALSE, 0, TUNE,             0L},
  {FALSE, FALSE, 0, PS,               0L},
  {FALSE, FALSE, 0, TWO_TONE,         0L},
  {FALSE, FALSE, 0, NR,               0L},
  {FALSE, FALSE, 0, A_TO_B,           0L},
  {FALSE, FALSE, 0, B_TO_A,           0L},
  {FALSE, FALSE, 0, MODE_MINUS,       0L},
  {FALSE, FALSE, 0, BAND_MINUS,       0L},
  {FALSE, FALSE, 0, MODE_PLUS,        0L},
  {FALSE, FALSE, 0, BAND_PLUS,        0L},
  {FALSE, FALSE, 0, XIT_ENABLE,       0L},
  {FALSE, FALSE, 0, NB,               0L},
  {FALSE, FALSE, 0, SNB,              0L},
  {FALSE, FALSE, 0, LOCK,             0L},
  {FALSE, FALSE, 0, CTUN,             0L}
};

static const SWITCH switches_controller2_v2[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, MOX,              0L},  //GPB7 SW2
  {FALSE, FALSE, 0, TUNE,             0L},  //GPB6 SW3
  {FALSE, FALSE, 0, PS,               0L},  //GPB5 SW4
  {FALSE, FALSE, 0, TWO_TONE,         0L},  //GPB4 SW5
  {FALSE, FALSE, 0, NR,               0L},  //GPA3 SW6
  {FALSE, FALSE, 0, NB,               0L},  //GPB3 SW14
  {FALSE, FALSE, 0, SNB,              0L},  //GPB2 SW15
  {FALSE, FALSE, 0, XIT_ENABLE,       0L},  //GPA7 SW13
  {FALSE, FALSE, 0, BAND_PLUS,        0L},  //GPA6 SW12
  {FALSE, FALSE, 0, MODE_PLUS,        0L},  //GPA5 SW11
  {FALSE, FALSE, 0, BAND_MINUS,       0L},  //GPA4 SW10
  {FALSE, FALSE, 0, MODE_MINUS,       0L},  //GPA0 SW9
  {FALSE, FALSE, 0, A_TO_B,           0L},  //GPA2 SW7
  {FALSE, FALSE, 0, B_TO_A,           0L},  //GPA1 SW8
  {FALSE, FALSE, 0, LOCK,             0L},  //GPB1 SW16
  {FALSE, FALSE, 0, CTUN,             0L}   //GPB0 SW17
};

static const SWITCH switches_g2_frontpanel[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, XIT_ENABLE,       0L},  //GPB7 SW22
  {FALSE, FALSE, 0, RIT_ENABLE,       0L},  //GPB6 SW21
  {FALSE, FALSE, 0, FUNCTION,         0L},  //GPB5 SW20
  {FALSE, FALSE, 0, SPLIT,            0L},  //GPB4 SW19
  {FALSE, FALSE, 0, LOCK,             0L},  //GPA3 SW9
  {FALSE, FALSE, 0, B_TO_A,           0L},  //GPB3 SW18
  {FALSE, FALSE, 0, A_TO_B,           0L},  //GPB2 SW17
  {FALSE, FALSE, 0, MODE_MINUS,       0L},  //GPA7 SW13
  {FALSE, FALSE, 0, BAND_PLUS,        0L},  //GPA6 SW12
  {FALSE, FALSE, 0, FILTER_PLUS,      0L},  //GPA5 SW11
  {FALSE, FALSE, 0, MODE_PLUS,        0L},  //GPA4 SW10
  {FALSE, FALSE, 0, MOX,              0L},  //GPA0 SW6
  {FALSE, FALSE, 0, CTUN,             0L},  //GPA2 SW8
  {FALSE, FALSE, 0, TUNE,             0L},  //GPA1 SW7
  {FALSE, FALSE, 0, BAND_MINUS,       0L},  //GPB1 SW16
  {FALSE, FALSE, 0, FILTER_MINUS,     0L}   //GPB0 SW15
};

ENCODER my_encoders[MAX_ENCODERS];
SWITCH  my_switches[MAX_SWITCHES];

ENCODER *encoders = NULL;
SWITCH *switches = NULL;

void gpio_default_encoder_actions(int ctrlr) {
  const ENCODER *default_encoders;
  switch (ctrlr) {
  case NO_CONTROLLER:
  case G2_V2:
  default:
    default_encoders = NULL;
    break;
  case CONTROLLER1:
    default_encoders = encoders_controller1;
    break;
  case CONTROLLER2_V1:
    default_encoders = encoders_controller2_v1;
    break;
  case CONTROLLER2_V2:
    default_encoders = encoders_controller2_v2;
    break;
  case G2_FRONTPANEL:
    default_encoders = encoders_g2_frontpanel;
    break;
  }
  if (default_encoders) {
    //
    // Copy (only) actions
    //
    for (int i = 0; i < MAX_ENCODERS; i++) {
      my_encoders[i].bottom_encoder_function = default_encoders[i].bottom_encoder_function;
      my_encoders[i].top_encoder_function    = default_encoders[i].top_encoder_function;
      my_encoders[i].switch_function         = default_encoders[i].switch_function;
    }
  }
}

void gpio_default_switch_actions(int ctrlr) {
  const SWITCH *default_switches;
  switch (ctrlr) {
  case NO_CONTROLLER:
  case CONTROLLER1:
  case G2_V2:
  default:
    default_switches = NULL;
    break;
  case CONTROLLER2_V1:
    default_switches = switches_controller2_v1;
    break;
  case CONTROLLER2_V2:
    default_switches = switches_controller2_v2;
    break;
  case G2_FRONTPANEL:
    default_switches = switches_g2_frontpanel;
    break;
  }
  if (default_switches) {
    //
    // Copy (only) actions
    //
    for (int i = 0; i < MAX_SWITCHES; i++) {
      my_switches[i].switch_function = default_switches[i].switch_function;
    }
  }
}

//
// If there is non-standard hardware at the GPIO lines
// the code below in the NO_CONTROLLER section must
// be adjusted such that "occupied" GPIO lines are not
// used for CW or PTT.
// For CONTROLLER1 and CONTROLLER2_V1, GPIO
// lines 9,10,11,14 are "free" and can be
// used for CW and PTT.
//
//  At this place, copy complete data structures to my_encoders
//  and my_switches, including GPIO lines etc.
//
void gpio_set_defaults(int ctrlr) {
  t_print("%s: %d\n", __func__, ctrlr);
  switch (ctrlr) {
  case CONTROLLER1:
    memcpy(my_encoders, encoders_controller1, sizeof(my_encoders));
    encoders = my_encoders;
    switches = switches_controller1[0];
    break;
  case CONTROLLER2_V1:
    memcpy(my_encoders, encoders_controller2_v1, sizeof(my_encoders));
    memcpy(my_switches, switches_controller2_v1, sizeof(my_switches));
    encoders = my_encoders;
    switches = my_switches;
    break;
  case CONTROLLER2_V2:
    memcpy(my_encoders, encoders_controller2_v2, sizeof(my_encoders));
    memcpy(my_switches, switches_controller2_v2, sizeof(my_switches));
    encoders = my_encoders;
    switches = my_switches;
    break;
  case G2_FRONTPANEL:
    memcpy(my_encoders, encoders_g2_frontpanel, sizeof(my_encoders));
    memcpy(my_switches, switches_g2_frontpanel, sizeof(my_switches));
    encoders = my_encoders;
    switches = my_switches;
    break;
  case G2_V2:
  case NO_CONTROLLER:
  default:
    memcpy(my_encoders, encoders_no_controller, sizeof(my_encoders));
    memcpy(my_switches, switches_no_controller, sizeof(my_switches));
    encoders = my_encoders;
    switches = my_switches;
    break;
  }
}

void gpioRestoreState(void) {
  if (access("controller_mapping.props", F_OK) == 0) {
    loadProperties("controller_mapping.props");
  } else {
    loadProperties("gpio.props");
  }
  GetPropI0("controller",                                         controller);
  //
  // No GPIO support: we can only have the G2Mk2 or  none
  //
  if (controller != G2_V2) {
    controller = NO_CONTROLLER;
  }
  gpio_set_defaults(controller);
  for (int i = 0; i < MAX_ENCODERS; i++) {
    GetPropI1("encoders[%d].bottom_encoder_enabled", i,           encoders[i].bottom_encoder_enabled);
    GetPropI1("encoders[%d].bottom_encoder_pullup", i,            encoders[i].bottom_encoder_pullup);
    GetPropI1("encoders[%d].bottom_encoder_address_a", i,         encoders[i].bottom_encoder_address_a);
    GetPropI1("encoders[%d].bottom_encoder_address_b", i,         encoders[i].bottom_encoder_address_b);
    GetPropI1("encoders[%d].top_encoder_enabled", i,              encoders[i].top_encoder_enabled);
    GetPropI1("encoders[%d].top_encoder_pullup", i,               encoders[i].top_encoder_pullup);
    GetPropI1("encoders[%d].top_encoder_address_a", i,            encoders[i].top_encoder_address_a);
    GetPropI1("encoders[%d].top_encoder_address_b", i,            encoders[i].top_encoder_address_b);
    GetPropI1("encoders[%d].switch_enabled", i,                   encoders[i].switch_enabled);
    GetPropI1("encoders[%d].switch_pullup", i,                    encoders[i].switch_pullup);
    GetPropI1("encoders[%d].switch_address", i,                   encoders[i].switch_address);
  }
  for (int f = 0; f < MAX_FUNCTIONS; f++) {
    for (int i = 0; i < MAX_SWITCHES; i++) {
      GetPropI2("switches[%d,%d].switch_enabled", f, i,           switches_controller1[f][i].switch_enabled);
      GetPropI2("switches[%d,%d].switch_pullup", f, i,            switches_controller1[f][i].switch_pullup);
      GetPropI2("switches[%d,%d].switch_address", f, i,           switches_controller1[f][i].switch_address);
    }
  }
  if (controller != CONTROLLER1) {
    for (int i = 0; i < MAX_SWITCHES; i++) {
      GetPropI1("switches[%d].switch_enabled", i,                 switches[i].switch_enabled);
      GetPropI1("switches[%d].switch_pullup", i,                  switches[i].switch_pullup);
      GetPropI1("switches[%d].switch_address", i,                 switches[i].switch_address);
    }
  }
}

void gpioSaveState(void) {
  clearProperties();
  SetPropI0("controller",                                         controller);
  for (int i = 0; i < MAX_ENCODERS; i++) {
    SetPropI1("encoders[%d].bottom_encoder_enabled", i,           encoders[i].bottom_encoder_enabled);
    SetPropI1("encoders[%d].bottom_encoder_pullup", i,            encoders[i].bottom_encoder_pullup);
    SetPropI1("encoders[%d].bottom_encoder_address_a", i,         encoders[i].bottom_encoder_address_a);
    SetPropI1("encoders[%d].bottom_encoder_address_b", i,         encoders[i].bottom_encoder_address_b);
    SetPropI1("encoders[%d].top_encoder_enabled", i,              encoders[i].top_encoder_enabled);
    SetPropI1("encoders[%d].top_encoder_pullup", i,               encoders[i].top_encoder_pullup);
    SetPropI1("encoders[%d].top_encoder_address_a", i,            encoders[i].top_encoder_address_a);
    SetPropI1("encoders[%d].top_encoder_address_b", i,            encoders[i].top_encoder_address_b);
    SetPropI1("encoders[%d].switch_enabled", i,                   encoders[i].switch_enabled);
    SetPropI1("encoders[%d].switch_pullup", i,                    encoders[i].switch_pullup);
    SetPropI1("encoders[%d].switch_address", i,                   encoders[i].switch_address);
  }
  for (int f = 0; f < MAX_FUNCTIONS; f++) {
    for (int i = 0; i < MAX_SWITCHES; i++) {
      SetPropI2("switches[%d,%d].switch_enabled", f, i,           switches_controller1[f][i].switch_enabled);
      SetPropI2("switches[%d,%d].switch_pullup", f, i,            switches_controller1[f][i].switch_pullup);
      SetPropI2("switches[%d,%d].switch_address", f, i,           switches_controller1[f][i].switch_address);
    }
  }
  if (controller != CONTROLLER1) {
    for (int i = 0; i < MAX_SWITCHES; i++) {
      SetPropI1("switches[%d].switch_enabled", i,                 switches[i].switch_enabled);
      SetPropI1("switches[%d].switch_pullup", i,                  switches[i].switch_pullup);
      SetPropI1("switches[%d].switch_address", i,                 switches[i].switch_address);
    }
  }
  saveProperties("controller_mapping.props");
}

void gpioRestoreActions(void) {
  int props_controller = NO_CONTROLLER;
  gpio_set_defaults(controller);
  //
  //  "toolbar" functions
  //
  GetPropI0("switches.function",                                 function);
  for (int f = 0; f < MAX_FUNCTIONS; f++) {
    for (int i = 0; i < MAX_SWITCHES; i++) {
      GetPropA2("switches[%d,%d].switch_function", f, i,         switches_controller1[f][i].switch_function);
    }
  }
  GetPropI0("controller",                                        props_controller);
  //
  // If the props file refers to another controller, skip props data
  //
  if (controller != props_controller) { return; }
  for (int i = 0; i < MAX_ENCODERS; i++) {
    GetPropA1("encoders[%d].bottom_encoder_function", i,         encoders[i].bottom_encoder_function);
    GetPropA1("encoders[%d].top_encoder_function", i,            encoders[i].top_encoder_function);
    GetPropA1("encoders[%d].switch_function", i,                 encoders[i].switch_function);
  }
  if (controller != CONTROLLER1) {
    for (int i = 0; i < MAX_SWITCHES; i++) {
      GetPropA1("switches[%d].switch_function", i,               switches[i].switch_function);
    }
  }
}

void gpioSaveActions(void) {
  char name[128];
  char value[128];
  //
  //  "toolbar" functions
  //
  SetPropI0("switches.function",                                 function);
  for (int f = 0; f < MAX_FUNCTIONS; f++) {
    for (int i = 0; i < MAX_SWITCHES; i++) {
      SetPropA2("switches[%d,%d].switch_function", f, i,         switches_controller1[f][i].switch_function);
    }
  }
  SetPropI0("controller",                                        controller);
  //
  // If there is no controller, there is nothing to store
  //
  if (controller == NO_CONTROLLER) { return; }
  for (int i = 0; i < MAX_ENCODERS; i++) {
    SetPropA1("encoders[%d].bottom_encoder_function", i,         encoders[i].bottom_encoder_function);
    SetPropA1("encoders[%d].top_encoder_function", i,            encoders[i].top_encoder_function);
    SetPropA1("encoders[%d].switch_function", i,                 encoders[i].switch_function);
  }
  if (controller != CONTROLLER1) {
    for (int i = 0; i < MAX_SWITCHES; i++) {
      SetPropA1("switches[%d].switch_function", i,               switches[i].switch_function);
    }
  }
  snprintf(value, sizeof(value), "%d", function);
  setProperty("switches.function", value);
  for (int f = 0; f < MAX_FUNCTIONS; f++) {
    for (int i = 0; i < MAX_SWITCHES; i++) {
      snprintf(name, sizeof(name), "switches[%d,%d].switch_function", f, i);
      Action2String(switches_controller1[f][i].switch_function, value, sizeof(value));
      setProperty(name, value);
    }
  }
  if (controller == CONTROLLER2_V1 || controller == CONTROLLER2_V2 || controller == G2_FRONTPANEL) {
    for (int i = 0; i < MAX_SWITCHES; i++) {
      snprintf(name, sizeof(name), "switches[%d].switch_function", i);
      Action2String(switches[i].switch_function, value, sizeof(value));
      setProperty(name, value);
    }
  }
}
