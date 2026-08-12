/*  Copyright (C)
*   2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
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

//
//  Native CoreAudio output backend.
//

#ifndef _COREAUDIO_H
#define _COREAUDIO_H

#include "receiver.h"

extern void *coreaudio_output_open(RECEIVER *rx, const char *device_name, int *channels);
extern void coreaudio_output_close(void *handle);

extern void *coreaudio_input_open(const char *device_name);
extern void coreaudio_input_close(void *handle);

extern void *coreaudio_tci_monitor_open(const char *device_name, int *channels);
extern void coreaudio_tci_monitor_close(void *handle);
extern int coreaudio_output_is_alive(void *handle);
extern int coreaudio_input_is_alive(void *handle);
extern int coreaudio_tci_monitor_is_alive(void *handle);

extern int coreaudio_get_cards(void);

#endif
