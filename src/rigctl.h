/* Copyright (C)
*  2016 Steve Wilson <wevets@gmail.com>
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

#ifndef RIGCTL_H
#define RIGCTL_H

struct _SERIALPORT {
  //
  // parity and bits are not included, since we
  // always use 8 bits and "no parity"
  //
  char port[64];    // e.g. "/dev/ttyACM0"
  int  baud;        // baud rate
  int  enable;      // is it enabled?
  int  andromeda;   // flag for handling ANDROMEDA console
  int  g2;          // This port is used for G2-internal communication
  int  autoreporting;
  int  swapRtsDtr; // invert RTS and DTR control signals if needed
};

typedef struct _SERIALPORT SERIALPORT;

#define MAX_SERIAL 3
#if defined (__LDESK__)
  extern SERIALPORT SerialPorts[MAX_SERIAL + 2];
#else
  extern SERIALPORT SerialPorts[MAX_SERIAL];
#endif
extern gboolean rigctl_debug;

extern void launch_tcp_rigctl (void);
extern int launch_serial_rigctl (int id);
extern void disable_serial_rigctl (int id);
extern int rigctl_tcp_running();
extern void  shutdown_tcp_rigctl(void);
#if defined (__LDESK__)
  extern void launch_serptt(void);
  extern void launch_sertune(void);
  extern int serptt_fd;
  extern int sertune_fd;
#endif
#if defined (__AUTOG__)
  extern void launch_autogain_hl2(void);
  extern void restart_autogain_hl2(void);
  extern volatile int autogain_thread_running;
  extern pthread_t autogain_thread;
  extern pthread_mutex_t autogain_mutex;
#endif
extern void launch_rx200_monitor(void);
extern void launch_lpf_monitor(void);
extern void launch_rigctld_monitor(void);
extern int cat_control;
extern unsigned int rigctl_tcp_port;
extern volatile int rigctl_tcp_enable;
extern int rigctl_tcp_andromeda;
extern int rigctl_tcp_autoreporting;
extern int autogain_is_adjusted;
extern volatile int rigctld_enabled;
extern volatile int use_rigctld;
extern void stop_rigctld(void);

#endif // RIGCTL_H
