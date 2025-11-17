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

#ifndef DXCLUSTER_H
#define DXCLUSTER_H

#ifdef __cplusplus
extern "C" {
#endif

void dxcluster_open_window(const char *host,
                           long int portaddress,
                           const char *callsign,
                           int width,
                           int height,
                           int pos_x,
                           int pos_y);

#ifdef __cplusplus
}
#endif

#endif /* DXCLUSTER_H */
