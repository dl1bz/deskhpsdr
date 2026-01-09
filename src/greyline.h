/* Copyright (C)
*
* 2024 - 2026 Heiko Amft, DL1BZ (Project deskHPSDR)
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

#ifndef GREYLINE_H
#define GREYLINE_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Preferred API:
 * Pass only the desired window width. The height is computed automatically from the
 * embedded map image aspect ratio (day_jpg.h).
 */
void open_greyline_win(int window_width, const char *locator);

/* Same as open_greyline_win(), but forces a specific parent window. */
void open_greyline_win_for_parent(GtkWindow *parent, int window_width, const char *locator);

/* Backward-compatible APIs (explicit width/height). */
void open_greyline_win_wh(int window_width, int window_height, const char *locator);

void open_greyline_win_for_parent_wh(GtkWindow *parent,
                                     int window_width,
                                     int window_height,
                                     const char *locator);

#ifdef __cplusplus
}
#endif

#endif /* GREYLINE_H */
