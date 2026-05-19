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
#include <unistd.h>

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
#include "diversity_menu.h"
#include "actions.h"
#include "controller_mapping.h"
#include "ext.h"
#include "sliders.h"
#include "new_protocol.h"
#include "zoompan.h"
#include "iambic.h"
#include "message.h"

//
// Controller/action mapping only. Direct Raspberry Pi GPIO support has been removed.
// The remaining data provides toolbar and optional controller action defaults.
//
#define R_START 0x00

//
// Default assignment for "no controller" and toolbar switch actions.
// Legacy GPIO-connected controller profiles have been removed.
//
static const ENCODER encoders_no_controller[MAX_ENCODERS] = {
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
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

ENCODER my_encoders[MAX_ENCODERS];
SWITCH  my_switches[MAX_SWITCHES];

ENCODER *encoders = NULL;
SWITCH *switches = NULL;

void gpio_default_encoder_actions(int ctrlr) {
  (void)ctrlr;
}

void gpio_default_switch_actions(int ctrlr) {
  (void)ctrlr;
}

void gpio_set_defaults(int ctrlr) {
  t_print("%s: %d\n", __func__, ctrlr);
  switch (ctrlr) {
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
  // Direct GPIO controller profiles have been removed.
  // Only G2_V2 and NO_CONTROLLER remain valid controller selections here.
  //
  if (controller != G2_V2) {
    controller = NO_CONTROLLER;
  }
  gpio_set_defaults(controller);
}

void gpioSaveState(void) {
  clearProperties();
  SetPropI0("controller",                                         controller);
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
}
