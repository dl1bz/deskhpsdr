/* Copyright (C)
* 2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
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
#ifndef SOLAR_H
#define SOLAR_H

typedef struct {
  int sunspots;
  float solarflux;
  int aindex;
  int kindex;
  char updated[64];
  char xray[16];
} SolarData;

SolarData fetch_solar_data();

#endif // SOLAR_H
