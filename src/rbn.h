/*  Copyright (C)
*   2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
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

#ifndef RBN_H
#define RBN_H

#ifdef __cplusplus
extern "C" {
#endif

void rbn_start(void);
void rbn_stop(void);
void rbn_update_from_settings(void);

#ifdef __cplusplus
}
#endif

#endif /* RBN_H */
