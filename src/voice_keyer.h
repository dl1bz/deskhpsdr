/* Copyright (C)
* 2024-2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
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
#ifndef VOICE_KEYER_H
#define VOICE_KEYER_H

/* Public API */
void voice_keyer_show(void);

void voice_keyer_play_slot(int slot);   /* slot: 0..VK_SLOTS-1 */
void voice_keyer_stop(void);
int  voice_keyer_is_open(void);

/* Global source flag used by TX captured-data chain */
extern int is_vk;

#endif /* VOICE_KEYER_H */
