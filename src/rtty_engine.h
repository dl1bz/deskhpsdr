/* Copyright (C)
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

#ifndef _RTTY_ENGINE_H
#define _RTTY_ENGINE_H

#include <stddef.h>

typedef enum {
  RTTY_FILL_MARK = 0,
  RTTY_FILL_LTRS = 1,
  RTTY_FILL_SPACE = 2
} RTTY_FILL_MODE;

void rtty_engine_init(void);
void rtty_engine_set_baud(double baud);
double rtty_engine_get_baud(void);
void rtty_engine_set_shift(int shift_hz);
int rtty_engine_get_shift(void);
void rtty_engine_set_reverse(int reverse);
int rtty_engine_get_reverse(void);
void rtty_engine_set_stop_bits(double stop_bits);
double rtty_engine_get_stop_bits(void);
void rtty_engine_set_uos(int enabled);
int rtty_engine_get_uos(void);
void rtty_engine_set_fill(RTTY_FILL_MODE fill);
RTTY_FILL_MODE rtty_engine_get_fill(void);
void rtty_engine_set_start_crlf(int enabled);
int rtty_engine_get_start_crlf(void);
void rtty_engine_set_idle_timeout(int seconds);
int rtty_engine_get_idle_timeout(void);
int rtty_engine_start(void);
int rtty_engine_queue_text(const char *text);
void rtty_engine_drain(void);
void rtty_engine_stop(void);
void rtty_engine_abort(void);
void rtty_engine_set_buffer_empty_callback(void (*callback)(void));
int rtty_engine_is_active(void);
void rtty_engine_render_iq(double *iq, int frames, int sample_rate, int txmode);

#endif
