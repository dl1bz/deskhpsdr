/* Copyright (C)
* 2019 - Christoph van Wüllen, DL1YCF
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

typedef struct _midi_device {
  char *name;
  int  active;
} MIDI_DEVICE;

#define MAX_MIDI_DEVICES 10

extern MIDI_DEVICE midi_devices[MAX_MIDI_DEVICES];
extern int n_midi_devices;

extern void get_midi_devices(void);
