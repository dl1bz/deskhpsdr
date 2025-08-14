/* Copyright (C)
*
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

#ifndef _TOOLSET_H
#define _TOOLSET_H

extern int sunspots;
extern int a_index;
extern int k_index;
extern int solar_flux;
extern char geomagfield[32];
extern char xray[16];

extern int is_pi(void);
extern int https_ok(const char* hostname, int mit_cert_check);
extern void check_and_run(int is_dbg);
extern const char* truncate_text(const char* text, size_t max_length);
extern char* truncate_text_malloc(const char* text, size_t max_length);
extern char* truncate_text_3p(const char* text, size_t max_length);
extern gboolean check_and_run_idle_cb(gpointer data);
extern void to_uppercase(char *str);
extern int file_present(const char *filename);
extern const char* extract_short_msg(const char *msg);
extern void show_NOTUNE_dialog(GtkWindow *parent);

#endif // _TOOLSET_H