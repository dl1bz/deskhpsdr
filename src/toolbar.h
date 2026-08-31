/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
* 2024-2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
* SPDX-License-Identifier: GPL-3.0-or-later
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

#include "controller_mapping.h"

extern int function;

extern SWITCH *toolbar_switches;

void update_toolbar_labels(void);

int toolbar_get_visible_rows(int my_width, int my_height);

int toolbar_get_height(int my_width, int my_height, int row_height);

gboolean toolbar_needs_rebuild(int my_width, int my_height, int window_height);

GtkWidget *toolbar_init(int my_width, int my_height, int window_height);

#endif
