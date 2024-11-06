/* Copyright (C)
* 2021 - John Melton, G0ORX/N6LYT
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

#ifndef _ACTION_H_
#define _ACTION_H_

enum ACTION {
  NO_ACTION = 0,
  A_SWAP_B,
  B_TO_A,
  A_TO_B,
  AF_GAIN,
  AF_GAIN_RX1,
  AF_GAIN_RX2,
  AGC,
  AGC_GAIN,
  AGC_GAIN_RX1,
  AGC_GAIN_RX2,
  MENU_AGC,
  ANF,
  ATTENUATION,
  BAND_10,
  BAND_12,
  BAND_1240,
  BAND_136,
  BAND_144,
  BAND_15,
  BAND_160,
  BAND_17,
  BAND_20,
  BAND_220,
  BAND_2300,
  BAND_30,
  BAND_3400,
  BAND_40,
  BAND_430,
  BAND_6,
  BAND_60,
  BAND_70,
  BAND_80,
  BAND_902,
  BAND_AIR,
  BAND_GEN,
  BAND_MINUS,
  BAND_PLUS,
  BAND_WWV,
  BANDSTACK_MINUS,
  BANDSTACK_PLUS,
  MENU_BAND,
  MENU_BANDSTACK,
  CAPTURE,
  COMP_ENABLE,
  COMPRESSION,
  CTUN,
  CW_AUDIOPEAKFILTER,
  CW_FREQUENCY,
  CW_LEFT,
  CW_RIGHT,
  CW_SPEED,
  CW_KEYER_KEYDOWN,
  CW_KEYER_PTT,
  CW_KEYER_SPEED,
  DIV,
  DIV_GAIN,
  DIV_GAIN_COARSE,
  DIV_GAIN_FINE,
  DIV_PHASE,
  DIV_PHASE_COARSE,
  DIV_PHASE_FINE,
  MENU_DIVERSITY,
  DUPLEX,
  FILTER_MINUS,
  FILTER_PLUS,
  FILTER_CUT_LOW,
  FILTER_CUT_HIGH,
  FILTER_CUT_DEFAULT,
  MENU_FILTER,
  FUNCTION,
  FUNCTIONREV,
  IF_SHIFT,
  IF_SHIFT_RX1,
  IF_SHIFT_RX2,
  IF_WIDTH,
  IF_WIDTH_RX1,
  IF_WIDTH_RX2,
  LINEIN_GAIN,
  LOCK,
  MENU_MAIN,
  MENU_MEMORY,
  MIC_GAIN,
  MODE_MINUS,
  MODE_PLUS,
  MENU_MODE,
  MOX,
  MULTI_ENC,
  MULTI_SELECT,
  MULTI_BUTTON,
  MUTE,
  MUTE_RX1,
  MUTE_RX2,
  NB,
  NR,
  MENU_NOISE,
  NUMPAD_0,
  NUMPAD_1,
  NUMPAD_2,
  NUMPAD_3,
  NUMPAD_4,
  NUMPAD_5,
  NUMPAD_6,
  NUMPAD_7,
  NUMPAD_8,
  NUMPAD_9,
  NUMPAD_BS,
  NUMPAD_CL,
  NUMPAD_DEC,
  NUMPAD_KHZ,
  NUMPAD_MHZ,
  NUMPAD_ENTER,
  PAN,
  PAN_MINUS,
  PAN_PLUS,
  PANADAPTER_HIGH,
  PANADAPTER_LOW,
  PANADAPTER_STEP,
  PREAMP,
  PS,
  MENU_PS,
  PTT,
  RCL0,
  RCL1,
  RCL2,
  RCL3,
  RCL4,
  RCL5,
  RCL6,
  RCL7,
  RCL8,
  RCL9,
  RF_GAIN,
  RF_GAIN_RX1,
  RF_GAIN_RX2,
  RIT,
  RIT_CLEAR,
  RIT_ENABLE,
  RIT_MINUS,
  RIT_PLUS,
  RIT_RX1,
  RIT_RX2,
  RIT_STEP,
  RSAT,
  MENU_RX,
  RX1,
  RX2,
  SAT,
  SNB,
  SPLIT,
  SQUELCH,
  SQUELCH_RX1,
  SQUELCH_RX2,
  SWAP_RX,
  TUNE,
  TUNE_DRIVE,
  TUNE_FULL,
  TUNE_MEMORY,
  DRIVE,
  TWO_TONE,
  MENU_TX,
  VFO,
  MENU_FREQUENCY,
  VFO_STEP_MINUS,
  VFO_STEP_PLUS,
  VFOA,
  VFOB,
  VOX,
  VOXLEVEL,
  WATERFALL_HIGH,
  WATERFALL_LOW,
  XIT,
  XIT_CLEAR,
  XIT_ENABLE,
  XIT_MINUS,
  XIT_PLUS,
  ZOOM,
  ZOOM_MINUS,
  ZOOM_PLUS,
  ACTIONS
};

enum ACTIONtype {
  TYPE_NONE = 0,
  MIDI_KEY = 1,         // MIDI Button (press event)
  MIDI_KNOB = 2,        // MIDI Knob   (value between 0 and 100)
  MIDI_WHEEL = 4,       // MIDI Wheel  (direction and speed)
  CONTROLLER_SWITCH = 8, // Controller Button
  CONTROLLER_ENCODER = 16 // Controller Encoder
};

typedef struct _action_table {
  enum ACTION action;
  const char *str;              // desciptive text
  const char *button_str;       // short button text, also used in props files
  enum ACTIONtype type;
} ACTION_TABLE;

typedef struct _multi_table {
  enum ACTION action;
  const char *descr;            // short text without captialization
} MULTI_TABLE;

enum ACTION_MODE {
  RELATIVE,
  ABSOLUTE,
  PRESSED,
  RELEASED
};

typedef struct process_action {
  enum ACTION action;
  enum ACTION_MODE mode;
  int val;
} PROCESS_ACTION;

extern ACTION_TABLE ActionTable[ACTIONS + 1];

extern int process_action(void *data);
extern void schedule_action(enum ACTION action, enum ACTION_MODE mode, int val);
extern void Action2String(const int id, char *str, size_t len);
extern int  String2Action(const char *str);
extern void GetMultifunctionString(char* str, size_t len);
extern int  GetMultifunctionStatus(void);
#endif
