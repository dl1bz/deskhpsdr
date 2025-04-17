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
#include <stdio.h>
#include "solar.h"

int main() {
  SolarData sd = fetch_solar_data();

  if (sd.sunspots == -1) {
    fprintf(stderr, "Fehler beim Abrufen der Solar-Daten\n");
    return 1;
  }

  printf("  Solar Report:\n");
  printf("  Sunspots:   %d\n", sd.sunspots);
  printf("  Solar Flux: %.1f\n", sd.solarflux);
  printf("  A-Index:    %d\n", sd.aindex);
  printf("  K-Index:    %d\n", sd.kindex);
  printf("  Updated:    %s\n", sd.updated);
  return 0;
}
