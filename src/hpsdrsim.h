/* Copyright (C)
* 2019 - Christoph van Wüllen, DL1YCF
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

//
// hpsdrsim.h, define global data
//
// From the main program, this is included with EXTERN="", while
// other modules include is with "EXTERN=extern".
//
///////////////////////////////////////////////////////////////////////////
//
// The 800-Hz tone and the "man made noise" are for a sample rate of
// 1536 kHz, and must be decimated when using smaller sample rates
//
///////////////////////////////////////////////////////////////////////////

//
// Unfortunately, the code number of the gear
// differes in old and new protocol
//

#define ODEV_NONE          999
#define ODEV_METIS           0
#define ODEV_HERMES          1
#define ODEV_GRIFFIN         2
#define ODEV_ANGELIA         4
#define ODEV_ORION           5
#define ODEV_HERMES_LITE     6
#define ODEV_ORION2         10
#define ODEV_C25           100
#define ODEV_HERMES_LITE2  506

#define NDEV_NONE          999
#define NDEV_ATLAS           0
#define NDEV_HERMES          1
#define NDEV_HERMES2         2
#define NDEV_ANGELIA         3
#define NDEV_ORION           4
#define NDEV_ORION2          5
#define NDEV_SATURN         10
#define NDEV_HERMES_LITE     6
#define NDEV_HERMES_LITE2  506
#define NDEV_C25           100

EXTERN int OLDDEVICE;
EXTERN int NEWDEVICE;

//
// A table of (random) noise the whole spectrum
// This is a very long table such that there is no audible "beating"
// pattern even at very high sample rates.
//
#define LENNOISE 1536000
#define NOISEDIV (RAND_MAX / 768000)

EXTERN double noiseItab[LENNOISE];
EXTERN double noiseQtab[LENNOISE];

//
// A table of (man made) noise fed to the I samples of ADC0
// and to the Q samples of ADC1, such that it can be eliminated
// using DIVERSITY
//
EXTERN int diversity;
EXTERN int noiseblank;
EXTERN int nb_pulse, nb_width;

#define LENDIV 48000
EXTERN double divtab[LENDIV];

//
// TX fifo (needed for PureSignal)
//

// RTXLEN must be an sixteen-fold multiple of 63
// because we have 63 samples per 512-byte METIS packet,
// and two METIS packets per TCP/UDP packet,
// and two/four/eight-fold up-sampling if the TX sample
// rate is 96000/192000/384000
//
// In the new protocol, TX samples come in bunches of
// 240 samples. So NEWRTXLEN is defined as a multiple of
// 240 not exceeding RTXLEN
//
#define OLDRTXLEN 64512 // must be larger than NEWRTXLEN
#define NEWRTXLEN 64320
EXTERN double  isample[OLDRTXLEN];
EXTERN double  qsample[OLDRTXLEN];

//
// Address where to send packets from the old and new protocol
// to the PC
//
EXTERN struct sockaddr_in addr_new;
EXTERN struct sockaddr_in addr_old;

//
// Constants for conversion of TX power
//
EXTERN double c1, c2, maxpwr;

//
// Forward declarations for new protocol stuff
//
void   new_protocol_general_packet(unsigned char *buffer);
int    new_protocol_running(void);

#ifndef __APPLE__
// using clock_nanosleep of librt
extern int clock_nanosleep(clockid_t __clock_id, int __flags,
                           __const struct timespec *__req,
                           struct timespec *__rem);
#endif

//
// Constants defining the distortion of the TX signal
// These give about -24 dBc at full drive, that is
// about the value a reasonable amp gives.
//
#define IM3a  0.60
#define IM3b  0.20

//
// Digital Inputs, reported to the SDR program
//
EXTERN uint8_t radio_digi_changed;
EXTERN uint8_t radio_ptt, radio_dash, radio_dot;
EXTERN uint8_t radio_io1, radio_io2, radio_io3, radio_io4;
EXTERN uint8_t radio_io5, radio_io6, radio_io8;

//
// Flag for modifying the clock speed of the radio
//
EXTERN int speed;
//
// message printing
//
#include <stdarg.h>
EXTERN void t_print(const char *format, ...);
EXTERN void t_perror(const char *string);

//
// define PACKETLIST to get info about every packet received
//
//#define PACKETLIST
