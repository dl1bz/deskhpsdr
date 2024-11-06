/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#ifndef _NEW_PROTOCOL_H
#define _NEW_PROTOCOL_H

#include "MacOS.h"   // for semaphores
#include "receiver.h"

#define MAX_DDC 4

// port definitions from host
#define GENERAL_REGISTERS_FROM_HOST_PORT              1024
#define PROGRAMMING_FROM_HOST_PORT                    1024
#define RECEIVER_SPECIFIC_REGISTERS_FROM_HOST_PORT    1025
#define TRANSMITTER_SPECIFIC_REGISTERS_FROM_HOST_PORT 1026
#define HIGH_PRIORITY_FROM_HOST_PORT                  1027
#define AUDIO_FROM_HOST_PORT                          1028
#define TX_IQ_FROM_HOST_PORT                          1029

// port definitions to host
#define COMMAND_RESPONSE_TO_HOST_PORT                 1024
#define HIGH_PRIORITY_TO_HOST_PORT                    1025
#define MIC_LINE_TO_HOST_PORT                         1026
#define WIDE_BAND_TO_HOST_PORT                        1027
#define RX_IQ_TO_HOST_PORT_0                          1035
#define RX_IQ_TO_HOST_PORT_1                          1036
#define RX_IQ_TO_HOST_PORT_2                          1037
#define RX_IQ_TO_HOST_PORT_3                          1038
#define RX_IQ_TO_HOST_PORT_4                          1039
#define RX_IQ_TO_HOST_PORT_5                          1040
#define RX_IQ_TO_HOST_PORT_6                          1041
#define RX_IQ_TO_HOST_PORT_7                          1042

// Network buffers
// Maximum length is 1444

#define NET_BUFFER_SIZE  1500

/////////////////////////////////////////////////////////////////////////////
//
// PEDESTRIAN BUFFER MANAGEMENT
//
////////////////////////////////////////////////////////////////////////////
//
// One buffer. The fences can be used to detect over-writing
// (feature currently not used).
//
////////////////////////////////////////////////////////////////////////////

struct mybuffer_ {
  struct mybuffer_ *next;
  int             free;
  long            lowfence;
  unsigned char   buffer[NET_BUFFER_SIZE];
  long            highfence;
};

typedef struct mybuffer_ mybuffer;

#define MIC_SAMPLES 64

extern void schedule_high_priority(void);
extern void schedule_general(void);
extern void schedule_receive_specific(void);
extern void schedule_transmit_specific(void);

extern void new_protocol_init(void);

extern void filter_board_changed(void);
extern void pa_changed(void);
extern void tuner_changed(void);

extern void new_protocol_audio_samples(short left_audio_sample, short right_audio_sample);
extern void new_protocol_iq_samples(int isample, int qsample);
extern void new_protocol_flush_iq_samples(void);
extern void new_protocol_cw_audio_samples(short l, short r);

extern void new_protocol_menu_start(void);
extern void new_protocol_menu_stop(void);
extern void saturn_post_iq_data(int ddc, mybuffer *buffer);
extern void saturn_post_micaudio(int bytes, mybuffer *buffer);
extern void saturn_post_high_priority(mybuffer *buffer);

//
// if DUMP_TX_DATA is #defined, the first 1000000 samples
// after a RXTX transition are dumped to a file at the
// next TXRX transition. The value of DUMP_TX_DATA
// allows to dump either the TX IQ data sent to the radio,
// the RX or TX feedback data. PURESIGNAL must be enabled
// to provide this data.
//
#define DUMP_TXIQ   1
#define DUMP_TXFDBK 2
#define DUMP_RXFDBK 3

//#define DUMP_TX_DATA DUMP_RXFDBK

#ifdef DUMP_TX_DATA
  extern int rxiq_count;
  extern long rxiqi[];
  extern long rxiqq[];
#endif

#endif
