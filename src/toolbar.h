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

#ifndef _TOOLBAR_H
#define _TOOLBAR_H

#include "gpio.h"

extern int function;

extern SWITCH *toolbar_switches;

void update_toolbar_labels(void);

void sim_mox_cb(GtkWidget *widget, gpointer data);
void sim_s1_pressed_cb(GtkWidget *widget, gpointer data);
void sim_s1_released_cb(GtkWidget *widget, gpointer data);
void sim_s2_pressed_cb(GtkWidget *widget, gpointer data);
void sim_s2_released_cb(GtkWidget *widget, gpointer data);
void sim_s3_pressed_cb(GtkWidget *widget, gpointer data);
void sim_s3_released_cb(GtkWidget *widget, gpointer data);
void sim_s4_pressed_cb(GtkWidget *widget, gpointer data);
void sim_s4_released_cb(GtkWidget *widget, gpointer data);
void sim_s5_pressed_cb(GtkWidget *widget, gpointer data);
void sim_s5_released_cb(GtkWidget *widget, gpointer data);
void sim_s6_pressed_cb(GtkWidget *widget, gpointer data);
void sim_s6_released_cb(GtkWidget *widget, gpointer data);
void sim_function_cb(GtkWidget *widget, gpointer data);

GtkWidget *toolbar_init(int my_width, int my_height);

#endif
