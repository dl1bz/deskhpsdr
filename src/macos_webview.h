/*  Copyright (C)
*   2024 - 2026 Heiko Amft, DL1BZ (Project deskHPSDR)
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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __APPLE__

// Default-Fenster (kompatibel zu bisheriger Nutzung)
void macos_open_webview_window(const char* url,
const char *title,
int x,
int y,
int width,
int height);

// Mehrere Fenster, identifiziert über eine ID
void macos_open_webview_window_with_id(const char* id,
const char *url,
const char *title,
int x,
int y,
int width,
int height);

#else

static inline void macos_open_webview_window(const char* url,
const char *title,
int x,
int y,
int width,
int height) {
  (void) url;
  (void) title;
  (void) x;
  (void) y;
  (void) width;
  (void) height;
}

static inline void macos_open_webview_window_with_id(const char* id,
       const char *url,
       const char *title,
       int x,
       int y,
       int width,
       int height) {
  (void) id;
  (void) url;
  (void) title;
  (void) x;
  (void) y;
  (void) width;
  (void) height;
}

#endif

#ifdef __cplusplus
}
#endif
