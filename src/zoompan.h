/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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

#ifndef _ZOOMPAN_H
#define _ZOOMPAN_H

#define MAX_ZOOM 8

extern GtkWidget *zoompan_init(int my_width, int my_height);
extern int zoompan_active_receiver_changed(void *data);

extern void set_pan(int rx, double value);
extern void set_zoom(int rx, double value);

extern void remote_set_pan(int rx, double value);
extern void remote_set_zoom(int rx, double value);
#endif
