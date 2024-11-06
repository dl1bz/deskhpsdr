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

#ifndef _GPIO_H
#define _GPIO_H

#define MAX_ENCODERS 5
#define MAX_SWITCHES 16
#define MAX_FUNCTIONS 6

typedef struct _encoder {
  gboolean bottom_encoder_enabled;
  gboolean bottom_encoder_pullup;
  int bottom_encoder_address_a;
  int bottom_encoder_a_value;
  int bottom_encoder_address_b;
  int bottom_encoder_b_value;
  int bottom_encoder_pos;
  int bottom_encoder_function;
  guchar bottom_encoder_state;
  int top_encoder_enabled;
  gboolean top_encoder_pullup;
  int top_encoder_address_a;
  int top_encoder_a_value;
  int top_encoder_address_b;
  int top_encoder_b_value;
  int top_encoder_pos;
  int top_encoder_function;
  guchar top_encoder_state;
  gboolean switch_enabled;
  gboolean switch_pullup;
  int switch_address;
  int switch_function;
  gulong switch_debounce;
} ENCODER;

extern ENCODER *encoders;

typedef struct _switch {
  gboolean switch_enabled;
  gboolean switch_pullup;
  int switch_address;
  int switch_function;
  gulong switch_debounce;
} SWITCH;

extern SWITCH switches_controller1[MAX_FUNCTIONS][MAX_SWITCHES];

extern SWITCH *switches;

extern int *sw_action;

extern long settle_time;

extern void gpio_default_encoder_actions(int ctrlr);
extern void gpio_default_switch_actions(int ctrlr);
extern void gpio_set_defaults(int ctrlr);
extern void gpioRestoreActions(void);
extern void gpioRestoreState(void);
extern void gpioSaveState(void);
extern void gpioSaveActions(void);
extern int gpio_init(void);
extern void gpio_close(void);
extern void gpio_set_ptt(int state);
extern void gpio_set_cw(int state);

#endif
