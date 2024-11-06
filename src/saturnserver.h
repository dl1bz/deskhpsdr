/* Copyright (C)
* 2021 - Laurence Barker G8NJJ
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

/////////////////////////////////////////////////////////////
//
// Saturn project: Artix7 FPGA + Raspberry Pi4 Compute Module
// PCI Express interface from linux on Raspberry pi
// this application uses C code to emulate HPSDR protocol 1
//
// Contribution of interfacing to PiHPSDR from N1GP (Rick Koch)
//
// saturnserver.h:
// Saturn interface to PiHPSDR
//
//////////////////////////////////////////////////////////////

#ifndef __saturnserver_h
#define __saturnserver_h

#include <stdint.h>
#include <netinet/in.h>

// START threaddata.h
//
// list of port numbers, provided in the general packet
// (port 1024 for discovery and general packets not needed in this list)
#define VPORTTABLESIZE 20
// incoming port numbers
#define VPORTCOMMAND 0
#define VPORTDDCSPECIFIC 1
#define VPORTDUCSPECIFIC 2
#define VPORTHIGHPRIORITYTOSDR 3
#define VPORTSPKRAUDIO 4
#define VPORTDUCIQ 5
// outgoing port numbers:
#define VPORTHIGHPRIORITYFROMSDR 6
#define VPORTMICAUDIO 7
#define VPORTDDCIQ0 8
#define VPORTDDCIQ1 9
#define VPORTDDCIQ2 10
#define VPORTDDCIQ3 11
#define VPORTDDCIQ4 12
#define VPORTDDCIQ5 13
#define VPORTDDCIQ6 14
#define VPORTDDCIQ7 15
#define VPORTDDCIQ8 16
#define VPORTDDCIQ9 17
#define VPORTWIDEBAND0 18
#define VPORTWIDEBAND1 19

extern bool HW_Timer_Enable;

//
// a type to hold data for each incoming or outgoing data thread
//
struct ThreadSocketData {
  uint32_t DDCid;                               // only relevant to DDC threads
  int Socketid;                                 // socket to access internet
  uint16_t Portid;                              // port to access
  char *Nameid;                                 // name (for error msg etc)
  bool Active;                                  // true if thread is active
  struct sockaddr_in addr_cmddata;
  uint32_t Cmdid;                               // command from app to thread - bits set for each command
  uint32_t DDCSampleRate;                       // DDC sample rate
};

extern struct ThreadSocketData SocketData[];        // data for each thread
extern struct sockaddr_in reply_addr;               // destination address for outgoing data
extern bool IsTXMode;                               // true if in TX
extern bool SDRActive;                              // true if this SDR is running at the moment
extern bool ReplyAddressSet;                        // true when reply address has been set
extern bool StartBitReceived;                       // true when "run" bit has been set
extern bool NewMessageReceived;                     // set whenever a message is received
extern bool ThreadError;                            // set true if a thread reports an error

#define VBITCHANGEPORT 1                        // if set, thread must close its socket and open a new one on different port
#define VBITINTERLEAVE 2                        // if set, DDC threads should interleave data
#define VBITDDCENABLE 4                         // if set, DDC is enabled

//
// default port numbers, used if incoming port number = 0
//
extern uint16_t DefaultPorts[];

void start_saturn_server(void);
void shutdown_saturn_server(void);
void* saturn_server(void *arg);

//
// set the port for a given thread. If 0, set the default according to HPSDR spec.
// if port is different from the currently assigned one, set the "change port" bit
//
void SetPort(uint32_t ThreadNum, uint16_t PortNum);

//
// function to make an incoming or outgoing socket, bound to the specified port in the structure
// 1st parameter is a link into the socket data table
//
int MakeSocket(struct ThreadSocketData* Ptr, int DDCid);

// END threaddata.h

// START generalpacket.h
//
// protocol 2 handler for General Packet to SDR
// parameter is a pointer to the UDP message buffer.
// copy port numbers to port table,
// then create listener threads for incoming packets & senders foroutgoing
//
int HandleGeneralPacket(uint8_t *PacketBuffer);
// END generalpacket.h

// START IncomingDDCSpecific.h
#define VDDCSPECIFICSIZE 1444           // DDC specific packet size in bytes

//
// protocol 2 handler for incoming DDC specific Packet to SDR
//
void *IncomingDDCSpecific(void *arg);           // listener thread
// END IncomingDDCSpecific.h

// START IncomingDUCSpecific.h
#define VDUCSPECIFICSIZE 60             // DUC specific packet

//
// protocol 2 handler for incoming DUC specific Packet to SDR
//
void *IncomingDUCSpecific(void *arg);           // listener thread
// END IncomingDUCSpecific.h

// START InHighPriority.h
#define VHIGHPRIOTIYTOSDRSIZE 1444      // high priority packet to SDR

//
// protocol 2 handler for incoming high priority Packet to SDR
//
void *IncomingHighPriority(void *arg);          // listener thread
// END InHighPriority.h

// START InDUCIQ.h
#define VDUCIQSIZE 1444                 // TX DUC I/Q data packet

//
// protocol 2 handler for incoming DUC I/Q data Packet to SDR
//
void *IncomingDUCIQ(void *arg);                 // listener thread

//
// HandlerSetEERMode (bool EEREnabled)
// enables amplitude restoration mode. Generates envelope output alongside I/Q samples.
// NOTE hardware does not properly support this yet!
// TX FIFO must be empty. Stop multiplexer; set bit; restart
//
void HandlerSetEERMode(bool EEREnabled);
// END InDUCIQ.h

// START InSpkrAudio.h
#define VSPEAKERAUDIOSIZE 260           // speaker audio packet

//
// protocol 2 handler for incoming speaker audio data Packet to SDR
//
void *IncomingSpkrAudio(void *arg);             // listener thread
// END InSpkrAudio.h

extern bool saturn_server_en;
extern bool client_enable_tx;
#endif
