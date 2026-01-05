/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#ifndef _PANADAPTER_H
#define _PANADAPTER_H

// int compare_doubles(const void *a, const void *b);
void panadapter_set_max_label_rows(int r);
void pan_add_label(long long freq, const char *text);
void pan_add_label_timeout(long long freq, const char *text, int lifetime_ms);
void pan_clear_labels(void);
void pan_add_dx_spot(double freq_khz, const char *dxcall);
void rx_panadapter_peak_hold_clear(RECEIVER *rx);
void rx_panadapter_update(RECEIVER* rx);
void rx_panadapter_init(RECEIVER *rx, int width, int height);
void display_panadapter_messages(cairo_t *cr, int width, unsigned int fps);
extern int g_noise_level;

#endif
