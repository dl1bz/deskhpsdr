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

#ifndef _CONTROLLER_MAPPING_H
#define _CONTROLLER_MAPPING_H

#define MAX_SWITCHES 16
#define MAX_FUNCTIONS 6

typedef struct _switch {
  gboolean switch_enabled;
  gboolean switch_pullup;
  int switch_address;
  int switch_function;
  gulong switch_debounce;
} SWITCH;

extern SWITCH switches_toolbar[MAX_FUNCTIONS][MAX_SWITCHES];

extern SWITCH *switches;

extern void RestoreActions (void);
extern void SaveActions (void);

#endif
