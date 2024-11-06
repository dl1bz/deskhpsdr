/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
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

#ifdef CLIENT_SERVER
  #include "client_server.h"
  extern int ext_remote_command(void *data);
  extern int ext_rx_remote_update_display(void *data);
  extern int ext_set_title(void *data);
  extern int ext_remote_set_zoom(void *data);
  extern int ext_remote_set_pan(void *data);
#endif

//
// The following calls functions can be called usig g_idle_add
//
extern int ext_start_radio(void *data);
extern int ext_vfo_update(void *data);
extern int ext_tune_update(void *data);
extern int ext_mox_update(void *data);
extern int ext_start_tx(void *data);        // is this necessary?
extern int ext_start_rx(void *data);
extern int ext_start_vfo(void *data);
extern int ext_start_band(void *data);
extern int ext_update_noise(void *data);
extern int ext_update_eq(void *data);
extern int ext_set_vox(void *data);
extern int ext_set_duplex(void *data);      // is this necessary?

///////////////////////////////////////////////////////////
//
// Obsolete functions removed. Note that calls  such as
//
// g_idle_add(ext_menu_filter,NULL);
//
// can/should be replaced by
//
// schedule_action(MENU_FILTER, PRESSED, 0);
//
// to avoid duplicate code
//
///////////////////////////////////////////////////////////
