/* Copyright (C)
* 2021 - John Melton, G0ORX/N6LYT
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
#include <math.h>

#include "main.h"
#include "discovery.h"
#include "receiver.h"
#include "sliders.h"
#include "band_menu.h"
#include "diversity_menu.h"
#include "vfo.h"
#include "radio.h"
#include "radio_menu.h"
#include "new_menu.h"
#include "old_protocol.h"
#include "new_protocol.h"
#include "ps_menu.h"
#include "agc.h"
#include "filter.h"
#include "mode.h"
#include "band.h"
#include "bandstack.h"
#include "noise_menu.h"
#include "ext.h"
#include "zoompan.h"
#include "actions.h"
#include "gpio.h"
#include "toolbar.h"
#include "iambic.h"
#include "store.h"
#include "equalizer_menu.h"
#include "exit_menu.h"
#include "message.h"
#include "dxcluster.h"
#include "greyline.h"
#include "rx_panadapter.h"

//
// The "short button text" (button_str) needs to be present in ALL cases, and must be different
// for each case. button_str is used to identify the action in the props files and therefore
// it should not contain white space. Apart from the props files, the button_str determines
// what is written on the buttons in the toolbar (but that's it).
// For finding an action in the "action_dialog", it is most convenient if these actions are
// (roughly) sorted by the first string, but keep "NONE" at the beginning
//
ACTION_TABLE ActionTable[] = {
  {NO_ACTION,           "None",                 "NONE",         TYPE_NONE},
  {A_SWAP_B,            "A<>B",                 "A<>B",         MIDI_KEY   | CONTROLLER_SWITCH},
  {B_TO_A,              "A<B",                  "A<B",          MIDI_KEY   | CONTROLLER_SWITCH},
  {A_TO_B,              "A>B",                  "A>B",          MIDI_KEY   | CONTROLLER_SWITCH},
  {AF_GAIN,             "AF Gain",              "AFGAIN",       MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {AF_GAIN_RX1,         "AF Gain\nRX1",         "AFGAIN1",      MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {AF_GAIN_RX2,         "AF Gain\nRX2",         "AFGAIN2",      MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {AGC,                 "AGC",                  "AGCT",         MIDI_KEY   | CONTROLLER_SWITCH},
  {AGC_GAIN,            "AGC Gain",             "AGCGain",      MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {AGC_GAIN_RX1,        "AGC Gain\nRX1",        "AGCGain1",     MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {AGC_GAIN_RX2,        "AGC Gain\nRX2",        "AGCGain2",     MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {ATU_WIN,             atuwin_ACTION,          atuwin_ACTION,  TYPE_NONE},
  {MENU_AGC,            "AGC\nMenu",            "AGC-M",        MIDI_KEY   | CONTROLLER_SWITCH},
  {ANF,                 "ANF",                  "ANF",          MIDI_KEY   | CONTROLLER_SWITCH},
  {ATTENUATION,         "Atten",                "ATTEN",        MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {BAND_10,             "Band 10",              "10",           MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_12,             "Band 12",              "12",           MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_1240,           "Band 1240",            "1240",         MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_136,            "Band 136",             "136",          MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_144,            "Band 144",             "144",          MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_15,             "Band 15",              "15",           MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_160,            "Band 160",             "160",          MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_17,             "Band 17",              "17",           MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_20,             "Band 20",              "20",           MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_220,            "Band 220",             "220",          MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_2300,           "Band 2300",            "2300",         MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_30,             "Band 30",              "30",           MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_3400,           "Band 3400",            "3400",         MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_40,             "Band 40",              "40",           MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_430,            "Band 430",             "430",          MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_6,              "Band 6",               "6",            MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_60,             "Band 60",              "60",           MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_70,             "Band 70",              "70",           MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_80,             "Band 80",              "80",           MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_902,            "Band 902",             "902",          MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_AIR,            "Band AIR",             "AIR",          MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_GEN,            "Band GEN",             "GEN",          MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_MINUS,          "Band -",               "BND-",         MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_PLUS,           "Band +",               "BND+",         MIDI_KEY   | CONTROLLER_SWITCH},
  {BAND_WWV,            "Band WWV",             "WWV",          MIDI_KEY   | CONTROLLER_SWITCH},
  {BANDSTACK_MINUS,     "BndStack -",           "BSTK-",        MIDI_KEY   | CONTROLLER_SWITCH},
  {BANDSTACK_PLUS,      "BndStack +",           "BSTK+",        MIDI_KEY   | CONTROLLER_SWITCH},
  {BI_NAURAL,           "Binaural",             "BINAURAL",     TYPE_NONE},
  {MENU_BAND,           "Band\nMenu",           "BAND",         MIDI_KEY   | CONTROLLER_SWITCH},
  {MENU_BANDSTACK,      "BndStack\nMenu",       "BSTK",         MIDI_KEY   | CONTROLLER_SWITCH},
  {CAPTURE,             "Capture",              "CAPTURE",      MIDI_KEY   | CONTROLLER_SWITCH},
  {COMP_ENABLE,         "Cmpr On/Off",          "COMP",         MIDI_KEY   | CONTROLLER_SWITCH},
  {COMPRESSION,         "Cmpr Level",           "COMPVAL",      MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {CTUN,                "CTUN",                 "CTUN",         MIDI_KEY   | CONTROLLER_SWITCH},
  {CW_AUDIOPEAKFILTER,  "CW Audio\nPeak Fltr",  "CW-APF",       MIDI_KEY   | CONTROLLER_SWITCH},
  {CW_FREQUENCY,        "CW Frequency",         "CWFREQ",       MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {CW_LEFT,             "CW Left",              "CWL",          MIDI_KEY   | CONTROLLER_SWITCH},
  {CW_RIGHT,            "CW Right",             "CWR",          MIDI_KEY   | CONTROLLER_SWITCH},
  {CW_SPEED,            "CW Speed",             "CWSPD",        MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {CW_KEYER_KEYDOWN,    "CW Key\n(Keyer)",      "CWKy",         MIDI_KEY   | CONTROLLER_SWITCH},
  {CW_KEYER_PTT,        "PTT\n(CW Keyer)",      "CWKyPTT",      MIDI_KEY   | CONTROLLER_SWITCH},
  {CW_KEYER_SPEED,      "Speed\n(Keyer)",       "CWKySpd",      MIDI_KNOB},
  {DIV,                 "DIV On/Off",           "DIVT",         MIDI_KEY   | CONTROLLER_SWITCH},
  {DIV_GAIN,            "DIV Gain",             "DIVG",         MIDI_WHEEL | CONTROLLER_ENCODER},
  {DIV_GAIN_COARSE,     "DIV Gain\nCoarse",     "DIVGC",        MIDI_WHEEL | CONTROLLER_ENCODER},
  {DIV_GAIN_FINE,       "DIV Gain\nFine",       "DIVGF",        MIDI_WHEEL | CONTROLLER_ENCODER},
  {DIV_PHASE,           "DIV Phase",            "DIVP",         MIDI_WHEEL | CONTROLLER_ENCODER},
  {DIV_PHASE_COARSE,    "DIV Phase\nCoarse",    "DIVPC",        MIDI_WHEEL | CONTROLLER_ENCODER},
  {DIV_PHASE_FINE,      "DIV Phase\nFine",      "DIVPF",        MIDI_WHEEL | CONTROLLER_ENCODER},
  {MENU_DIVERSITY,      "DIV\nMenu",            "DIV-M",        MIDI_KEY   | CONTROLLER_SWITCH},
  {DUPLEX,              "Duplex",               "DUP",          MIDI_KEY   | CONTROLLER_SWITCH},
  {DXC_WIN,             "DXC\nWindow",          "DXC-WIN",      TYPE_NONE},
  {DXC_TEST,            "DXC\nTest",            "DXC-TEST",     TYPE_NONE},
  {FILTER_MINUS,        "Filter -",             "FL-",          MIDI_KEY   | CONTROLLER_SWITCH},
  {FILTER_PLUS,         "Filter +",             "FL+",          MIDI_KEY   | CONTROLLER_SWITCH},
  {FILTER_CUT_LOW,      "Filter Cut\nLow",      "FCUTL",        MIDI_WHEEL | CONTROLLER_ENCODER},
  {FILTER_CUT_HIGH,     "Filter Cut\nHigh",     "FCUTH",        MIDI_WHEEL | CONTROLLER_ENCODER},
  {FILTER_CUT_DEFAULT,  "Filter Cut\nDefault",  "FCUTDEF",      MIDI_KEY   | CONTROLLER_SWITCH},
  {F_SCREEN,            "Full\nScreen",         "FSCRN",        TYPE_NONE},
  {MENU_FILTER,         "RX Filter\nMenu",      "RXFILT",       MIDI_KEY   | CONTROLLER_SWITCH},
  {FUNCTION,            "Function",             "FUNC",         MIDI_KEY   | CONTROLLER_SWITCH},
  {FUNCTIONREV,         "FuncRev",              "FUNC-",        MIDI_KEY   | CONTROLLER_SWITCH},
  {GL_WIN,              "Greyline\nWindow",     "GREYLINE",     TYPE_NONE},
  {IF_SHIFT,            "IF Shift",             "IFSHFT",       MIDI_WHEEL | CONTROLLER_ENCODER},
  {IF_SHIFT_RX1,        "IF Shift\nRX1",        "IFSHFT1",      MIDI_WHEEL | CONTROLLER_ENCODER},
  {IF_SHIFT_RX2,        "IF Shift\nRX2",        "IFSHFT2",      MIDI_WHEEL | CONTROLLER_ENCODER},
  {IF_WIDTH,            "IF Width",             "IFWIDTH",      MIDI_WHEEL | CONTROLLER_ENCODER},
  {IF_WIDTH_RX1,        "IF Width\nRX1",        "IFWIDTH1",     MIDI_WHEEL | CONTROLLER_ENCODER},
  {IF_WIDTH_RX2,        "IF Width\nRX2",        "IFWIDTH2",     MIDI_WHEEL | CONTROLLER_ENCODER},
  {LINEIN_GAIN,         "Linein\nGain",         "LIGAIN",       MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {LOCK,                "Lock",                 "LOCKM",        MIDI_KEY   | CONTROLLER_SWITCH},
  {MENU_MAIN,           "Main\nMenu",           "MAIN-M",       MIDI_KEY   | CONTROLLER_SWITCH},
  {MENU_MEMORY,         "Memory\nMenu",         "MEM",          MIDI_KEY   | CONTROLLER_SWITCH},
  {MIC_GAIN,            "Mic Gain",             "MICGAIN",      MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {MODE_MINUS,          "Mode -",               "MD-",          MIDI_KEY   | CONTROLLER_SWITCH},
  {MODE_PLUS,           "Mode +",               "MD+",          MIDI_KEY   | CONTROLLER_SWITCH},
  {MENU_MODE,           "Mode\nMenu",           "MODE",         MIDI_KEY   | CONTROLLER_SWITCH},
  {MOX,                 "MOX",                  "MOX",          MIDI_KEY   | CONTROLLER_SWITCH},
  {MULTI_ENC,           "Multi",                "MULTI",        MIDI_WHEEL | CONTROLLER_ENCODER},
  {MULTI_SELECT,        "Multi Action\nSelect", "MULTISEL",     MIDI_WHEEL | CONTROLLER_ENCODER},
  {MULTI_BUTTON,        "Multi Toggle",         "MULTIBTN",     MIDI_KEY   | CONTROLLER_SWITCH},
  {MUTE,                "Mute",                 "MUTE",         MIDI_KEY   | CONTROLLER_SWITCH},
  {MUTE_RX1,            "Mute RX1",             "MUTE1",        MIDI_KEY   | CONTROLLER_SWITCH},
  {MUTE_RX2,            "Mute RX2",             "MUTE2",        MIDI_KEY   | CONTROLLER_SWITCH},
  {NB,                  "NB",                   "NB",           MIDI_KEY   | CONTROLLER_SWITCH},
  {NR,                  "NR",                   "NR",           MIDI_KEY   | CONTROLLER_SWITCH},
  {MENU_NOISE,          "Noise\nMenu",          "NOISE-M",      MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_0,            "NumPad 0",             "0",            MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_1,            "NumPad 1",             "1",            MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_2,            "NumPad 2",             "2",            MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_3,            "NumPad 3",             "3",            MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_4,            "NumPad 4",             "4",            MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_5,            "NumPad 5",             "5",            MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_6,            "NumPad 6",             "6",            MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_7,            "NumPad 7",             "7",            MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_8,            "NumPad 8",             "8",            MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_9,            "NumPad 9",             "9",            MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_BS,           "NumPad\nBS",           "BS",           MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_CL,           "NumPad\nCL",           "CL",           MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_DEC,          "NumPad\nDec",          "DEC",          MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_KHZ,          "NumPad\nkHz",          "KHZ",          MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_MHZ,          "NumPad\nMHz",          "MHZ",          MIDI_KEY   | CONTROLLER_SWITCH},
  {NUMPAD_ENTER,        "NumPad\nEnter",        "EN",           MIDI_KEY   | CONTROLLER_SWITCH},
  {PAN,                 "PanZoom",              "PAN",          MIDI_WHEEL | CONTROLLER_ENCODER},
  {PAN_MINUS,           "Pan-",                 "PAN-",         MIDI_KEY   | CONTROLLER_SWITCH},
  {PAN_PLUS,            "Pan+",                 "PAN+",         MIDI_KEY   | CONTROLLER_SWITCH},
  {PANADAPTER_HIGH,     "Panadapter\nHigh",     "PANH",         MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {PANADAPTER_LOW,      "Panadapter\nLow",      "PANL",         MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {PANADAPTER_STEP,     "Panadapter\nStep",     "PANS",         MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {PREAMP,              "Preamp\nOn/Off",       "PRE",          MIDI_KEY   | CONTROLLER_SWITCH},
  {PS,                  "PS On/Off",            "PS-T",         MIDI_KEY   | CONTROLLER_SWITCH},
  {MENU_PS,             "PS Menu",              "PS-M",         MIDI_KEY   | CONTROLLER_SWITCH},
  {PTT,                 "PTT",                  "PTT",          MIDI_KEY   | CONTROLLER_SWITCH},
  {QSPLT,               "Quick Split",          "QSPLT",        MIDI_KEY   | CONTROLLER_SWITCH},
  {RCL0,                "Rcl 0",                "RCL0",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RCL1,                "Rcl 1",                "RCL1",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RCL2,                "Rcl 2",                "RCL2",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RCL3,                "Rcl 3",                "RCL3",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RCL4,                "Rcl 4",                "RCL4",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RCL5,                "Rcl 5",                "RCL5",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RCL6,                "Rcl 6",                "RCL6",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RCL7,                "Rcl 7",                "RCL7",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RCL8,                "Rcl 8",                "RCL8",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RCL9,                "Rcl 9",                "RCL9",         MIDI_KEY   | CONTROLLER_SWITCH},
  {REPLAY,              "Replay",               "REPLAY",       MIDI_KEY   | CONTROLLER_SWITCH},
  {RF_GAIN,             "RF Gain",              "RFGAIN",       MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {RF_GAIN_RX1,         "RF Gain\nRX1",         "RFGAIN1",      MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {RF_GAIN_RX2,         "RF Gain\nRX2",         "RFGAIN2",      MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {RIT,                 "RIT",                  "RIT",          MIDI_WHEEL | CONTROLLER_ENCODER},
  {RIT_CLEAR,           "RIT\nClear",           "RITCL",        MIDI_KEY   | CONTROLLER_SWITCH},
  {RIT_ENABLE,          "RIT\nOn/Off",          "RITT",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RIT_MINUS,           "RIT -",                "RIT-",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RIT_PLUS,            "RIT +",                "RIT+",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RIT_RX1,             "RIT\nRX1",             "RIT1",         MIDI_WHEEL | CONTROLLER_ENCODER},
  {RIT_RX2,             "RIT\nRX2",             "RIT2",         MIDI_WHEEL | CONTROLLER_ENCODER},
  {RIT_STEP,            "RIT\nStep",            "RITST",        MIDI_KEY   | CONTROLLER_SWITCH},
  {RITXIT,              "RIT/XIT",              "RITXIT",       MIDI_WHEEL | CONTROLLER_ENCODER},
  {RITSELECT,           "RIT/XIT\nCycle",       "RITXTCYC",     MIDI_KEY   | CONTROLLER_SWITCH},
  {RITXIT_CLEAR,        "RIT/XIT\nClear",       "RITXTCLR",     MIDI_KEY   | CONTROLLER_SWITCH},
  {RSAT,                "RSAT",                 "RSAT",         MIDI_KEY   | CONTROLLER_SWITCH},
  {MENU_RX,             "RX\nMenu",             "RX-M",         MIDI_KEY   | CONTROLLER_SWITCH},
  {RX1,                 "RX1",                  "RX1",          MIDI_KEY   | CONTROLLER_SWITCH},
  {RX2,                 "RX2",                  "RX2",          MIDI_KEY   | CONTROLLER_SWITCH},
  {SAT,                 "SAT",                  "SAT",          MIDI_KEY   | CONTROLLER_SWITCH},
  {SHUTDOWN,            "Shutdown\nOS",         "SDWN",         MIDI_KEY   | CONTROLLER_SWITCH},
  {SNB,                 "SNB",                  "SNB",          MIDI_KEY   | CONTROLLER_SWITCH},
  {SPLIT,               "Split",                "SPLIT",        MIDI_KEY   | CONTROLLER_SWITCH},
  {SQUELCH,             "Squelch",              "SQUELCH",      MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {SQUELCH_RX1,         "Squelch\nRX1",         "SQUELCH1",     MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {SQUELCH_RX2,         "Squelch\nRX2",         "SQUELCH2",     MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {SWAP_RX,             "Swap RX",              "SWAPRX",       MIDI_KEY   | CONTROLLER_SWITCH},
  {TOOLBAR1,            "ToolBar1",             "TBAR1",        MIDI_KEY   | CONTROLLER_SWITCH},
  {TOOLBAR2,            "ToolBar2",             "TBAR2",        MIDI_KEY   | CONTROLLER_SWITCH},
  {TOOLBAR3,            "ToolBar3",             "TBAR3",        MIDI_KEY   | CONTROLLER_SWITCH},
  {TOOLBAR4,            "ToolBar4",             "TBAR4",        MIDI_KEY   | CONTROLLER_SWITCH},
  {TOOLBAR5,            "ToolBar5",             "TBAR5",        MIDI_KEY   | CONTROLLER_SWITCH},
  {TOOLBAR6,            "ToolBar6",             "TBAR6",        MIDI_KEY   | CONTROLLER_SWITCH},
  {TOOLBAR7,            "ToolBar7",             "TBAR7",        MIDI_KEY   | CONTROLLER_SWITCH},
  {TOOLBAR8,            "ToolBar8",             "TBAR8",        MIDI_KEY   | CONTROLLER_SWITCH},
  {TUNE,                "Tune",                 "TUNE",         MIDI_KEY   | CONTROLLER_SWITCH},
  {TUNE_IOB,            "Tune IOB",             "TUNE-IOB",     MIDI_KEY   | CONTROLLER_SWITCH},
  {TUNE_DRIVE,          "Tune\nDrv",            "TUNDRV",       MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {TUNE_FULL,           "Tune\nFull",           "TUNF",         MIDI_KEY   | CONTROLLER_SWITCH},
  {TUNE_MEMORY,         "Tune\nMem",            "TUNM",         MIDI_KEY   | CONTROLLER_SWITCH},
#ifdef __AH4IOB__
  {AH4_RUN,             "AH-4\nRun",            "AH4-RUN",      MIDI_KEY   | CONTROLLER_SWITCH},
  {AH4_BYP,             "AH-4\nBypass",         "AH4-BYP",      MIDI_KEY   | CONTROLLER_SWITCH},
#endif
  {DRIVE,               "TX Drive",             "TXDRV",        MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {TWO_TONE,            "Two-Tone",             "2TONE",        MIDI_KEY   | CONTROLLER_SWITCH},
  {MENU_TX,             "TX\nMenu",             "TX-M",         MIDI_KEY   | CONTROLLER_SWITCH},
  {VFO,                 "VFO",                  "VFO",          MIDI_WHEEL | CONTROLLER_ENCODER},
  {MENU_FREQUENCY,      "VFO\nMenu",            "FREQ",         MIDI_KEY   | CONTROLLER_SWITCH},
  {VFO_STEP_MINUS,      "VFO Step -",           "STEP-",        MIDI_KEY   | CONTROLLER_SWITCH},
  {VFO_STEP_PLUS,       "VFO Step +",           "STEP+",        MIDI_KEY   | CONTROLLER_SWITCH},
  {VFOA,                "VFO A",                "VFOA",         MIDI_WHEEL | CONTROLLER_ENCODER},
  {VFOB,                "VFO B",                "VFOB",         MIDI_WHEEL | CONTROLLER_ENCODER},
  {VOX,                 "VOX\nOn/Off",          "VOX",          MIDI_KEY   | CONTROLLER_SWITCH},
  {VOXLEVEL,            "VOX\nLevel",           "VOXLEV",       MIDI_WHEEL | CONTROLLER_ENCODER},
  {WATERFALL_HIGH,      "Wfall\nHigh",          "WFALLH",       MIDI_WHEEL | CONTROLLER_ENCODER},
  {WATERFALL_LOW,       "Wfall\nLow",           "WFALLL",       MIDI_WHEEL | CONTROLLER_ENCODER},
  {XIT,                 "XIT",                  "XIT",          MIDI_WHEEL | CONTROLLER_ENCODER},
  {XIT_CLEAR,           "XIT\nClear",           "XITCL",        MIDI_KEY   | CONTROLLER_SWITCH},
  {XIT_ENABLE,          "XIT\nOn/Off",          "XITT",         MIDI_KEY   | CONTROLLER_SWITCH},
  {XIT_MINUS,           "XIT -",                "XIT-",         MIDI_KEY   | CONTROLLER_SWITCH},
  {XIT_PLUS,            "XIT +",                "XIT+",         MIDI_KEY   | CONTROLLER_SWITCH},
  {ZOOM,                "Zoom",                 "ZOOM",         MIDI_KNOB  | MIDI_WHEEL | CONTROLLER_ENCODER},
  {ZOOM_MINUS,          "Zoom -",               "ZOOM-",        MIDI_KEY   | CONTROLLER_SWITCH},
  {ZOOM_PLUS,           "Zoom +",               "ZOOM+",        MIDI_KEY   | CONTROLLER_SWITCH},
  {ACTIONS,             "None",                 "NONE",         TYPE_NONE}
};

//
// Supporting repeated actions if a key is pressed for a long time:
//
// In this case, a repeat timer is  initiated. Since there casen only by
// one repeat timer active at one moment, we can use static storage to
// 'remember' the action.
// The benefit of this is that there is no need to defer the g_free o a
// recently allocated PROCESS_ACTION structure.
//
static guint repeat_timer = 0;
static gboolean repeat_timer_released;
static PROCESS_ACTION repeat_action;
static gboolean multi_select_active;
static gboolean multi_first = TRUE;
static unsigned int multi_action = 0;
#define VMAXMULTIACTION 28

//
// The strings in the following table are chosen
// as to occupy minimum space in the VFO bar
//
MULTI_TABLE multi_action_table[] = {
  {AF_GAIN,          "AFgain"},
  {AGC_GAIN,         "AGC"},
  {ATTENUATION,      "Att"},
  {COMPRESSION,      "Cmpr"},
  {CW_FREQUENCY,     "CWfrq"},
  {CW_SPEED,         "CWspd"},
  {DIV_GAIN,         "DivG"},
  {DIV_PHASE,        "DivP"},
  {FILTER_CUT_LOW,   "FCutL"},
  {FILTER_CUT_HIGH,  "FCutH"},
  {IF_SHIFT,         "IFshft"},
  {IF_WIDTH,         "IFwid"},
  {LINEIN_GAIN,      "LineIn"},
  {MIC_GAIN,         "Mic"},
  {PAN,              "Pan"},
  {PANADAPTER_HIGH,  "PanH"},
  {PANADAPTER_LOW,   "PanL"},
  {PANADAPTER_STEP,  "PanStp"},
  {RF_GAIN,          "RFgain"},
  {RIT,              "RIT"},
  {SQUELCH,          "Sqlch"},
  {TUNE_DRIVE,       "TunDrv"},
  {DRIVE,            "Drive"},
  {VOXLEVEL,         "VOX"},
  {WATERFALL_HIGH,   "WfallH"},
  {WATERFALL_LOW,    "WFallL"},
  {XIT,              "XIT"},
  {ZOOM,             "Zoom"}
};

static gboolean get_window_frame(GtkWindow *win, int *x, int *y, int *w, int *h) {
  GdkWindow *gdk_win = gtk_widget_get_window(GTK_WIDGET(win));

  if (!gdk_win) {
    return FALSE;  /* Fenster noch nicht realisiert */
  }

  GdkRectangle rect;
  gdk_window_get_frame_extents(gdk_win, &rect);

  if (x) { *x = rect.x; }

  if (y) { *y = rect.y; }

  if (w) { *w = rect.width; }

  if (h) { *h = rect.height; }

  return TRUE;
}

static gboolean repeat_cb(gpointer data) {
  //
  // This is periodically called to execute the same action
  // again and agin (e.g. while the RIT button is kept being
  // pressed. The action is stored in repeat_action.
  //
  if (repeat_timer_released) {
    repeat_timer = 0;
    return G_SOURCE_REMOVE;
  }

  PROCESS_ACTION *a = g_new(PROCESS_ACTION, 1);
  *a = repeat_action;
  process_action(a);                 // übernimmt g_free()
  return G_SOURCE_CONTINUE;
}

#ifdef __AH4IOB__
static void stop_repeat_action(void) {
  repeat_timer_released = TRUE;

  if (repeat_timer != 0) {
    g_source_remove(repeat_timer);
    repeat_timer = 0;
  }

  t_print("g_timeout_stop\n");
}

static void start_repeat_action(PROCESS_ACTION *repaction, guint timerval) {
  static guint savedtimerval = 0;
  repeat_action = *repaction;

  if (repeat_timer == 0) {
    // Why not use gpointer data for repeat_action ?
    repeat_timer = g_timeout_add(timerval, repeat_cb, NULL);
    repeat_timer_released = FALSE;
    t_print("g_timeout_add %d\n", timerval);
  } else {
    if (savedtimerval != timerval) {
      stop_repeat_action();
      repeat_timer = g_timeout_add(timerval, repeat_cb, NULL);
      repeat_timer_released = FALSE;
      t_print("g_timeout_readd %d\n", timerval);
    }
  }

  savedtimerval = timerval;
}
#endif

static inline double KnobOrWheel(const PROCESS_ACTION *a, double oldval, double minval, double maxval, double inc) {
  //
  // Knob ("Potentiometer"):  set value
  // Wheel("Rotary Encoder"): increment/decrement the value (by "inc" per tick)
  //
  // In both cases, the returned value is
  //  - in the range minval...maxval
  //  - rounded to a multiple of inc
  //
  switch (a->mode) {
  case RELATIVE:
    oldval += a->val * inc;
    break;

  case ABSOLUTE:
    // The magic floating point  constant is 1/127
    oldval = minval + a->val * (maxval - minval) * 0.00787401574803150;
    break;

  default:
    // do nothing
    break;
  }

  //
  // Round and check range
  //
  oldval = inc * round(oldval / inc);

  if (oldval > maxval) { oldval = maxval; }

  if (oldval < minval) { oldval = minval; }

  return oldval;
}

//
// This interface puts an "action" into the GTK idle queue,
// but "CW key" actions are processed immediately
//
void schedule_action(enum ACTION action, enum ACTION_MODE mode, int val) {
  PROCESS_ACTION *a;

  switch (action) {
  case CW_LEFT:
  case CW_RIGHT:
    cw_key_hit = 1;
    keyer_event(action == CW_LEFT, mode == PRESSED);
    break;

  case CW_KEYER_KEYDOWN:

    //
    // hard "key-up/down" action WITHOUT break-in
    // intended for external keyers (MIDI or GPIO connected)
    // which take care of PTT themselves.
    //
    if (mode == PRESSED && (!cw_keyer_internal || MIDI_cw_is_active)) {
#ifdef GPIO
      gpio_set_cw(1);
#endif
      cw_key_down = 960000; // max. 20 sec to protect hardware
      cw_key_up = 0;
      cw_key_hit = 1;
    } else {
#ifdef GPIO
      gpio_set_cw(0);
#endif
      cw_key_down = 0;
      cw_key_up = 0;
    }

    break;

  default:
    //
    // schedule action through GTK idle queue
    //
    a = g_new(PROCESS_ACTION, 1);
    a->action = action;
    a->mode = mode;
    a->val = val;
    g_idle_add(process_action, a);
    break;
  }
}

int process_action(void *data) {
  PROCESS_ACTION *a = (PROCESS_ACTION *)data;
  double value;
  int i;

  //t_print("%s: a=%p action=%d mode=%d value=%d\n",__func__,a,a->action,a->mode,a->val);
  switch (a->action) {
  case A_SWAP_B:
    if (a->mode == PRESSED) {
      vfo_a_swap_b();
    }

    break;

  case A_TO_B:
    if (a->mode == PRESSED) {
      vfo_a_to_b();
    }

    break;

  case AF_GAIN:
    value = KnobOrWheel(a, active_receiver->volume, -40.0, 0.0, 1.0);
    set_af_gain(active_receiver->id, value);
    break;

  case AF_GAIN_RX1:
    value = KnobOrWheel(a, receiver[0]->volume, -40.0, 0.0, 1.0);
    set_af_gain(0, value);
    break;

  case AF_GAIN_RX2:
    if (receivers == 2) {
      value = KnobOrWheel(a, receiver[1]->volume, -40.0, 0.0, 1.0);
      set_af_gain(1, value);
    }

    break;

  case AGC:
    if (a->mode == PRESSED) {
      active_receiver->agc++;

      if (active_receiver->agc >= AGC_LAST) {
        active_receiver->agc = 0;
      }

      rx_set_agc(active_receiver);
      g_idle_add(ext_vfo_update, NULL);
      update_slider_agc_btn();
    }

    break;

  case AGC_GAIN:
    value = KnobOrWheel(a, active_receiver->agc_gain, -20.0, 120.0, 1.0);
    set_agc_gain(active_receiver->id, value);
    break;

  case AGC_GAIN_RX1:
    value = KnobOrWheel(a, receiver[0]->agc_gain, -20.0, 120.0, 1.0);
    set_agc_gain(0, value);
    break;

  case AGC_GAIN_RX2:
    if (receivers == 2) {
      value = KnobOrWheel(a, receiver[1]->agc_gain, -20.0, 120.0, 1.0);
      set_agc_gain(1, value);
    }

    break;

  case ATU_WIN:
    if (a->mode == PRESSED) {
      open_atu_window(GTK_WINDOW(top_window),
                      atuwin_TITLE,
                      atuwin_URL);
    }

    break;

  case DXC_WIN:
    if (a->mode == PRESSED) {
      int twx, twy, tww, twh;

      if (dxcwin_x < 0 || dxcwin_y < 0 || dxcwin_w < 0 || dxcwin_h < 0) {
        if (!full_screen && get_window_frame(GTK_WINDOW(top_window), &twx, &twy, &tww, &twh)) {
          t_print("twx=%d twy=%d tww=%d twh=%d\n", twx, twy, tww, twh);
          dxcluster_open_window(
            dxc_address,
            dxc_port,
            dxc_login,
#ifdef __APPLE__
            tww / 2,        // width
#else
            tww * 2 / 3,    // width
#endif
            200,            // height
            twx,            // pos_x
#ifdef __APPLE__
            twy + twh + 30  // pos_y
#else
            twy + twh       // pos_y
#endif
          );
        } // calculated win
      } else {
        dxcluster_open_window(
          dxc_address,
          dxc_port,
          dxc_login,
          dxcwin_w,
          dxcwin_h,
          dxcwin_x,
          dxcwin_y
        );
      }
    } // main

    break;

  case DXC_TEST:
    if (a->mode == PRESSED) {
      // Testdrive
      // pan_add_label_timeout(7100000LL, "DL1BZ", 30000);
      // pan_add_label_timeout(7101000LL, "DL2BZ", 20000);
      // pan_add_label_timeout(7102000LL, "DL3BZ", 10000);
    }

    break;

  case ANF:
    if (a->mode == PRESSED) {
      int id = active_receiver->id;
      TOGGLE(active_receiver->anf);

      if (id == 0) {
        int mode = vfo[id].mode;
        mode_settings[mode].anf = active_receiver->anf;
        copy_mode_settings(mode);
      }

      update_noise();
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case ATTENUATION:
    if (have_rx_att) {
      value = KnobOrWheel(a, adc[active_receiver->adc].attenuation,   0.0, 31.0, 1.0);
      set_attenuation_value(value);
    }

    break;

  case B_TO_A:
    if (a->mode == PRESSED) {
      vfo_b_to_a();
    }

    break;

  case BAND_10:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band10);
    }

    break;

  case BAND_12:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band12);
    }

    break;

  case BAND_1240:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band1240);
    }

    break;

  case BAND_144:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band144);
    }

    break;

  case BAND_15:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band15);
    }

    break;

  case BAND_160:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band160);
    }

    break;

  case BAND_17:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band17);
    }

    break;

  case BAND_20:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band20);
    }

    break;

  case BAND_220:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band220);
    }

    break;

  case BAND_2300:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band2300);
    }

    break;

  case BAND_30:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band30);
    }

    break;

  case BAND_3400:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band3400);
    }

    break;

  case BAND_40:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band40);
    }

    break;

  case BAND_430:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band430);
    }

    break;

  case BAND_6:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band6);
    }

    break;

  case BAND_60:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band60);
    }

    break;

  case BAND_70:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band70);
    }

    break;

  case BAND_80:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band80);
    }

    break;

  case BAND_902:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band902);
    }

    break;

  case BAND_AIR:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, bandAIR);
    }

    break;

  case BAND_GEN:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, bandGen);
    }

    break;

  case BAND_136:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, band136);
    }

    break;

  case BAND_MINUS:
    if (a->mode == PRESSED) {
      band_minus(active_receiver->id);
    }

    break;

  case BAND_PLUS:
    if (a->mode == PRESSED) {
      band_plus(active_receiver->id);
    }

    break;

  case BAND_WWV:
    if (a->mode == PRESSED) {
      vfo_band_changed(active_receiver->id, bandWWV);
    }

    break;

  case BANDSTACK_MINUS:
    if (a->mode == PRESSED) {
      const BAND *band = band_get_band(vfo[active_receiver->id].band);
      const BANDSTACK *bandstack = band->bandstack;
      int b = vfo[active_receiver->id].bandstack - 1;

      if (b < 0) { b = bandstack->entries - 1; };

      vfo_bandstack_changed(b);
    }

    break;

  case BANDSTACK_PLUS:
    if (a->mode == PRESSED) {
      const BAND *band = band_get_band(vfo[active_receiver->id].band);
      const BANDSTACK *bandstack = band->bandstack;
      int b = vfo[active_receiver->id].bandstack + 1;

      if (b >= bandstack->entries) { b = 0; }

      vfo_bandstack_changed(b);
    }

    break;

  case BI_NAURAL:
    if (!radio_is_transmitting() && a->mode == PRESSED) {
      TOGGLE(active_receiver->binaural);
      rx_set_af_binaural(active_receiver);
    }

    break;

  case REPLAY:
  case CAPTURE:
    if (a->mode == PRESSED) {
      switch (capture_state) {
      case CAP_INIT:
        //
        // Hitting "capture" during TX when nothing has ever been
        // recorded moves us to CAP_AVAIL with an empty buffer.
        //
        capture_data = g_new(double, capture_max);
        capture_record_pointer = 0;
        capture_replay_pointer = 0;
        capture_state = CAP_AVAIL;
        break;

      case CAP_AVAIL:

        //
        // In this state, a recording is already in memory, so we can
        // either play-back (TX) or start a new recording (RX)
        //
        if (radio_is_transmitting()) {
          if (can_transmit) {
            radio_start_xmit_captured_data();  // adjust Mic gain etc.
            capture_replay_pointer = 0;
            capture_trigger_action = a->action;   // CAPTURE oder REPLAY
            capture_state = CAP_XMIT;
          }
        } else {
          if (a->action == CAPTURE) {
            radio_start_capture();
            capture_record_pointer = 0;
            capture_trigger_action = CAPTURE;
            capture_state = CAP_RECORDING;
          } else {
            capture_replay_pointer = 0;
            capture_trigger_action = REPLAY;
            capture_state = CAP_REPLAY;
          }
        }

        break;

      case CAP_RECORDING:
      case CAP_RECORD_DONE:
        //
        // The two states only differ in whether recording stops due
        // to user request (CAP_RECORDING) or because the audio
        // buffer was full (CAP_RECORD_DONE)
        //
        radio_end_capture();
        capture_state = CAP_AVAIL;
        break;

      case CAP_XMIT:
      case CAP_XMIT_DONE:

        //
        // The two states only differ in whether playback stops due
        // to user request (CAP_XMIT) or because the entire recording
        // has been re-played.
        //
        if (can_transmit) {
          radio_end_xmit_captured_data();  // restore Mic gain etc.
        }

        capture_state = CAP_AVAIL;
        break;

      case CAP_REPLAY:
      case CAP_REPLAY_DONE:
        //
        // Replay stops, either due to user request, or since
        // all data has been replayed
        //
        capture_state = CAP_AVAIL;
        break;

      case CAP_GOTOSLEEP:
        //
        // Called after a timeout
        //
        capture_state = CAP_SLEEPING;
        break;

      case CAP_SLEEPING:
        //
        // Data is available but this is not displayed. Go back to
        // CAP_AVAIL
        //
        capture_state = CAP_AVAIL;
        break;
      }
    }

    break;

  case COMP_ENABLE:
    if (can_transmit && a->mode == PRESSED) {
      int mode = vfo_get_tx_mode();
      TOGGLE(transmitter->compressor);
      mode_settings[mode].compressor = transmitter->compressor;
      copy_mode_settings(mode);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case COMPRESSION:
    if (can_transmit) {
      int mode = vfo_get_tx_mode();
      value = KnobOrWheel(a, transmitter->compressor_level, 0.0, 20.0, 1.0);
      transmitter->compressor = SET(value > 0.5);
      transmitter->compressor_level = value;
      mode_settings[mode].compressor = transmitter->compressor;
      mode_settings[mode].compressor_level = transmitter->compressor_level;
      copy_mode_settings(mode);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case CTUN:
    if (a->mode == PRESSED) {
      vfo_ctun_update(active_receiver->id, NOT(vfo[active_receiver->id].ctun));
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case CW_AUDIOPEAKFILTER:
    if (a->mode == PRESSED) {
      TOGGLE(vfo[active_receiver->id].cwAudioPeakFilter);
      rx_filter_changed(active_receiver);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case CW_FREQUENCY:
    value = KnobOrWheel(a, (double)cw_keyer_sidetone_frequency, 300.0, 1000.0, 10.0);
    cw_keyer_sidetone_frequency = (int)value;
    rx_filter_changed(active_receiver);
    // we may omit the P2 high-prio packet since this is sent out at regular intervals
    g_idle_add(ext_vfo_update, NULL);
    break;

  case CW_SPEED:
    value = KnobOrWheel(a, (double)cw_keyer_speed, 1.0, 60.0, 1.0);
    cw_keyer_speed = (int)value;
    keyer_update();
    g_idle_add(ext_vfo_update, NULL);
    break;

  case DIV:
    if (a->mode == PRESSED && n_adc > 1) {
      set_diversity(NOT(diversity_enabled));
    }

    break;

  case DIV_GAIN:
    set_diversity_gain(div_gain + (double)a->val * 0.05);
    break;

  case DIV_GAIN_COARSE:
    set_diversity_gain(div_gain + (double)a->val * 0.25);
    break;

  case DIV_GAIN_FINE:
    set_diversity_gain((double)a->val * 0.01);
    break;

  case DIV_PHASE:
    set_diversity_phase(div_phase + (double)a->val * 0.5);
    break;

  case DIV_PHASE_COARSE:
    set_diversity_phase(div_phase + (double)a->val * 2.5);
    break;

  case DIV_PHASE_FINE:
    set_diversity_phase(div_phase + (double)a->val * 0.1);
    break;

  case DRIVE:
    value = KnobOrWheel(a, radio_get_drive(), 0.0, drive_max, 1.0);
    set_drive(value);
    break;

  case DUPLEX:

    //
    // Ignore DUPLEX action while transmitting
    //
    if (can_transmit && !radio_is_transmitting() && a->mode == PRESSED) {
      TOGGLE(duplex);
      g_idle_add(ext_set_duplex, NULL);  // can just use setDuplex ?
    }

    break;

  case FILTER_MINUS:

    // since the widest filters start at f=0, FILTER_MINUS actually
    // cycles upwards
    if (a->mode == PRESSED) {
      int f = vfo[active_receiver->id].filter + 1;

      if (f >= FILTERS) { f = 0; }

      vfo_filter_changed(f);
    }

    break;

  case FILTER_PLUS:

    // since the widest filters start at f=0, FILTER_PLUS actually
    // cycles downwards
    if (a->mode == PRESSED) {
      int f = vfo[active_receiver->id].filter - 1;

      if (f < 0) { f = FILTERS - 1; }

      vfo_filter_changed(f);
    }

    break;

  case FILTER_CUT_HIGH: {
    filter_high_changed(active_receiver->id, a->val);
  }
  break;

  case FILTER_CUT_LOW: {
    filter_low_changed(active_receiver->id, a->val);
  }
  break;

  case FILTER_CUT_DEFAULT:
    if (a->mode == PRESSED) {
      filter_cut_default(active_receiver->id);
    }

    break;

  case FUNCTION:
    if (a->mode == PRESSED) {
      function++;

      if (function >= MAX_FUNCTIONS) {
        function = 0;
      }

      toolbar_switches = switches_controller1[function];
      update_toolbar_labels();

      if (controller == CONTROLLER1) {
        switches = switches_controller1[function];
      }
    }

    break;

  case FUNCTIONREV:
    if (a->mode == PRESSED) {
      function--;

      if (function < 0) {
        function = MAX_FUNCTIONS - 1;
      }

      toolbar_switches = switches_controller1[function];
      update_toolbar_labels();

      if (controller == CONTROLLER1) {
        switches = switches_controller1[function];
      }
    }

    break;

  case GL_WIN:
    if (a->mode == PRESSED) {
      // open_greyline_win(window_width, window_height, locator);
      open_greyline_win((int)display_width * 2 / 3, own_locator);
    }

    break;

  case F_SCREEN:
    if (a->mode == PRESSED) {
      TOGGLE(full_screen);
      radio_reconfigure_screen();
    }

    break;

  case IF_SHIFT:
    filter_shift_changed(active_receiver->id, a->val);
    break;

  case IF_SHIFT_RX1:
    filter_shift_changed(0, a->val);
    break;

  case IF_SHIFT_RX2:
    filter_shift_changed(1, a->val);
    break;

  case IF_WIDTH:
    filter_width_changed(active_receiver->id, a->val);
    break;

  case IF_WIDTH_RX1:
    filter_width_changed(0, a->val);
    break;

  case IF_WIDTH_RX2:
    filter_width_changed(1, a->val);
    break;

  case LINEIN_GAIN:
    value = KnobOrWheel(a, linein_gain, -34.0, 12.5, 1.5);
    set_linein_gain(value);
    break;

  case LOCK:
    if (a->mode == PRESSED) {
      TOGGLE(locked);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case MENU_AGC:
    if (a->mode == PRESSED) {
      start_agc();
    }

    break;

  case MENU_BAND:
    if (a->mode == PRESSED) {
      start_band();
    }

    break;

  case MENU_BANDSTACK:
    if (a->mode == PRESSED) {
      start_bandstack();
    }

    break;

  case MENU_DIVERSITY:
    if (a->mode == PRESSED) {
      start_diversity();
    }

    break;

  case MENU_FILTER:
    if (a->mode == PRESSED) {
      start_filter();
    }

    break;

  case MENU_FREQUENCY:
    if (a->mode == PRESSED) {
      start_vfo(active_receiver->id);
    }

    break;

  case MENU_MAIN:
    if (a->mode == PRESSED) {
      new_menu();
    }

    break;

  case MENU_MEMORY:
    if (a->mode == PRESSED) {
      start_store();
    }

    break;

  case MENU_MODE:
    if (a->mode == PRESSED) {
      start_mode();
    }

    break;

  case MENU_NOISE:
    if (a->mode == PRESSED) {
      start_noise();
    }

    break;

  case MENU_PS:
    if (a->mode == PRESSED) {
      start_ps();
    }

    break;

  case MENU_RX:
    if (a->mode == PRESSED) {
      start_rx();
    }

    break;

  case MENU_TX:
    if (a->mode == PRESSED) {
      start_tx();
    }

    break;

  case MIC_GAIN:
    if (can_transmit) {
      value = KnobOrWheel(a, transmitter->mic_gain, -12.0, 50.0, 1.0);
      set_mic_gain(value);
    }

    break;

  case MODE_MINUS:
    if (a->mode == PRESSED) {
      int mode = vfo[active_receiver->id].mode;
      mode--;

      if (mode < 0) { mode = MODES - 1; }

      vfo_mode_changed(mode);
    }

    break;

  case MODE_PLUS:
    if (a->mode == PRESSED) {
      int mode = vfo[active_receiver->id].mode;
      mode++;

      if (mode >= MODES) { mode = 0; }

      vfo_mode_changed(mode);
    }

    break;

  case MOX:
    if (a->mode == PRESSED) {
      int state = radio_get_mox();
      radio_mox_update(!state);
      //t_print("MOX pressed; bool = %d\n", (int)!state);
    }

    break;

  case MULTI_BUTTON:                  // swap multifunction from implementing an action, and choosing which action is assigned
    if (a->mode == PRESSED) {
      multi_first = FALSE;
      multi_select_active = !multi_select_active;
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  // multifunction encoder. If multi_select_active, it edits the assigned action; else implements assigned action.
  case MULTI_ENC:
    multi_first = FALSE;

    if (multi_select_active) {
      multi_action = KnobOrWheel(a, multi_action, 0, VMAXMULTIACTION - 1, 1);
      g_idle_add(ext_vfo_update, NULL);
    } else {
      PROCESS_ACTION *multifunction_action;
      multifunction_action = g_new(PROCESS_ACTION, 1);
      multifunction_action->mode = a->mode;
      multifunction_action->val = a->val;
      multifunction_action->action = multi_action_table[multi_action].action;
      process_action((void*)multifunction_action);
    }

    g_idle_add(ext_vfo_update, NULL);
    break;

  case MULTI_SELECT:                // know to choose the action for multifunction endcoder
    multi_first = FALSE;
    multi_action = KnobOrWheel(a, multi_action, 0, VMAXMULTIACTION - 1, 1);
    g_idle_add(ext_vfo_update, NULL);
    break;

  case MUTE:
    if (a->mode == PRESSED) {
      active_receiver->mute_radio = !active_receiver->mute_radio;
      g_idle_add(ext_vfo_update, NULL);
      update_slider_af_gain_btn();
    }

    break;

  case MUTE_RX1:
    if (a->mode == PRESSED) {
      receiver[0]->mute_radio = !receiver[0]->mute_radio;
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case MUTE_RX2:
    if (a->mode == PRESSED && receivers > 1) {
      receiver[1]->mute_radio = !receiver[1]->mute_radio;
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case NB:
    if (a->mode == PRESSED) {
      int id = active_receiver->id;
      active_receiver->nb++;

      if (active_receiver->nb > 2) { active_receiver->nb = 0; }

      if (id == 0) {
        int mode = vfo[id].mode;
        mode_settings[mode].nb = active_receiver->nb;
        copy_mode_settings(mode);
      }

      update_noise();
    }

    break;

  case NR:
    if (a->mode == PRESSED) {
      int id = active_receiver->id;
      active_receiver->nr++;
#ifdef EXTNR

      if (active_receiver->nr > 4) { active_receiver->nr = 0; }

#else

      if (active_receiver->nr > 2) { active_receiver->nr = 0; }

#endif

      if (id == 0) {
        int mode = vfo[id].mode;
        mode_settings[mode].nr = active_receiver->nr;
        copy_mode_settings(mode);
      }

      update_noise();

      if (display_sliders && have_rx_gain) {
        update_slider_nr_btn();
      }
    }

    break;

  case NUMPAD_0:
    if (a->mode == PRESSED) {
      vfo_num_pad(0, active_receiver->id);
    }

    break;

  case NUMPAD_1:
    if (a->mode == PRESSED) {
      vfo_num_pad(1, active_receiver->id);
    }

    break;

  case NUMPAD_2:
    if (a->mode == PRESSED) {
      vfo_num_pad(2, active_receiver->id);
    }

    break;

  case NUMPAD_3:
    if (a->mode == PRESSED) {
      vfo_num_pad(3, active_receiver->id);
    }

    break;

  case NUMPAD_4:
    if (a->mode == PRESSED) {
      vfo_num_pad(4, active_receiver->id);
    }

    break;

  case NUMPAD_5:
    if (a->mode == PRESSED) {
      vfo_num_pad(5, active_receiver->id);
    }

    break;

  case NUMPAD_6:
    if (a->mode == PRESSED) {
      vfo_num_pad(6, active_receiver->id);
    }

    break;

  case NUMPAD_7:
    if (a->mode == PRESSED) {
      vfo_num_pad(7, active_receiver->id);
    }

    break;

  case NUMPAD_8:
    if (a->mode == PRESSED) {
      vfo_num_pad(8, active_receiver->id);
    }

    break;

  case NUMPAD_9:
    if (a->mode == PRESSED) {
      vfo_num_pad(9, active_receiver->id);
    }

    break;

  case NUMPAD_BS:
    if (a->mode == PRESSED) {
      vfo_num_pad(-6, active_receiver->id);
    }

    break;

  case NUMPAD_CL:
    if (a->mode == PRESSED) {
      vfo_num_pad(-1, active_receiver->id);
    }

    break;

  case NUMPAD_ENTER:
    if (a->mode == PRESSED) {
      vfo_num_pad(-2, active_receiver->id);
    }

    break;

  case NUMPAD_KHZ:
    if (a->mode == PRESSED) {
      vfo_num_pad(-3, active_receiver->id);
    }

    break;

  case NUMPAD_MHZ:
    if (a->mode == PRESSED) {
      vfo_num_pad(-4, active_receiver->id);
    }

    break;

  case NUMPAD_DEC:
    if (a->mode == PRESSED) {
      vfo_num_pad(-5, active_receiver->id);
    }

    break;

  case PAN:
    set_pan(active_receiver->id,  active_receiver->pan + 100 * a->val);
    break;

  case PAN_MINUS:
    if (a->mode == PRESSED) {
      set_pan(active_receiver->id,  active_receiver->pan - 100);
    }

    break;

  case PAN_PLUS:
    if (a->mode == PRESSED) {
      set_pan(active_receiver->id,  active_receiver->pan + 100);
    }

    break;

  case PANADAPTER_HIGH:
    value = KnobOrWheel(a, active_receiver->panadapter_high, -60.0, 20.0, 1.0);
    active_receiver->panadapter_high = (int)value;
    break;

  case PANADAPTER_LOW:
    value = KnobOrWheel(a, active_receiver->panadapter_low, -160.0, -60.0, 1.0);
    active_receiver->panadapter_low = (int)value;
    break;

  case PANADAPTER_STEP:
    value = KnobOrWheel(a, active_receiver->panadapter_step, 5.0, 30.0, 1.0);
    active_receiver->panadapter_step = (int)value;
    break;

  case PREAMP:
    break;

  case PS:
    if (a->mode == PRESSED) {
#if defined (__CPYMODE__)
      int _mode = vfo[active_receiver->id].mode;

      if (can_transmit) {
        // PS make no sense in CW and FM !
        if (_mode == modeUSB || _mode == modeLSB || _mode == modeDIGL || _mode == modeDIGU || _mode == modeAM
            || _mode == modeDSB) {
          tx_ps_onoff(transmitter, transmitter->puresignal ? 0 : 1);
          mode_settings[_mode].puresignal = transmitter->puresignal;
          copy_mode_settings(_mode);
        } else {
          mode_settings[_mode].puresignal = 0;
          copy_mode_settings(_mode);
        }
      }

#else

      if (can_transmit) {
        tx_ps_onoff(transmitter, transmitter->puresignal ? 0 : 1);
      }

#endif

      if (display_sliders && have_rx_gain) {
        update_slider_ps_btn();
      }
    }

    break;

  case PTT:
    if (a->mode == PRESSED || a->mode == RELEASED) {
      radio_mox_update(a->mode == PRESSED);
    }

    break;

  case QSPLT:
    if (a->mode == PRESSED) {
      long long f_vfo_a, f_vfo_b;
      int _mode = vfo[active_receiver->id].mode;
      f_vfo_a = vfo[VFO_A].ctun ? vfo[VFO_A].ctun_frequency : vfo[VFO_A].frequency;
      f_vfo_b = vfo[VFO_B].ctun ? vfo[VFO_B].ctun_frequency : vfo[VFO_B].frequency;

      if (_mode == modeUSB || _mode == modeLSB || _mode == modeDSB) {
        f_vfo_b = f_vfo_a + 5000.0;
      } else if (_mode == modeCWL || _mode == modeCWU) {
        f_vfo_b = f_vfo_a + 1000.0;
      }

      radio_set_split(1);
      vfo_a_to_b();
      vfo_set_frequency(VFO_B, f_vfo_b);
      g_idle_add(ext_vfo_update, NULL);
      t_print("QRG: VFO_A = %lld VFO_B = %lld\n", f_vfo_a, f_vfo_b);
    }

    break;

  case RCL0:
  case RCL1:
  case RCL2:
  case RCL3:
  case RCL4:
  case RCL5:
  case RCL6:
  case RCL7:
  case RCL8:
  case RCL9:
    if (a->mode == PRESSED) {
      recall_memory_slot(a->action - RCL0);
    }

    break;

  case RF_GAIN:
    if (have_rx_gain) {
      value = KnobOrWheel(a, adc[active_receiver->adc].gain, adc[active_receiver->adc].min_gain,
                          adc[active_receiver->adc].max_gain, 1.0);
      set_rf_gain(active_receiver->id, value);
    }

    break;

  case RF_GAIN_RX1:
    if (have_rx_gain) {
      value = KnobOrWheel(a, adc[receiver[0]->adc].gain, adc[receiver[0]->adc].min_gain, adc[receiver[0]->adc].max_gain, 1.0);
      set_rf_gain(0, value);
    }

    break;

  case RF_GAIN_RX2:
    if (have_rx_gain && receivers == 2) {
      value = KnobOrWheel(a, adc[receiver[1]->adc].gain, adc[receiver[1]->adc].min_gain, adc[receiver[1]->adc].max_gain, 1.0);
      set_rf_gain(1, value);
    }

    break;

  case RIT:
    if (a->mode == RELATIVE) {
      int id = active_receiver->id;
      vfo_rit_incr(id, vfo[id].rit_step * a->val);
    }

    break;

  case RIT_CLEAR:
    if (a->mode == PRESSED) {
      vfo_rit_value(active_receiver->id, 0);
    }

    break;

  case RIT_ENABLE:
    if (a->mode == PRESSED) {
      vfo_rit_toggle(active_receiver->id);
    }

    break;

  case RIT_MINUS:
    if (a->mode == PRESSED) {
      int id = active_receiver->id;
      vfo_rit_incr(id, -vfo[id].rit_step);

      if (repeat_timer == 0) {
        repeat_action = *a;
        repeat_timer = g_timeout_add(250, repeat_cb, NULL);
        repeat_timer_released = FALSE;
      }
    } else {
      // Stop immediately; otherwise a quick re-press can be blocked until the next timer tick.
      repeat_timer_released = TRUE;

      if (repeat_timer != 0) {
        g_source_remove(repeat_timer);
        repeat_timer = 0;
      }
    }

    break;

  case RIT_PLUS:
    if (a->mode == PRESSED) {
      int id = active_receiver->id;
      vfo_rit_incr(id, vfo[id].rit_step);

      if (repeat_timer == 0) {
        repeat_action = *a;
        repeat_timer = g_timeout_add(250, repeat_cb, NULL);
        repeat_timer_released = FALSE;
      }
    } else {
      repeat_timer_released = TRUE;

      if (repeat_timer != 0) {
        g_source_remove(repeat_timer);
        repeat_timer = 0;
      }
    }

    break;

  case RIT_RX1:
    vfo_rit_incr(0, vfo[0].rit_step * a->val);
    break;

  case RIT_RX2:
    vfo_rit_incr(1, vfo[1].rit_step * a->val);
    break;

  case RIT_STEP:
    if (a->mode == PRESSED) {
      int incr = 10 * vfo[active_receiver->id].rit_step;

      if (incr > 100) { incr = 100; }

      vfo_set_rit_step(incr);
    }

    break;

  case RITXIT:

    //
    // a RITXIT encoder automatically switches between RIT or XIT. It does XIT
    // if (and only if) RIT is disabled and XIT is enabled, otherwise it does RIT
    //
    if (a->mode == RELATIVE) {
      int id = active_receiver->id;

      if ((vfo[id].rit_enabled == 0) && (vfo[vfo_get_tx_vfo()].xit_enabled == 1)) {
        vfo_xit_incr(vfo[id].rit_step * a->val);
      } else {
        vfo_rit_incr(id, vfo[id].rit_step * a->val);
      }
    }

    break;

  case RITSELECT:

    //
    // An action which cycles between RIT on, XIT on, and both off.
    // This is intended to be used together with the RITXIT encoder
    //
    if (a->mode == PRESSED) {
      if ((vfo[active_receiver->id].rit_enabled == 0) && (vfo[vfo_get_tx_vfo()].xit_enabled == 0)) {
        vfo_rit_onoff(active_receiver->id, 1);
        vfo_xit_onoff(0);
      } else if ((vfo[active_receiver->id].rit_enabled == 1) && (vfo[vfo_get_tx_vfo()].xit_enabled == 0)) {
        vfo_rit_onoff(active_receiver->id, 0);
        vfo_xit_onoff(1);
      } else {
        vfo_rit_onoff(active_receiver->id, 0);
        vfo_xit_onoff(0);
      }
    }

    break;

  case RITXIT_CLEAR:
    if (a->mode == PRESSED) {
      vfo_rit_value(active_receiver->id, 0);
      vfo_xit_value(0);
    }

    break;

  case RX1:
    if (a->mode == PRESSED && receivers == 2) {
      rx_set_active(receiver[0]);
    }

    break;

  case RX2:
    if (a->mode == PRESSED && receivers == 2) {
      rx_set_active(receiver[1]);
    }

    break;

  case RSAT:
    if (a->mode == PRESSED) {
      radio_set_satmode (sat_mode == RSAT_MODE ? SAT_NONE : RSAT_MODE);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case SAT:
    if (a->mode == PRESSED) {
      radio_set_satmode (sat_mode == SAT_MODE ? SAT_NONE : SAT_MODE);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case SHUTDOWN:
    if (a->mode == PRESSED) {
      stop_program();
#ifdef __APPLE__
      (void) system("shutdown -h now");
#else
      (void) system("sudo shutdown -h -P now");
#endif
      _exit(0);
    }

    break;

  case SNB:
    if (a->mode == PRESSED) {
      int id = active_receiver->id;

      if (active_receiver->snb == 0) {
        active_receiver->snb = 1;

        if (id == 0) {
          int mode = vfo[id].mode;
          mode_settings[mode].snb = 1;
          copy_mode_settings(mode);
        }
      } else {
        active_receiver->snb = 0;

        if (id == 0) {
          int mode = vfo[id].mode;
          mode_settings[mode].snb = 0;
          copy_mode_settings(mode);
        }
      }

      update_noise();
    }

    break;

  case SPLIT:
    if (a->mode == PRESSED) {
      radio_split_toggle();
    }

    break;

  case SQUELCH:
    value = KnobOrWheel(a, active_receiver->squelch, 0.0, 100.0, 1.0);
    active_receiver->squelch = value;
    set_squelch(active_receiver);
    break;

  case SQUELCH_RX1:
    value = KnobOrWheel(a, receiver[0]->squelch, 0.0, 100.0, 1.0);
    receiver[0]->squelch = value;
    set_squelch(receiver[0]);
    break;

  case SQUELCH_RX2:
    if (receivers == 2) {
      value = KnobOrWheel(a, receiver[1]->squelch, 0.0, 100.0, 1.0);
      receiver[1]->squelch = value;
      set_squelch(receiver[1]);
    }

    break;

  case SWAP_RX:
    if (a->mode == PRESSED) {
      if (receivers == 2) {
        rx_set_active(receiver[active_receiver->id == 1 ? 0 : 1]);
      }
    }

    break;

  case TUNE:
    if (a->mode == PRESSED) {
      int state = radio_get_tune();
      radio_tune_update(!state);
      update_slider_tune_drive_btn();
    }

    break;

  case TUNE_IOB:
    if (a->mode == PRESSED) {
      int state = radio_get_tune();
      radio_tune_update(!state);
      update_slider_tune_drive_btn();

      if (device == DEVICE_HERMES_LITE2 && hl2_iob_is_present()) {
        if (!state) {
          // TUNE wird eingeschaltet
          hl2_iob_set_antenna_tuner(1); // "fire-and-forget"
        } else {
          // TUNE wird ausgeschaltet
          // kein "0" schreiben
        }
      } else {
        t_print("%s: No Hermes Lite 2 with IO board detected. No action.\n", __func__);
        break;
      }
    }

    break;
#ifdef __AH4IOB__

  case AH4_RUN: {
    // make sure that enable_hl2_atu_gateware is OFF, we use the ATU only with the IO board
    if (device == DEVICE_HERMES_LITE2 && enable_hl2_atu_gateware) {
      enable_hl2_atu_gateware = 0;
    }

    switch (a->mode) {
    case PRESSED: {
      int state     = radio_get_tune();
      int new_state = !state;

      if (device == DEVICE_HERMES_LITE2) {
        if (!state) {
          // TUNE war aus → neuer Tune-Versuch
          if (hl2_iob_get_antenna_tuner_status() >= 0xF0) {
            // Letzter Versuch endete im Error-State → blockieren, bis 0x00 kommt
            t_print("AH4: error lock active, reschedule and wait for status 0x00 before retry\n");
          } else {
            // sauberer Start: Tuner anstoßen, aber KEINE HF
            hl2_pa_enable_suppressed = 1;
            radio_tune_update(1);
            update_slider_tune_drive_btn();
            // sauberer Start: Tuner anstoßen, HF mit PA ausgeschaltet (hl2_pa_enable_suppressed)
            // HF mit PA ausgeschaltet ist eine Abhilfe für "modulation not immediately applied issue"
            hl2_iob_set_antenna_tuner(1);
            t_print("AH4: start sequence, waiting for 0xEE\n");
            a->mode = RELATIVE;
            start_repeat_action(a, 1);
          }
        } else {
          // TUNE war an → User beendet Tune manuell, HF aus
          radio_tune_update(0);
          update_slider_tune_drive_btn();
          hl2_pa_enable_suppressed = 0;
          t_print("AH4: user TUNE off\n");
        }
      } else {
        // Nicht-HL2 → unverändertes TUNE-Verhalten
        radio_tune_update(new_state);
        update_slider_tune_drive_btn();
      }

      break;
    }

    case RELATIVE: {
      // Poll-Schritt, nur für HL2 relevant
      if (device != DEVICE_HERMES_LITE2) {
        break;
      }

      // Kein IO-Board → Polling beenden
      if (!hl2_iob_is_present()) {
        t_print("AH4: no IO board present, abort polling\n");
        break;
      }

      unsigned char s = hl2_iob_get_antenna_tuner_status();

      if (s == 0xEE) {
        // HF erst bei 0xEE aktivieren
        if (hl2_pa_enable_suppressed) {
          hl2_pa_enable_suppressed = 0;
          t_print("AH4: RF on (0xEE)\n");
        }
      } else if (s == 0x00) {
        if (radio_get_tune() && !hl2_pa_enable_suppressed) {
          // HF war aktiv → sauberer Abschluss
          radio_tune_update(0);
          update_slider_tune_drive_btn();
          t_print("AH4: Tune end (OK)\n");
          stop_repeat_action();
          // hier kein weiteres schedule_action nötig
        }
      } else if (s >= 0xF0) {
        if (radio_get_tune()) {
          radio_tune_update(0);
          update_slider_tune_drive_btn();
          t_print("AH4: Errorcode 0x%02X, RF off, locked until 0x00\n", s);
        } else {
          t_print("AH4: Errorcode 0x%02X, locked until 0x00\n", s);
        }

        stop_repeat_action();
        hl2_pa_enable_suppressed = 0;
      }

      static unsigned char prevs = 0xFF;

      if (prevs != s) { t_print("AH4: Status 0x%02X\n", s); }

      prevs = s;
      break;
    }

    default:
      // RELEASED usw. ignorieren
      break;
    }

    break;
  }

  //----------------------------------------------------------------------------------------------------------

  case AH4_BYP:
    if (a->mode == PRESSED) {
      if (device == DEVICE_HERMES_LITE2 && hl2_iob_is_present()) {
        // AH-4 Bypass anfordern
        hl2_iob_set_antenna_tuner(2);
      }
    }

    break;
#endif

  case TUNE_DRIVE:
    if (can_transmit) {
      value = KnobOrWheel(a, (double) transmitter->tune_drive, 0.0, 100.0, 1.0);
      transmitter->tune_drive = (int) value;
      transmitter->tune_use_drive = 0;
      show_popup_slider(TUNE_DRIVE, 0, 0.0, 100.0, 1.0, value, "TUNE DRIVE");
    }

    break;

  case TUNE_FULL:
    if (a->mode == PRESSED) {
      if (can_transmit) {
        TOGGLE(full_tune);
        memory_tune = FALSE;
      }
    }

    break;

  case TUNE_MEMORY:
    if (a->mode == PRESSED) {
      if (can_transmit) {
        TOGGLE(memory_tune);
        full_tune = FALSE;
      }
    }

    break;

  case TWO_TONE:
    if (a->mode == PRESSED) {
      if (can_transmit) {
        tx_set_twotone(transmitter, NOT(transmitter->twotone));
      }
    }

    break;

  case VFO:
    if (a->mode == RELATIVE && !locked) {
      static int acc = 0;
      acc += (int) a->val;
      int new = acc / vfo_encoder_divisor;

      if (new != 0) {
        vfo_step(new);
        acc -= new*vfo_encoder_divisor;
      }
    }

    break;

  case VFO_STEP_MINUS:
    if (a->mode == PRESSED) {
      i = vfo_get_stepindex(active_receiver->id);
      vfo_set_step_from_index(active_receiver->id, --i);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case VFO_STEP_PLUS:
    if (a->mode == PRESSED) {
      i = vfo_get_stepindex(active_receiver->id);
      vfo_set_step_from_index(active_receiver->id, ++i);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case VFOA:
    if (a->mode == RELATIVE && !locked) {
      static int acc = 0;
      acc += (int) a->val;
      int new = acc / vfo_encoder_divisor;

      if (new != 0) {
        vfo_id_step(0, new);
        acc -= new*vfo_encoder_divisor;
      }
    }

    break;

  case VFOB:
    if (a->mode == RELATIVE && !locked) {
      static int acc = 0;
      acc += (int) a->val;
      int new = acc / vfo_encoder_divisor;

      if (new != 0) {
        vfo_id_step(1, new);
        acc -= new*vfo_encoder_divisor;
      }
    }

    break;

  case VOX:
    if (a->mode == PRESSED) {
      vox_enabled = !vox_enabled;
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case VOXLEVEL:
    vox_threshold = KnobOrWheel(a, vox_threshold, 0.0, 1.0, 0.01);
    break;

  case WATERFALL_HIGH:
    value = KnobOrWheel(a, active_receiver->waterfall_high, -100.0, 0.0, 1.0);
    active_receiver->waterfall_high = (int)value;
    break;

  case WATERFALL_LOW:
    value = KnobOrWheel(a, active_receiver->waterfall_low, -150.0, -50.0, 1.0);
    active_receiver->waterfall_low = (int)value;
    break;

  case XIT:
    vfo_xit_incr(vfo[vfo_get_tx_vfo()].rit_step * a->val);
    break;

  case XIT_CLEAR:
    if (a->mode == PRESSED) {
      vfo_xit_value(0);
    }

    break;

  case XIT_ENABLE:
    if (a->mode == PRESSED && can_transmit) {
      vfo_xit_toggle();
    }

    break;

  case XIT_MINUS:
    if (a->mode == PRESSED) {
      vfo_xit_incr(-10 * vfo[vfo_get_tx_vfo()].rit_step);

      if (repeat_timer == 0) {
        repeat_action = *a;
        repeat_timer = g_timeout_add(250, repeat_cb, NULL);
        repeat_timer_released = FALSE;
      }
    } else {
      repeat_timer_released = TRUE;

      if (repeat_timer != 0) {
        g_source_remove(repeat_timer);
        repeat_timer = 0;
      }
    }

    break;

  case XIT_PLUS:
    if (a->mode == PRESSED) {
      vfo_xit_incr(10 * vfo[vfo_get_tx_vfo()].rit_step);

      if (repeat_timer == 0) {
        repeat_action = *a;
        repeat_timer = g_timeout_add(250, repeat_cb, NULL);
        repeat_timer_released = FALSE;
      }
    } else {
      repeat_timer_released = TRUE;

      if (repeat_timer != 0) {
        g_source_remove(repeat_timer);
        repeat_timer = 0;
      }
    }

    break;

  case ZOOM:
    value = KnobOrWheel(a, active_receiver->zoom, 1.0, 8.0, 1.0);
    set_zoom(active_receiver->id, (int)  value);
    break;

  case ZOOM_MINUS:
    if (a->mode == PRESSED) {
      set_zoom(active_receiver->id, active_receiver->zoom - 1);
    }

    break;

  case ZOOM_PLUS:
    if (a->mode == PRESSED) {
      set_zoom(active_receiver->id, active_receiver->zoom + 1);
    }

    break;

  case CW_KEYER_PTT:

    //
    // This is a PTT signal from an external keyer (either MIDI or GPIO connected).
    // In addition to activating PTT, we have to set MIDI_cw_is_active to temporarily
    // enable CW from deskHPSDR even if CW is handled  in the radio.
    //
    // This is to support a configuration where a key is attached to (and handled in)
    // the radio, while a contest logger controls a keyer whose key up/down events
    // arrive via MIDI/GPIO.
    //
    // When KEYER_PTT is removed, clear MIDI_CW_is_active to (re-)allow CW being handled in the
    // radio. deskHPSDR then goes RX unless "Radio PTT" is seen, which indicates that either
    // a footswitch has been pushed, or that the radio went TX due to operating a Morse key
    // attached to the radio.
    // In both cases, deskHPSDR stays TX and the radio will induce the TX/RX transition by removing radio_ptt.
    //
    switch (a->mode) {
    case PRESSED:
      MIDI_cw_is_active = 1;         // disable "CW handled in radio"
      cw_key_hit = 1;                // this tells rigctl to abort CAT CW
      schedule_transmit_specific();
      radio_mox_update(1);
      break;

    case RELEASED:
      MIDI_cw_is_active = 0;         // enable "CW handled in radio", if it was selected
      schedule_transmit_specific();

      if (!radio_ptt) {
        radio_mox_update(0);
      }

      break;

    default:
      // should not happen
      break;
    }

    break;

  case CW_KEYER_SPEED:
    //
    // This is a MIDI message from a CW keyer. The MIDI controller
    // value maps 1:1 to the speed, but we keep it within limits.
    //
    i = a->val;

    if (i >= 1 && i <= 60) { cw_keyer_speed = i; }

    keyer_update();
    g_idle_add(ext_vfo_update, NULL);
    break;

  case NO_ACTION:
    // do nothing
    break;

  default:
    if (a->action >= 0 && a->action < ACTIONS) {
      t_print("%s: UNKNOWN PRESSED SWITCH ACTION %d (%s)\n", __func__, a->action, ActionTable[a->action].str);
    } else {
      t_print("%s: INVALID PRESSED SWITCH ACTION %d\n", __func__, a->action);
    }

    break;
  }

  g_free(data);
  return 0;
}

//
// Function to convert an internal action number to a unique string
// This is used to specify actions in the props files.
//
void Action2String(int id, char *str, size_t len) {
  if (id < 0 || id >= ACTIONS) {
    g_strlcpy(str, "NONE", len);
  } else {
    g_strlcpy(str, ActionTable[id].button_str, len);
  }
}

//
// Function to convert a string to an action number
// This is used to specify actions in the props files.
//
int String2Action(const char *str) {
  int i;

  for (i = 0; i < ACTIONS; i++) {
    if (!strcmp(str, ActionTable[i].button_str)) { return i; }
  }

  return NO_ACTION;
}

//
// function to get status for multifunction encoder
// status = 0: no multifunction encoder in use (no status)
// status = 1: "active" (normal) state (status in yellow)
// status = 2: "select" state (status in red)
//
int GetMultifunctionStatus(void){
  if (multi_first) {
    return 0;
  }

  return multi_select_active ? 2 : 1;
}

//
// function to get string for multifunction encoder
//
void GetMultifunctionString(char* str, size_t len) {
  g_strlcpy(str, "M=", len);
  g_strlcat(str, multi_action_table[multi_action].descr, len);
}
