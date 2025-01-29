/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
* 2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
*   This source code has been forked and was adapted from piHPSDR by DL1YCF to deskHPSDR in October 2024
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

extern GtkWidget *sub_menu;
extern GtkWidget *main_menu;

extern void new_menu(void);

extern void start_meter(void);
extern void start_step(void);
extern void start_band(void);
extern void start_bandstack(void);
extern void start_mode(void);
extern void start_filter(void);
extern void start_noise(void);
extern void start_encoder(void);
extern void start_vfo(int vfo);
extern void start_agc(void);
extern void start_store(void);
extern void start_rx(void);
extern void start_tx(void);
extern void start_diversity(void);
extern void start_ps(void);
#ifdef CLIENT_SERVER
  extern void start_server(void);
#endif

extern void encoder_step(int encoder, int step);

extern int menu_active_receiver_changed(void *data);

enum _active_menu {
  NO_MENU = 0,
  BAND_MENU,
  BANDSTACK_MENU,
  MODE_MENU,
  FILTER_MENU,
  NOISE_MENU,
  AGC_MENU,
  VFO_MENU,
  STORE_MENU
};

extern int active_menu;
#if defined (__LDESK__)
  static const char * const PGNAME = "deskHPSDR";
#else
  static const char * const PGNAME = "piHPSDR";
#endif