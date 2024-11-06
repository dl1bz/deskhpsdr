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
// this application uses C code to emulate HPSDR protocol 2
//
// saturnserver.c:
//
// Contribution of interfacing to PiHPSDR from N1GP (Rick Koch)
//
// Protocol2 is defined by "openHPSDR Ethernet Protocol V3.8"
// unlike protocol 1, it uses multiple ports for the data endpoints
//
// Saturn network interface to PiHPSDR
//
//////////////////////////////////////////////////////////////

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <semaphore.h>

#include "saturnregisters.h"              // register I/O for Saturn
#include "saturnserver.h"
#include "saturndrivers.h"
#include "saturnmain.h"
#include "message.h"

struct sockaddr_in reply_addr;              // destination address for outgoing data

bool ReplyAddressSet = false;               // true when reply address has been set
bool StartBitReceived = false;              // true when "run" bit has been set
bool NewMessageReceived = false;            // set whenever a message is received
bool ExitRequested = false;                 // true if "exit checking" thread requests shutdown
bool SkipExitCheck = false;                 // true to skip "exit checking", if running as a service
bool ThreadError = false;                   // true if a thread reports an error
bool ServerActive = false;
bool saturn_server_en = false;
bool client_enable_tx = false;
bool HW_Timer_Enable = true;

#define VDISCOVERYSIZE 60                   // discovery packet
#define VDISCOVERYREPLYSIZE 60              // reply packet
#define VWIDEBANDSIZE 1028                  // wideband scalar samples
#define VCONSTTXAMPLSCALEFACTOR 0x0001FFFF  // 18 bit scale value - set to 1/2 of full scale

struct ThreadSocketData SocketData[VPORTTABLESIZE] = {
  {0, 0, 1024, "Cmd", false, {}, 0, 0},                     // command (incoming) thread
  {0, 0, 1025, "DDC Specific", false, {}, 0, 0},            // DDC specifc (incoming) thread
  {0, 0, 1026, "DUC Specific", false, {}, 0, 0},            // DUC specific (incoming) thread
  {0, 0, 1027, "High Priority In", false, {}, 0, 0},        // High Priority (incoming) thread
  {0, 0, 1028, "Spkr Audio", false, {}, 0, 0},              // Speaker Audio (incoming) thread
  {0, 0, 1029, "DUC I/Q", false, {}, 0, 0},                 // DUC IQ (incoming) thread
  {0, 0, 1025, "High Priority Out", false, {}, 0, 0},       // High Priority (outgoing) thread
  {0, 0, 1026, "Mic Audio", false, {}, 0, 0},               // Mic Audio (outgoing) thread
  {0, 0, 1035, "DDC I/Q 0", false, {}, 0, 0},               // DDC IQ 0 (outgoing) thread
  {0, 0, 1036, "DDC I/Q 1", false, {}, 0, 0},               // DDC IQ 1 (outgoing) thread
  {0, 0, 1037, "DDC I/Q 2", false, {}, 0, 0},               // DDC IQ 2 (outgoing) thread
  {0, 0, 1038, "DDC I/Q 3", false, {}, 0, 0},               // DDC IQ 3 (outgoing) thread
  {0, 0, 1039, "DDC I/Q 4", false, {}, 0, 0},               // DDC IQ 4 (outgoing) thread
  {0, 0, 1040, "DDC I/Q 5", false, {}, 0, 0},               // DDC IQ 5 (outgoing) thread
  {0, 0, 1041, "DDC I/Q 6", false, {}, 0, 0},               // DDC IQ 6 (outgoing) thread
  {0, 0, 1042, "DDC I/Q 7", false, {}, 0, 0},               // DDC IQ 7 (outgoing) thread
  {0, 0, 1043, "DDC I/Q 8", false, {}, 0, 0},               // DDC IQ 8 (outgoing) thread
  {0, 0, 1044, "DDC I/Q 9", false, {}, 0, 0},               // DDC IQ 9 (outgoing) thread
  {0, 0, 1027, "Wideband 0", false, {}, 0, 0},              // Wideband 0 (outgoing) thread
  {0, 0, 1028, "Wideband 1", false, {}, 0, 0}               // Wideband 1 (outgoing) thread
};

//
// default port numbers, used if incoming port number = 0
//
uint16_t DefaultPorts[VPORTTABLESIZE] = {
  1024, 1025, 1026, 1027, 1028,
  1029, 1025, 1026, 1035, 1036,
  1037, 1038, 1039, 1040, 1041,
  1042, 1043, 1044, 1027, 1028
};

pthread_t saturn_server_thread;
pthread_t DDCSpecificThread;
pthread_t DUCSpecificThread;
pthread_t HighPriorityToSDRThread;
pthread_t SpkrAudioThread;
pthread_t DUCIQThread;
pthread_t DDCIQThread[VNUMDDC];               // array, but not sure how many
pthread_t MicThread;
pthread_t HighPriorityFromSDRThread;
pthread_t CheckForNoActivityThread;           // thread looks for inactvity

//
// set the port for a given thread. If 0, set the default according to HPSDR spec.
// if port is different from the currently assigned one, set the "change port" bit
//
void SetPort(uint32_t ThreadNum, uint16_t PortNum) {
  uint16_t CurrentPort;
  CurrentPort = SocketData[ThreadNum].Portid;

  if (PortNum == 0) {
    SocketData[ThreadNum].Portid = DefaultPorts[ThreadNum];  //default if not set
  } else {
    SocketData[ThreadNum].Portid = PortNum;
  }

  if (SocketData[ThreadNum].Portid != CurrentPort) {
    SocketData[ThreadNum].Cmdid |= VBITCHANGEPORT;
  }
}

//
// function to make an incoming or outgoing socket, bound to the specified port in the structure
// 1st parameter is a link into the socket data table
//
int MakeSocket(struct ThreadSocketData* Ptr, int DDCid) {
  struct timeval ReadTimeout;                                       // read timeout
  int yes = 1;

  //  struct sockaddr_in addr_cmddata;
  //
  // create socket for incoming data
  //
  if ((Ptr->Socketid = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    t_perror("socket fail");
    return EXIT_FAILURE;
  }

  //
  // set 1ms timeout, and re-use any recently open ports
  //
  setsockopt(Ptr->Socketid, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  ReadTimeout.tv_sec = 0;
  ReadTimeout.tv_usec = 1000;
  setsockopt(Ptr->Socketid, SOL_SOCKET, SO_RCVTIMEO, (void *)&ReadTimeout, sizeof(ReadTimeout));
  //
  // bind application to the specified port
  //
  memset(&Ptr->addr_cmddata, 0, sizeof(struct sockaddr_in));
  Ptr->addr_cmddata.sin_family = AF_INET;
  Ptr->addr_cmddata.sin_addr.s_addr = htonl(INADDR_ANY);
  Ptr->addr_cmddata.sin_port = htons(Ptr->Portid);

  if (bind(Ptr->Socketid, (struct sockaddr *)&Ptr->addr_cmddata, sizeof(struct sockaddr_in)) < 0) {
    t_perror("bind");
    return EXIT_FAILURE;
  }

  struct sockaddr_in checkin;

  socklen_t len = sizeof(checkin);

  if (getsockname(Ptr->Socketid, (struct sockaddr *)&checkin, &len) == -1) {
    t_perror("getsockname");
  }

  Ptr->DDCid = DDCid;                       // set DDC number, for outgoing ports
  return 0;
}

//
// this runs as its own thread to see if messages have stopped being received.
// if no messages in a second, goes back to "inactive" state.
//
// cppcheck-suppress constParameterCallback
void* CheckForActivity(void *arg) {
  while (1) {
    sleep(1000);                                   // wait for 1 second
    bool PreviouslyActiveState = ServerActive;     // see if active on entry

    if (!NewMessageReceived && HW_Timer_Enable) {  // if no messages received,
      ServerActive = false;                        // set back to inactive
      ReplyAddressSet = false;
      StartBitReceived = false;

      if (PreviouslyActiveState) {
        for (int i = 4; i < VNUMDDC; i++) {        // disable upper bank of DDCs
          SetP2SampleRate(i, false, 48, false);
        }

        WriteP2DDCRateRegister();
        t_print("Reverted to Inactive State after no activity\n");
      }
    }

    NewMessageReceived = false;
  }

  t_print("ENDING CheckForActivity thread\n");
}

//
// perform ordely shutdown of the program
//
void shutdown_saturn_server() {
  ServerActive = false;
  close(SocketData[0].Socketid);                          // close incoming data socket
  ExitRequested = true;
  t_print("Shutdown COMPLETE\n");
}

void start_saturn_server() {
  ExitRequested = false;

  if (pthread_create(&saturn_server_thread, NULL, saturn_server, NULL) < 0) {
    t_perror("pthread_create saturn_server thread");
    return;
  }

  pthread_detach(saturn_server_thread);
}

//
// Initialise, then handle incoming command/general data
// has a loop that reads & processes incoming command packets
// see protocol documentation
//
void* saturn_server(void *arg) {
  int i, size;
  uint8_t UDPInBuffer[VDISCOVERYSIZE];
  //
  // part written discovery reply packet
  //
  uint8_t DiscoveryReply[VDISCOVERYREPLYSIZE] = {
    0, 0, 0, 0,                                   // sequence bytes
    2,                                            // 2 if not active; 3 if active
    0, 0, 0, 0, 0, 0,                             // SDR (raspberry i) MAC address
    10,                                           // board type. Saturn
    39,                                           // protocol version 3.9
    20,                                           // this SDR firmware version. >17 to enable QSK
    0, 0, 0, 0, 0, 0,                             // Mercury, Metis, Penny version numbers
    6,                                            // 6 DDC's for network client
    1,                                            // phase word
    0,                                            // endian mode
    0, 0,                                         // beta version, reserved byte (total 25 useful bytes)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                 // 10 bytes padding
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                 // 10 bytes padding
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0      // 15 bytes padding
  };
  uint8_t CmdByte;                                                  // command word from PC app
  struct sockaddr_in addr_from;                                     // holds MAC address of source of incoming messages
  struct iovec iovecinst;                                           // iovcnt buffer - 1 for each outgoing buffer
  struct msghdr datagram;                                           // multiple incoming message header

  //
  // start up thread to check for no longer getting messages, to set back to inactive
  //
  if (pthread_create(&CheckForNoActivityThread, NULL, CheckForActivity, NULL) < 0) {
    t_perror("pthread_create check for exit");
    return NULL;
  }

  pthread_detach(CheckForNoActivityThread);
  //
  // create socket for incoming data on the command port
  //
  MakeSocket(SocketData, 0);
#if defined(__linux__)
  //
  // get MAC address of ethernet adapter "eth0"
  //
  struct ifreq hwaddr;                                              // holds this device MAC address
  memset(&hwaddr, 0, sizeof(hwaddr));
  strncpy(hwaddr.ifr_name, "eth0", IFNAMSIZ - 1);
  ioctl(SocketData[VPORTCOMMAND].Socketid, SIOCGIFHWADDR, &hwaddr);

  for (i = 0; i < 6; ++i) { DiscoveryReply[i + 5] = hwaddr.ifr_addr.sa_data[i]; }        // copy MAC to reply message

#else

  // BSD, or MacOS have a different mechanism of getting the hardware addr.
  // Since this is intended to work on RaspPi only, just use fake addr.
  for (i = 0; i < 6; ++i) { DiscoveryReply[i + 5] = 0xAA; }

#endif
  MakeSocket(SocketData + VPORTDDCSPECIFIC, 0);          // create and bind a socket

  if (pthread_create(&DDCSpecificThread, NULL, IncomingDDCSpecific, (void * )&SocketData[VPORTDDCSPECIFIC]) < 0) {
    t_perror("pthread_create DDC specific");
    return NULL;
  }

  pthread_detach(DDCSpecificThread);
  MakeSocket(SocketData + VPORTDUCSPECIFIC, 0);          // create and bind a socket

  if (pthread_create(&DUCSpecificThread, NULL, IncomingDUCSpecific, (void * )&SocketData[VPORTDUCSPECIFIC]) < 0) {
    t_perror("pthread_create DUC specific");
    return NULL;
  }

  pthread_detach(DUCSpecificThread);
  MakeSocket(SocketData + VPORTHIGHPRIORITYTOSDR, 0);          // create and bind a socket

  if (pthread_create(&HighPriorityToSDRThread, NULL, IncomingHighPriority,
                     (void * )&SocketData[VPORTHIGHPRIORITYTOSDR]) < 0) {
    t_perror("pthread_create High priority to SDR");
    return NULL;
  }

  pthread_detach(HighPriorityToSDRThread);
#if 0
  MakeSocket(SocketData + VPORTSPKRAUDIO, 0);          // create and bind a socket

  if (pthread_create(&SpkrAudioThread, NULL, IncomingSpkrAudio, (void * )&SocketData[VPORTSPKRAUDIO]) < 0) {
    t_perror("pthread_create speaker audio");
    return NULL;
  }

  pthread_detach(SpkrAudioThread);
#endif
  MakeSocket(SocketData + VPORTDUCIQ, 0);          // create and bind a socket

  if (pthread_create(&DUCIQThread, NULL, IncomingDUCIQ, (void * )&SocketData[VPORTDUCIQ]) < 0) {
    t_perror("pthread_create DUC I/Q");
    return NULL;
  }

  pthread_detach(DUCIQThread);
  //
  // create outgoing mic data thread
  // note this shares a port with incoming DUC specific, so don't create a new port
  // instead copy socket settings from DUCSPECIFIC socket:
  //
  SocketData[VPORTMICAUDIO].Socketid = SocketData[VPORTDUCSPECIFIC].Socketid;
  memcpy(&SocketData[VPORTMICAUDIO].addr_cmddata, &SocketData[VPORTDUCSPECIFIC].addr_cmddata, sizeof(struct sockaddr_in));
#if 0

  if (pthread_create(&MicThread, NULL, OutgoingMicSamples, (void * )&SocketData[VPORTMICAUDIO]) < 0) {
    t_perror("pthread_create Mic");
    return NULL;
  }

  pthread_detach(MicThread);
#endif
  //
  // create outgoing high priority data thread
  // note this shares a port with incoming DDC specific, so don't create a new port
  // instead copy socket settings from VPORTDDCSPECIFIC socket:
  //
  SocketData[VPORTHIGHPRIORITYFROMSDR].Socketid = SocketData[VPORTDDCSPECIFIC].Socketid;
  memcpy(&SocketData[VPORTHIGHPRIORITYFROMSDR].addr_cmddata, &SocketData[VPORTDDCSPECIFIC].addr_cmddata,
         sizeof(struct sockaddr_in));
#if 0

  if (pthread_create(&HighPriorityFromSDRThread, NULL, OutgoingHighPriority,
                     (void * )&SocketData[VPORTHIGHPRIORITYFROMSDR]) < 0) {
    t_perror("pthread_create outgoing hi priority");
    return NULL;
  }

  pthread_detach(HighPriorityFromSDRThread);
#endif
  //
  // create all the DDC sockets
  //
  MakeSocket(SocketData + VPORTDDCIQ0, 0);
  MakeSocket(SocketData + VPORTDDCIQ1, 1);
  MakeSocket(SocketData + VPORTDDCIQ2, 2);
  MakeSocket(SocketData + VPORTDDCIQ3, 3);
  MakeSocket(SocketData + VPORTDDCIQ4, 4);
  MakeSocket(SocketData + VPORTDDCIQ5, 5);
  MakeSocket(SocketData + VPORTDDCIQ6, 6);
  MakeSocket(SocketData + VPORTDDCIQ7, 7);
  MakeSocket(SocketData + VPORTDDCIQ8, 8);
  MakeSocket(SocketData + VPORTDDCIQ9, 9);

  //
  // now main processing loop. Process received Command packets arriving at port 1024
  // these are identified by the command byte (byte 4)
  // cmd=00: general packet
  // cmd=02: discovery
  // cmd=03: set IP address (not supported)
  // cmd=04: erase (not supported)
  // cmd=05: program (not supported)
  //
  while (!ExitRequested) {
    memset(&iovecinst, 0, sizeof(struct iovec));
    memset(&datagram, 0, sizeof(datagram));
    iovecinst.iov_base = &UDPInBuffer;                  // set buffer for incoming message number i
    iovecinst.iov_len = VDISCOVERYSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &addr_from;
    datagram.msg_namelen = sizeof(addr_from);
    size = recvmsg(SocketData[0].Socketid, &datagram, 0);         // get one message. If it times out, gets size=-1

    if (size < 0 && errno != EAGAIN) {
      t_perror("recvfrom, port 1024");
      return NULL;
    }

    if (ThreadError) {
      break;
    }

    //
    // only process packets of length 60 bytes on this port, to exclude protocol 1 discovery for example.
    // (that means we can't handle the programming packet but we don't use that anyway)
    //
    CmdByte = UDPInBuffer[4];

    if (size == VDISCOVERYSIZE) {
      NewMessageReceived = true;

      switch (CmdByte) {
      //
      // general packet. Get the port numbers and establish listener threads
      //
      case 0:
        //t_print("P2 General packet to SDR, size= %d\n", size);
        //
        // get "from" MAC address and port; this is where the data goes back to
        //
        memset(&reply_addr, 0, sizeof(reply_addr));
        reply_addr.sin_family = AF_INET;
        reply_addr.sin_addr.s_addr = addr_from.sin_addr.s_addr;
        reply_addr.sin_port =
          addr_from.sin_port;                       // (but each outgoing thread needs to set its own sin_port)
        saturn_handle_general_packet(true, UDPInBuffer);
        ReplyAddressSet = true;

        if (ReplyAddressSet && StartBitReceived) {
          ServerActive = true;  // only set active if we have start bit too
        }

        break;

      //
      // discovery packet
      //
      case 2:
        t_print("P2 Discovery packet\n");

        if (ServerActive) {
          DiscoveryReply[4] = 3;  // response 2 if not active, 3 if running
        } else {
          DiscoveryReply[4] = 2;
        }

        memset(&UDPInBuffer, 0, VDISCOVERYREPLYSIZE);
        memcpy(&UDPInBuffer, DiscoveryReply, VDISCOVERYREPLYSIZE);
        sendto(SocketData[0].Socketid, &UDPInBuffer, VDISCOVERYREPLYSIZE, 0, (struct sockaddr *)&addr_from, sizeof(addr_from));
        break;

      case 3:
      case 4:
      case 5:
        t_print("Unsupported packet\n");
        break;

      default:
        break;
      }// end switch (packet type)
    }

    //
    // now do any "post packet" processing
    //
  } //while(1)

  if (ThreadError) {
    t_print("Thread error reported - exiting\n");
  }

  //
  // clean exit
  //
  t_print("Exiting\n");
  shutdown_saturn_server();
  return NULL;
}

//
// listener thread for incoming high priority packets
//
void *IncomingHighPriority(void *arg) {                 // listener thread
  struct ThreadSocketData *ThreadData;                  // socket etc data for this thread
  struct sockaddr_in addr_from;                         // holds MAC address of source of incoming messages
  uint8_t UDPInBuffer[VHIGHPRIOTIYTOSDRSIZE];           // incoming buffer
  struct iovec iovecinst;                               // iovcnt buffer - 1 for each outgoing buffer
  struct msghdr datagram;                               // multiple incoming message header
  ThreadData = (struct ThreadSocketData *)arg;
  ThreadData->Active = true;
  t_print("spinning up high priority incoming thread with port %d\n", ThreadData->Portid);

  //
  // main processing loop
  //
  while (!ExitRequested) {
    memset(&iovecinst, 0, sizeof(struct iovec));
    memset(&datagram, 0, sizeof(datagram));
    iovecinst.iov_base = &UDPInBuffer;                  // set buffer for incoming message number i
    iovecinst.iov_len = VHIGHPRIOTIYTOSDRSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &addr_from;
    datagram.msg_namelen = sizeof(addr_from);
    int size = recvmsg(ThreadData->Socketid, &datagram, 0);         // get one message. If it times out, ges size=-1

    if (size < 0 && errno != EAGAIN) {
      t_perror("recvfrom, high priority");
      t_print("error number = %d\n", errno);
      return NULL;
    }

    //
    // if correct packet, process it
    //
    if (size == VHIGHPRIOTIYTOSDRSIZE) {
      NewMessageReceived = true;
      saturn_handle_high_priority(true, UDPInBuffer);
    }
  }

  //
  // close down thread
  //
  close(ThreadData->Socketid);                  // close incoming data socket
  ThreadData->Socketid = 0;
  ThreadData->Active = false;                   // indicate it is closed
  return NULL;
}

//
// listener thread for incoming DDC specific packets
//
void *IncomingDDCSpecific(void *arg) {                  // listener thread
  struct ThreadSocketData *ThreadData;                  // socket etc data for this thread
  struct sockaddr_in addr_from;                         // holds MAC address of source of incoming messages
  uint8_t UDPInBuffer[VDDCSPECIFICSIZE];                // incoming buffer
  struct iovec iovecinst;                               // iovcnt buffer - 1 for each outgoing buffer
  struct msghdr datagram;                               // multiple incoming message header
  ThreadData = (struct ThreadSocketData *)arg;
  ThreadData->Active = true;
  t_print("spinning up DDC specific thread with port %d\n", ThreadData->Portid);

  //
  // main processing loop
  //
  while (!ExitRequested) {
    memset(&iovecinst, 0, sizeof(struct iovec));
    memset(&datagram, 0, sizeof(datagram));
    iovecinst.iov_base = &UDPInBuffer;                  // set buffer for incoming message number i
    iovecinst.iov_len = VDDCSPECIFICSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &addr_from;
    datagram.msg_namelen = sizeof(addr_from);
    int size = recvmsg(ThreadData->Socketid, &datagram, 0);         // get one message. If it times out, ges size=-1

    if (size < 0 && errno != EAGAIN) {
      t_perror("recvfrom, DDC Specific");
      return NULL;
    }

    if (size == VDDCSPECIFICSIZE) {
      NewMessageReceived = true;
      saturn_handle_ddc_specific(true, UDPInBuffer);
    }
  }

  //
  // close down thread
  //
  close(ThreadData->Socketid);                  // close incoming data socket
  ThreadData->Socketid = 0;
  ThreadData->Active = false;                   // indicate it is closed
  return NULL;
}

//
// listener thread for incoming DUC specific packets
//
void *IncomingDUCSpecific(void *arg) {                  // listener thread
  struct ThreadSocketData *ThreadData;                  // socket etc data for this thread
  struct sockaddr_in addr_from;                         // holds MAC address of source of incoming messages
  uint8_t UDPInBuffer[VDUCSPECIFICSIZE];                // incoming buffer
  struct iovec iovecinst;                               // iovcnt buffer - 1 for each outgoing buffer
  struct msghdr datagram;                               // multiple incoming message header
  ThreadData = (struct ThreadSocketData *)arg;
  ThreadData->Active = true;
  t_print("spinning up DUC specific thread with port %d\n", ThreadData->Portid);

  //
  // main processing loop
  //
  while (!ExitRequested) {
    memset(&iovecinst, 0, sizeof(struct iovec));
    memset(&datagram, 0, sizeof(datagram));
    iovecinst.iov_base = &UDPInBuffer;                  // set buffer for incoming message number i
    iovecinst.iov_len = VDUCSPECIFICSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &addr_from;
    datagram.msg_namelen = sizeof(addr_from);
    int size = recvmsg(ThreadData->Socketid, &datagram, 0);         // get one message. If it times out, ges size=-1

    if (size < 0 && errno != EAGAIN) {
      t_perror("recvfrom, DUC specific");
      return NULL;
    }

    if (size == VDUCSPECIFICSIZE) {
      NewMessageReceived = true;
      saturn_handle_duc_specific(true, UDPInBuffer);
    }
  }

  //
  // close down thread
  //
  close(ThreadData->Socketid);                  // close incoming data socket
  ThreadData->Socketid = 0;
  ThreadData->Active = false;                   // indicate it is closed
  return NULL;
}

#define VSPKSAMPLESPERFRAME 64                      // samples per UDP frame
#define VMEMWORDSPERFRAME 32                        // 8 byte writes per UDP msg
#define VSPKSAMPLESPERMEMWORD 2                     // 2 samples (each 4 bytres) per 8 byte word
#define VDMABUFFERSIZE 32768            // memory buffer to reserve
#define VALIGNMENT 4096                             // buffer alignment
#define VBASE 0x1000                // DMA start at 4K into buffer
#define VDMATRANSFERSIZE 256                        // write 1 message at a time

//
// listener thread for incoming DDC (speaker) audio packets
// planned strategy: just DMA spkr data when available; don't copy and DMA a larger amount.
// if sufficient FIFO data available: DMA that data and transfer it out.
// if it turns out to be too inefficient, we'll have to try larger DMA.
//
void *IncomingSpkrAudio(void *arg) {                    // listener thread
  struct ThreadSocketData *ThreadData;                  // socket etc data for this thread
  struct sockaddr_in addr_from;                         // holds MAC address of source of incoming messages
  uint8_t UDPInBuffer[VSPEAKERAUDIOSIZE];               // incoming buffer
  struct iovec iovecinst;                               // iovcnt buffer - 1 for each outgoing buffer
  struct msghdr datagram;                               // multiple incoming message header
  //
  // variables for DMA buffer
  //
  uint8_t* SpkWriteBuffer = NULL;             // data for DMA to write to spkr
  uint32_t SpkBufferSize = VDMABUFFERSIZE;
  unsigned char* SpkBasePtr;                // ptr to DMA location in spk memory
  int DMAWritefile_fd = -1;               // DMA read file device
  bool FIFOOverflow, FIFOUnderflow, FIFOOverThreshold;
  unsigned int Current;
  //uint32_t RegVal = 0;                    // debug
  ThreadData = (struct ThreadSocketData *)arg;
  ThreadData->Active = true;
  t_print("spinning up speaker audio thread with port %d\n", ThreadData->Portid);
  //
  // setup DMA buffer
  //
  posix_memalign((void**)&SpkWriteBuffer, VALIGNMENT, SpkBufferSize);

  if (SpkWriteBuffer == NULL) {
    t_print("spkr write buffer allocation failed\n");
    return NULL;
  }

  SpkBasePtr = SpkWriteBuffer + VBASE;
  memset(SpkWriteBuffer, 0, SpkBufferSize);
  //
  // open DMA device driver
  //
  DMAWritefile_fd = open(VSPKDMADEVICE, O_RDWR);

  if (DMAWritefile_fd < 0) {
    t_print("XDMA write device open failed for spk data\n");
    return NULL;
  }

  SetupFIFOMonitorChannel(eSpkCodecDMA, false);
  ResetDMAStreamFIFO(eSpkCodecDMA);

  //
  // main processing loop
  //
  while (!ExitRequested) {
    memset(&iovecinst, 0, sizeof(struct iovec));            // clear buffers
    memset(&datagram, 0, sizeof(datagram));
    iovecinst.iov_base = &UDPInBuffer;                      // set buffer for incoming message number i
    iovecinst.iov_len = VSPEAKERAUDIOSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &addr_from;
    datagram.msg_namelen = sizeof(addr_from);
    int size = recvmsg(ThreadData->Socketid, &datagram, 0);     // get one message. If it times out, sets size=-1

    if (size < 0 && errno != EAGAIN) {
      t_perror("recvfrom fail, Speaker data");
      return NULL;
    }

    if (size == VSPEAKERAUDIOSIZE) {                        // we have received a packet!
      NewMessageReceived = true;
      //RegVal += 1;            //debug
      int Depth = ReadFIFOMonitorChannel(eSpkCodecDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                         &Current); // read the FIFO free locations

      //t_print("speaker packet received; depth = %d\n", Depth);
      while (Depth < VMEMWORDSPERFRAME) {     // loop till space available
        usleep(1000);                                   // 1ms wait
        Depth = ReadFIFOMonitorChannel(eSpkCodecDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                       &Current); // read the FIFO free locations
        //if(FIFOOverThreshold)
        //  t_print("Codec speaker FIFO Overthreshold, depth now = %d\n", Current);
        //if(FIFOUnderflow)
        //  t_print("Codec Speaker FIFO Underflowed, depth now = %d\n", Current);
      }

      // copy data from UDP Buffer & DMA write it
      memcpy(SpkBasePtr, UDPInBuffer + 4, VDMATRANSFERSIZE);              // copy out spk samples
      //            if(RegVal == 100)
      //                DumpMemoryBuffer(SpkBasePtr, VDMATRANSFERSIZE);
      DMAWriteToFPGA(DMAWritefile_fd, SpkBasePtr, VDMATRANSFERSIZE, VADDRSPKRSTREAMWRITE);
    }
  }

  //
  // close down thread
  //
  close(ThreadData->Socketid);                  // close incoming data socket
  ThreadData->Socketid = 0;
  ThreadData->Active = false;                   // indicate it is closed
  return NULL;
}

#define VIQSAMPLESPERFRAME 240                      // samples per UDP frame
#define VMEMDUCWORDSPERFRAME 180                       // memory writes per UDP frame
#define VBYTESPERSAMPLE 6             // 24 bit + 24 bit samples
#define VDMADUCTRANSFERSIZE 1440                       // write 1 message at a time

//
// listener thread for incoming DUC I/Q packets
// planned strategy: just DMA spkr data when available; don't copy and DMA a larger amount.
// if sufficient FIFO data available: DMA that data and transfer it out.
// if it turns out to be too inefficient, we'll have to try larger DMA.
//
void *IncomingDUCIQ(void *arg) {                        // listener thread
  struct ThreadSocketData *ThreadData;                  // socket etc data for this thread
  struct sockaddr_in addr_from;                         // holds MAC address of source of incoming messages
  uint8_t UDPInBuffer[VDUCIQSIZE];                      // incoming buffer
  struct iovec iovecinst;                               // iovcnt buffer - 1 for each outgoing buffer
  struct msghdr datagram;                               // multiple incoming message header
  ThreadData = (struct ThreadSocketData *)arg;
  ThreadData->Active = true;
  t_print("spinning up incoming DUC I/Q thread with port %d\n", ThreadData->Portid);

  //
  //
  // main processing loop
  //
  while (!ExitRequested) {
    memset(&iovecinst, 0, sizeof(struct iovec));
    memset(&datagram, 0, sizeof(datagram));
    iovecinst.iov_base = &UDPInBuffer;                  // set buffer for incoming message number i
    iovecinst.iov_len = VDUCIQSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &addr_from;
    datagram.msg_namelen = sizeof(addr_from);
    int size = recvmsg(ThreadData->Socketid, &datagram, 0);         // get one message. If it times out, ges size=-1

    if (size < 0 && errno != EAGAIN) {
      t_perror("recvfrom fail, TX I/Q data");
      return NULL;
    }

    if (size == VDUCIQSIZE) {
      NewMessageReceived = true;
      saturn_handle_duc_iq(true, UDPInBuffer);
    }
  }

  //
  // close down thread
  //
  close(ThreadData->Socketid);                  // close incoming data socket
  ThreadData->Socketid = 0;
  ThreadData->Active = false;                   // indicate it is closed
  return NULL;
}

//
// HandlerCheckDDCSettings()
// called when DDC settings have been changed. Check which DDCs are enabled, and sample rate.
// arguably don't need this, as it finds out from the embedded data in the DDC stream
//
void HandlerCheckDDCSettings(void) {
}

//
// HandlerSetEERMode (bool EEREnabled)
// enables amplitude restoration mode. Generates envelope output alongside I/Q samples.
// NOTE hardware does not properly support this yet!
// TX FIFO must be empty. Stop multiplexer; set bit; restart
//
void HandlerSetEERMode(bool EEREnabled) {
}
