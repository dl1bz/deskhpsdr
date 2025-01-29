/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT, 2016 - Steve Wilson, KA6S
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

#ifndef _STORE_H
#define _STORE_H

#include <gtk/gtk.h>
#include "bandstack.h"

/* --------------------------------------------------------------------------*/
/**
* @brief Band definition
*/
struct _MEM_STORE {
  long long frequency;
  long long ctun_frequency;
  int ctun;
  int mode;
  int filter;
  int deviation;
  int ctcss_enabled;
  int ctcss;
  int bd;
};

typedef struct _MEM_STORE MEM;

extern MEM mem[];
void memRestoreState(void);
void memSaveState(void);
void recall_memory_slot(int index);
void store_memory_slot(int index);

#endif
