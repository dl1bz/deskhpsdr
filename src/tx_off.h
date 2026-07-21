/* Copyright (C)
* 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
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
*/

#ifndef _TX_OFF_H
#define _TX_OFF_H

typedef enum {
  TX_OFF_TARGET_NONE = 0,
  TX_OFF_TARGET_VOX,
  TX_OFF_TARGET_MOX
} TX_OFF_TARGET;

typedef void (*TX_OFF_COMPLETE_FUNC)(void);

struct _transmitter;

extern int  tx_off_request(TX_OFF_TARGET target, TX_OFF_COMPLETE_FUNC complete);
extern void tx_off_cancel(void);
extern void tx_off_cancel_target(TX_OFF_TARGET target);
extern int  tx_off_pending_target(TX_OFF_TARGET target);
extern int  tx_off_zero_input_samples(void);
extern int  tx_off_zero_input_block(void);
extern int  tx_off_output_enabled(void);
extern void tx_off_output_block(const struct _transmitter *tx);

#endif
