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

#ifndef _SLIDERS_H
#define _SLIDERS_H

// include these since we are using RECEIVER and TRANSMITTER
#include "receiver.h"
#include "transmitter.h"
#include "actions.h"

extern void att_type_changed(void);
extern void update_c25_att(void);

extern int sliders_active_receiver_changed(void *data);
extern int update_mic_gain(void *);
extern int update_drive(void *);
extern int update_tune_drive(void *);

extern void set_agc_gain(int rx, double value);
extern void set_af_gain(int rx, double value);
extern void set_rf_gain(int rx, double value);
extern void set_mic_gain(double value);
extern void set_linein_gain(double value);
extern void set_drive(double drive);
extern void set_filter_cut_low(int rx, int value);
extern void set_filter_cut_high(int rx, int value);
extern void set_attenuation_value(double attenuation);
extern void set_filter_width(int rx, int width);
extern void set_filter_shift(int rx, int width);
extern GtkWidget *sliders_init(int my_width, int my_height);

extern void sliders_update(void);

extern void set_squelch(RECEIVER *rx);

extern void show_diversity_gain(void);
extern void show_diversity_phase(void);

void show_popup_slider(enum ACTION action, int rx, double min, double max, double delta, double value,
                       const char *title);

#endif
