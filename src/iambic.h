/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 3
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef _IAMBIC_H
#define _IAMBIC_H

enum {
  CHECK = 0,
  STRAIGHT,
  PREDOT,
  SENDDOT,
  PREDASH,
  SENDDASH,
  DOTDELAY,
  DASHDELAY,
  LETTERSPACE,
  EXITLOOP
};

void keyer_event(int left, int state);
void keyer_update(void);
void keyer_close(void);
int  keyer_init(void);

#endif
