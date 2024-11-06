/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#ifndef _BANDSTACK_H
#define _BANDSTACK_H

/* --------------------------------------------------------------------------*/
/**
* @brief Bandstack definition
*/

//
// a) ctun and ctun_frequency added such that one returns to the "old"
//    setup when switching bands.
// b) variable filter frequencies removed because they were nowhere used
// c) CTCSS status added
//
struct _BANDSTACK_ENTRY {
  long long frequency;
  int ctun;
  long long ctun_frequency;
  int mode;
  int filter;
  int deviation;
  int ctcss_enabled;
  int ctcss;
};

typedef struct _BANDSTACK_ENTRY BANDSTACK_ENTRY;

struct _BANDSTACK {
  int entries;
  int current_entry;
  BANDSTACK_ENTRY *entry;
};

typedef struct _BANDSTACK BANDSTACK;

#endif

