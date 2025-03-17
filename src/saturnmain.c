/* Copyright (C)
* 2021 - Laurence Barker G8NJJ
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

/////////////////////////////////////////////////////////////
//
// Saturn project: Artix7 FPGA + Raspberry Pi4 Compute Module
// PCI Express interface from linux on Raspberry pi
// this application uses C code to emulate HPSDR protocol 2
//
// Contribution of interfacing to PiHPSDR from N1GP (Rick Koch)
//
// saturnmain.c: based off p2app client app software
//
// Saturn interface to PiHPSDR
//
//////////////////////////////////////////////////////////////

#include <gtk/gtk.h>
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
#include <sys/stat.h>

#include "saturnregisters.h"              // register I/O for Saturn
#include "saturndrivers.h"                      // version I/O for Saturn
#include "saturnmain.h"
#include "saturnserver.h"

#include "discovered.h"
#include "new_protocol.h"
#include "message.h"

extern sem_t DDCInSelMutex;                 // protect access to shared DDC input select register
extern sem_t DDCResetFIFOMutex;             // protect access to FIFO reset register
extern sem_t RFGPIOMutex;                   // protect access to RF GPIO register
extern sem_t CodecRegMutex;                 // protect writes to codec

bool IsTXMode;                              // true if in TX
bool SDRActive;                             // true if this SDR is running at the moment
bool Exiting = false;
extern bool saturn_server_en;
extern bool client_enable_tx;
extern bool ServerActive;
extern bool MOXAsserted;

//
// For "minor" versions up to 17, there is no "major" one.
// For "minor" version 18,  the "major" version is 1
// With each firmware update, the "minor" version is increased (it is not reset upon advancinc the major)
// The "major" version is increased if piHPSDR compatibility is broken
//
#define FIRMWARE_MIN_MINOR    8               // Minimum FPGA "minor" software version that this software requires
#define FIRMWARE_MAX_MINOR   18               // Maximum FPGA "minor" software version that this software is tested on
#define FIRMWARE_MIN_MAJOR    1               // Minimum FPGA "major" software version that this software requires
#define FIRMWARE_MAX_MAJOR    1               // Minimum FPGA "major" software version that this software requires

#define SDRBOARDID 1                          // Hermes
#define SDRSWVERSION 1                        // version of this software
#define VDISCOVERYSIZE 60                     // discovery packet
#define VDISCOVERYREPLYSIZE 60                // reply packet
#define VWIDEBANDSIZE 1028                    // wideband scalar samples
#define VCONSTTXAMPLSCALEFACTOR 0x0001FFFF    // 18 bit scale value - set to 1/2 of full scale
#define VCONSTTXAMPLSCALEFACTOR_13 0x0002000  // 18 bit scale value - set to 1/32 of full scale FWV13+
#define VDMATRANSFERSIZE 4096
#define VDMABUFFERSIZE 131072                 // memory buffer to reserve (4x DDC FIFO so OK)
#define VALIGNMENT 4096                       // buffer alignment
#define VBASE 0x1000                          // offset into I/Q buffer for DMA to start
#define VIQSAMPLESPERFRAME 238
#define VIQBYTESPERFRAME 6*VIQSAMPLESPERFRAME // total bytes in one outgoing frame
#define VIQDUCSAMPLESPERFRAME 240

#define VSPKSAMPLESPERFRAME 64                // samples per UDP frame
#define VMEMWORDSPERFRAME 32                  // 8 byte writes per UDP msg
#define VSPKSAMPLESPERMEMWORD 2               // 2 samples (each 4 bytres) per 8 byte word
#define VDMASPKBUFFERSIZE 32768               // memory buffer to reserve
#define VDMASPKTRANSFERSIZE 256               // write 1 message at a time

#define VMICSAMPLESPERFRAME 64
#define VDMAMICBUFFERSIZE 32768           // memory buffer to reserve
#define VDMAMICTRANSFERSIZE 128                        // read 1 message at a time
#define VMICPACKETSIZE 132

// uncomment to display debug printouts for FPGA data over/under flows
//#define DISPLAY_OVER_UNDER_FLOWS 1

static gpointer saturn_rx_thread(gpointer arg);
static GThread *saturn_rx_thread_id;
static gpointer saturn_micaudio_thread(gpointer arg);
static GThread *saturn_micaudio_thread_id;
static gpointer saturn_high_priority_thread(gpointer arg);
static GThread *saturn_high_priority_thread_id;
//
// code to allocate and free dynamic allocated memory
// first the memory buffers:
//
uint8_t* DMAReadBuffer = NULL;                                                          // data for DMA read from DDC
uint32_t DMABufferSize = VDMABUFFERSIZE;
unsigned char*
DMAReadPtr;                                                              // pointer for 1st available location in DMA memory
unsigned char*
DMAHeadPtr;                                                              // ptr to 1st free location in DMA memory
unsigned char*
DMABasePtr;                                                              // ptr to target DMA location in DMA memory

uint8_t* DDCSampleBuffer[VNUMDDC];                          // buffer per DDC
unsigned char*
IQReadPtr[VNUMDDC];                                                      // pointer for reading out an I or Q sample
unsigned char*
IQHeadPtr[VNUMDDC];                                                      // ptr to 1st free location in I/Q memory
unsigned char*
IQBasePtr[VNUMDDC];                                                      // ptr to DMA location in I/Q memory

// Memory buffers to be exchanged with PiHPSDR APIs
#define MAXMYBUF 3
#define DDCMYBUF 0
#define MICMYBUF 1
#define HPMYBUF  2
//
// number of buffers allocated (for statistics)
//
static int num_buf[MAXMYBUF];

//
// head of buffer list
//
static mybuffer *buflist[MAXMYBUF];

//
// Obtain a free buffer. If no one is available allocate
// new ones. Note these buffer "live" as long as the
// program lives. They are never released.
//
static mybuffer *get_my_buffer(int numlist) {
  int i, j, first;
  const char *desc;
  mybuffer *bp = buflist[numlist];

  while (bp) {
    if (bp->free) {
      // found free buffer. Mark as used and return that one.
      bp->free = 0;
      return bp;
    }

    bp = bp->next;
  }

  //
  // No buffer free, or the first time we request a buffer:
  // allocate (a) new one(s). Note we need very few
  // HighPrio buffers, a limited amount of MicSample buffers,
  // and a possibly large amount of DDC IQ buffers.
  //
  first = (bp == NULL);

  switch (numlist) {
  case HPMYBUF:
    j = 1;
    desc = "HP";
    break;

  case MICMYBUF:
    j = 5;
    desc = "MIC";
    break;

  case DDCMYBUF:
    j = 25;
    desc = "DDC";
    break;

  default:
    // NOTREACHED
    j = 5;
    desc = "UNKNOWN";
    break;
  }

  for (i = 0; i < j; i++) {
    bp = malloc(sizeof(mybuffer));
    bp->free = 1;
    bp->next = buflist[numlist];
    buflist[numlist] = bp;
    num_buf[numlist]++;
  }

  t_print("%s: number of buffer[%s] %s to %d\n", __FUNCTION__, desc,
          first ? "set" : "increased", num_buf[numlist]);
  // Mark the first buffer in list as used and return that one.
  buflist[numlist]->free = 0;
  return buflist[numlist];
}

void saturn_free_buffers() {
  mybuffer *mybuf;

  for (int i = 0; i < MAXMYBUF; i++) {
    mybuf = buflist[i];

    while (mybuf) {
      mybuf->free = 1;
      mybuf = mybuf->next;
    }
  }
}

bool CreateDynamicMemory(void) {                            // return true if error
  uint32_t DDC;
  bool Result = false;

  //
  // set up per-DDC data structures
  //
  for (DDC = 0; DDC < VNUMDDC; DDC++) {
    DDCSampleBuffer[DDC] = malloc(DMABufferSize);
    IQReadPtr[DDC] = DDCSampleBuffer[DDC] + VBASE;          // offset 4096 bytes into buffer
    IQHeadPtr[DDC] = DDCSampleBuffer[DDC] + VBASE;
    IQBasePtr[DDC] = DDCSampleBuffer[DDC] + VBASE;
  }

  //
  // then create the buffer for DMA, and initialise its pointers
  //
  posix_memalign((void**)&DMAReadBuffer, VALIGNMENT, DMABufferSize);

  if (!DMAReadBuffer) {
    t_print("I/Q read buffer allocation failed\n");
    Result = true;
    return Result;
  }

  DMAReadPtr = DMAReadBuffer + VBASE;                             // offset 4096 bytes into buffer
  DMAHeadPtr = DMAReadBuffer + VBASE;
  DMABasePtr = DMAReadBuffer + VBASE;
  memset(DMAReadBuffer, 0, DMABufferSize);
  return Result;
}

void saturn_register_init() {
  ESoftwareID ID;
  unsigned int Version = GetFirmwareVersion(&ID);
  //
  // initialise register access semaphores
  //
  sem_init(&DDCInSelMutex, 0, 1);                                   // for DDC input select register
  sem_init(&DDCResetFIFOMutex, 0, 1);                               // for FIFO reset register
  sem_init(&RFGPIOMutex, 0, 1);                                     // for RF GPIO register
  sem_init(&CodecRegMutex, 0, 1);                                   // for codec writes
  //
  // setup Saturn hardware
  //
  CodecInitialise();
  InitialiseDACAttenROMs();
  InitialiseCWKeyerRamp(true, 9000);
  SetCWSidetoneEnabled(true);
  SetTXProtocol(true);                                              // set to protocol 2
  SetTXModulationSource(eIQData);                                   // disable debug options
  //HandlerSetEERMode(false);                                       // no EER
  SetByteSwapping(true);                                            // h/w to generate NOT network byte order
  SetSpkrMute(false);

  if (Version < 13) {
    SetTXAmplitudeScaling(VCONSTTXAMPLSCALEFACTOR);                 // for firmware version up to 1.2
  } else {
    SetTXAmplitudeScaling(VCONSTTXAMPLSCALEFACTOR_13);              // for  firmware version from 1.3 on
  }

  EnableAlexManualFilterSelect(true);
  SetBalancedMicInput(false);
}

// is there already a pihpsdr running and using xdma?
bool is_already_running() {
  FILE *fp;
  char path[1035];
#if defined (__LDESK__)
  fp = popen("lsof /dev/xdma0_user | grep deskhpsdr", "r");
#else
  fp = popen("lsof /dev/xdma0_user | grep pihpsdr", "r");
#endif

  if (fp == NULL) {
    t_print("Failed to run command in %s\n", __FUNCTION__ );
    exit(1);
  }

  while (fgets(path, sizeof(path), fp) != NULL) {}

  pclose(fp);
#if defined (__LDESK__)
  return (strstr(path, "deskhpsdr") == NULL) ? false : true;
#else
  return (strstr(path, "pihpsdr") == NULL) ? false : true;
#endif
}

#define SATURNPRODUCTID 1                               // Saturn, any version
#define SATURNGOLDENCONFIGID 3                          // "golden" configuration id
#define SATURNPRIMARYCONFIGID 4                         // "primary" configuration id
#define VADDRPRODVERSIONREG 0XC004

void saturn_discovery() {
  struct stat sb;

  if (devices < MAX_DEVICES && stat("/dev/xdma0_user", &sb) == 0 && S_ISCHR(sb.st_mode)) {
    uint8_t *mac = discovered[devices].info.network.mac_address;
    uint32_t SoftwareInformation;                   // swid & version
    uint32_t ProductInformation;                    // product id & version
    uint32_t MinorVersion;                          // s/w version
    uint32_t MajorVersion;                          // s/w version
    uint32_t SWID;                                  // s/w id
    uint32_t ProdID;                                // product version and id
    uint32_t ClockInfo;                             // clock status
    bool goodConfig = true;
    bool incompatible = true;
    char buf[256];
    bool running = is_already_running();

    if (OpenXDMADriver() == 0) {
      return;
    }

    saturn_register_init();
    //
    // read the raw data from registers
    //
    SoftwareInformation = RegisterRead(VADDRSWVERSIONREG);
    ProductInformation = RegisterRead(VADDRPRODVERSIONREG);
    ClockInfo    = (SoftwareInformation      ) & 0xF;               // 4 clock bits
    MinorVersion = (SoftwareInformation >>  4) & 0xFFFF;            // 16 bit minor sw version
    MajorVersion = (SoftwareInformation >> 25) & 0x7F;              //  7 bit major sw version
    SWID         = (SoftwareInformation >> 20) & 0x1F;              //  5 bit software ID
    ProdID       = (ProductInformation  >> 16) & 0xFFFF;            // 16 bit product ID

    //
    // Initially, MajorVersions did not exist
    //
    if (MinorVersion < 18) {
      MajorVersion = 0;
    }

    if (ProdID != SATURNPRODUCTID) {
      t_print("SATURN ProdID does not match\n");
      goodConfig = false;
    }

    if (SWID != SATURNGOLDENCONFIGID && SWID != SATURNPRIMARYCONFIGID) {
      t_print("SATURN SWID does not match\n");
      goodConfig = false;
    }

    if (ClockInfo != 0xF) {
      t_print("SATURN clocks missing\n");
      goodConfig = false;  // not all clocks are present
    }

    if (goodConfig) {
      discovered[devices].status = (running) ? STATE_SENDING : STATE_AVAILABLE;
    }

    if (MajorVersion == 0 && MinorVersion >= FIRMWARE_MIN_MINOR && MinorVersion <= FIRMWARE_MAX_MINOR) {
      incompatible = false;
    }

    if (MajorVersion >= FIRMWARE_MIN_MAJOR && MajorVersion <=  FIRMWARE_MAX_MAJOR) {
      incompatible = false;
    }

    if (incompatible) {
      t_print("Incompatible Saturn FPGA firmware version (%d,%d), need (%d...%d, %d...%d) \n",
              MajorVersion,
              MinorVersion,
              FIRMWARE_MIN_MAJOR,
              FIRMWARE_MAX_MAJOR,
              FIRMWARE_MIN_MINOR,
              FIRMWARE_MAX_MINOR
             );
      discovered[devices].status = STATE_INCOMPATIBLE;
    }

    discovered[devices].protocol = NEW_PROTOCOL;
    discovered[devices].device = NEW_DEVICE_SATURN;
    discovered[devices].software_version = (RegisterRead(VADDRSWVERSIONREG) >> 4) & 0xFFFF;
    discovered[devices].fpga_version = RegisterRead(VADDRUSERVERSIONREG);
    g_strlcpy(discovered[devices].name, "saturn",  sizeof(discovered[devices].name));
    discovered[devices].frequency_min = 0.0;
    discovered[devices].frequency_max = 61440000.0;
    //
    // Try to obtain the hardware MAC address of the local eth0.
    // This is for diagnostic purposes only, so if it fails,
    // just use 00:00:00:00::00.
    //
    // One possible reason for failure is that the ethernet adapter is
    // not present or has a name different from eth0.
    // The interface name reported upstream is "XDMA" anyway.
    //
    memset(buf, 0, 256);
    FILE *fp = fopen("/sys/class/net/eth0/address", "rt");

    if (fp) {
      if (fgets(buf, sizeof buf, fp) != NULL) {
        sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0],
               &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
      }

      fclose(fp);
    } else {
      for (int i = 0; i < 6; i++) {
        discovered[devices].info.network.mac_address[i] = 0;
      }
    }

    discovered[devices].info.network.address_length = 0;
    discovered[devices].info.network.interface_length = 0;
    g_strlcpy(discovered[devices].info.network.interface_name, "XDMA",
              sizeof(discovered[devices].info.network.interface_name));
    discovered[devices].use_tcp = 0;
    discovered[devices].use_routing = 0;
    discovered[devices].supported_receivers = 2;
    t_print("discovery: found saturn device min=%0.3f MHz max=%0.3f MHz\n",
            discovered[devices].frequency_min * 1E-6,
            discovered[devices].frequency_max * 1E-6);
    devices++;
  }
}

#define VDUCIQSAMPLESPERFRAME 240                      // samples per UDP frame
#define VMEMDUCWORDSPERFRAME 180                       // memory writes per UDP frame
#define VBYTESPERSAMPLE 6                                                       // 24 bit + 24 bit samples
#define VDMADUCBUFFERSIZE 32768                                            // memory buffer to reserve
#define VDMADUCTRANSFERSIZE 1440                       // write 1 message at a time

static int DMADUCWritefile_fd = -1;               // DMA read file device
static unsigned char* DUCIQBasePtr;               // ptr to DMA location in I/Q memory

void saturn_init_duc_iq() {
  // variables for DMA buffer
  //
  uint8_t* IQWriteBuffer = NULL;              // data for DMA to write to DUC
  uint32_t IQBufferSize = VDMADUCBUFFERSIZE;
  t_print("%s: Initializing DUC I/Q data\n", __FUNCTION__);
  //
  // setup DMA buffer
  //
  posix_memalign((void**)&IQWriteBuffer, VALIGNMENT, IQBufferSize);

  if (!IQWriteBuffer) {
    t_print("%s: I/Q TX write buffer allocation failed\n", __FUNCTION__);
    exit(-1);
  }

  DUCIQBasePtr = IQWriteBuffer + VBASE;
  memset(IQWriteBuffer, 0, IQBufferSize);
  //
  // open DMA device driver
  //
  DMADUCWritefile_fd = open(VDUCDMADEVICE, O_RDWR);

  if (DMADUCWritefile_fd < 0) {
    t_print("%s: XDMA write device open failed for TX I/Q data\n", __FUNCTION__);
    exit(-1);
  }

  //
  // setup hardware
  //
  EnableDUCMux(false);                                  // disable temporarily
  SetTXIQDeinterleaved(false);                          // not interleaved (at least for now!)
  ResetDUCMux();                                        // reset 64 to 48 mux
  SetupFIFOMonitorChannel(eTXDUCDMA, false);
  ResetDMAStreamFIFO(eTXDUCDMA);
  EnableDUCMux(true);                                   // enable operation
}

static int TXActive = 0;   // The client actively transmitting, 0-none, 1-xdma, 2-network

void saturn_handle_duc_iq(bool FromNetwork, uint8_t *UDPInBuffer) {
  uint32_t Cntr;                                          // sample counter
  uint8_t* SrcPtr;                                        // pointer to data from Thetis
  uint8_t* DestPtr;                                       // pointer to DMA buffer data
  uint32_t DepthDUC = 0;
  unsigned int Current;                                   // current occupied locations in FIFO
  bool FIFODUCOverflow, FIFODUCUnderflow, FIFODUCOverThreshold;

  //t_print("DUC I/Q %sbuffer received, TXActive=%d\n", (FromNetwork)?"network ":"", TXActive);
  if (FromNetwork) { //RRK
    if (TXActive == 1) { return; }
  } else {
    if (TXActive == 2) { return; }
  }

  DepthDUC = ReadFIFOMonitorChannel(eTXDUCDMA, &FIFODUCOverflow, &FIFODUCOverThreshold, &FIFODUCUnderflow,
                                    &Current);  // read the FIFO free locations
#ifdef DISPLAY_OVER_UNDER_FLOWS

  if (FIFODUCOverThreshold) {
    t_print("TX DUC FIFO Overthreshold, depth now = %d\n", Current);
  }

  if (FIFODUCUnderflow) {
    t_print("TX DUC FIFO Underflowed, depth now = %d\n", Current);
  }

#endif

  while (DepthDUC < VMEMDUCWORDSPERFRAME) {     // loop till space available
    usleep(500);                                    // 0.5ms wait
    DepthDUC = ReadFIFOMonitorChannel(eTXDUCDMA, &FIFODUCOverflow, &FIFODUCOverThreshold, &FIFODUCUnderflow,
                                      &Current); // read the FIFO free locations
#ifdef DISPLAY_OVER_UNDER_FLOWS

    if (FIFODUCOverThreshold) {
      t_print("TX DUC FIFO Overthreshold, depth now = %d\n", Current);
    }

    if (FIFODUCUnderflow) {
      t_print("TX DUC FIFO Underflowed, depth now = %d\n", Current);
    }

#endif
  }

  // copy data from UDP Buffer & DMA write it
  //memcpy(DUCIQBasePtr, UDPInBuffer + 4, VDMADUCTRANSFERSIZE);                // copy out I/Q samples
  SrcPtr = (UDPInBuffer + 4);
  DestPtr = DUCIQBasePtr;

  for (Cntr = 0; Cntr < VIQDUCSAMPLESPERFRAME; Cntr++) {                 // samplecounter
    *DestPtr++ = *(SrcPtr + 3);                         // get I sample (3 bytes)
    *DestPtr++ = *(SrcPtr + 4);
    *DestPtr++ = *(SrcPtr + 5);
    *DestPtr++ = *(SrcPtr + 0);                         // get Q sample (3 bytes)
    *DestPtr++ = *(SrcPtr + 1);
    *DestPtr++ = *(SrcPtr + 2);
    SrcPtr += 6;                                        // point at next source sample
  }

  DMAWriteToFPGA(DMADUCWritefile_fd, DUCIQBasePtr, VDMADUCTRANSFERSIZE, VADDRDUCSTREAMWRITE);
  return;
}

static int DMASpkWritefile_fd = -1;
static unsigned char* SpkBasePtr;
static unsigned char* SpkReadPtr;               // pointer for reading out a spkr sample
static unsigned char* SpkHeadPtr;               // ptr to 1st free location in spk memory

void saturn_init_speaker_audio() {
  //
  // variables for DMA buffer
  //
  uint8_t* SpkWriteBuffer = NULL;             // data for DMA to write to spkr
  uint32_t SpkBufferSize = VDMASPKBUFFERSIZE;
  t_print("%s\n", __FUNCTION__);
  //
  // setup DMA buffer
  //
  posix_memalign((void**)&SpkWriteBuffer, VALIGNMENT, SpkBufferSize);

  if (!SpkWriteBuffer) {
    t_print("%s: spkr write buffer allocation failed\n", __FUNCTION__);
    exit(-1);
  }

  SpkReadPtr = SpkWriteBuffer + VBASE;              // offset 4096 bytes into buffer
  SpkHeadPtr = SpkWriteBuffer + VBASE;
  SpkBasePtr = SpkWriteBuffer + VBASE;
  memset(SpkWriteBuffer, 0, SpkBufferSize);
  //
  // open DMA device driver
  //
  DMASpkWritefile_fd = open(VSPKDMADEVICE, O_RDWR);

  if (DMASpkWritefile_fd < 0) {
    t_print("%s: XDMA write device open failed for spk data\n", __FUNCTION__);
    exit(-1);
  }

  SetupFIFOMonitorChannel(eSpkCodecDMA, false);
  ResetDMAStreamFIFO(eSpkCodecDMA);
  return;
}

void saturn_handle_speaker_audio(const uint8_t *UDPInBuffer) {
  //uint32_t RegVal = 0;    //debug
  bool FIFOSpkOverflow, FIFOSpkUnderflow, FIFOSpkOverThreshold;;
  uint32_t DepthSpk = 0;
  unsigned int Current;     // current occupied locations in FIFO
  //RegVal += 1;            //debug
  DepthSpk = ReadFIFOMonitorChannel(eSpkCodecDMA, &FIFOSpkOverflow, &FIFOSpkOverThreshold, &FIFOSpkUnderflow,
                                    &Current);  // read the FIFO free locations
#ifdef DISPLAY_OVER_UNDER_FLOWS

  if (FIFOSpkOverThreshold) {
    t_print("Codec speaker FIFO Overthreshold, depth now = %d\n", Current);
  }

  if (FIFOSpkUnderflow) {
    t_print("Codec Speaker FIFO Underflowed, depth now = %d\n", Current);
  }

#endif

  //t_print("speaker data received; depth = %d\n", DepthSpk);
  while (DepthSpk < VMEMWORDSPERFRAME) {     // loop till space available
    usleep(1000);                                   // 1ms wait
    DepthSpk = ReadFIFOMonitorChannel(eSpkCodecDMA, &FIFOSpkOverflow, &FIFOSpkOverThreshold, &FIFOSpkUnderflow,
                                      &Current); // read the FIFO free locations
#ifdef DISPLAY_OVER_UNDER_FLOWS

    if (FIFOSpkOverThreshold) {
      t_print("Codec speaker FIFO Overthreshold, depth now = %d\n", Current);
    }

    if (FIFOSpkUnderflow) {
      t_print("Codec Speaker FIFO Underflowed, depth now = %d\n", Current);
    }

#endif
  }

  // copy data from UDP Buffer & DMA write it
  memcpy(SpkBasePtr, UDPInBuffer + 4, VDMASPKTRANSFERSIZE);              // copy out spk samples
  //    if(RegVal == 100)
  //        DumpMemoryBuffer(SpkBasePtr, VDMASPKTRANSFERSIZE);
  DMAWriteToFPGA(DMASpkWritefile_fd, SpkBasePtr, VDMASPKTRANSFERSIZE, VADDRSPKRSTREAMWRITE);
  return;
}

void saturn_exit() {
  //
  // clean exit
  //
  t_print("%s: Exiting\n", __FUNCTION__);
  Exiting = true;
  SDRActive = false;
  SetMOX(false);
  SetTXEnable(false);
  EnableCW(false, false);
  ServerActive = false;
  CloseXDMADriver();
  sem_destroy(&DDCInSelMutex);
  sem_destroy(&DDCResetFIFOMutex);
  sem_destroy(&RFGPIOMutex);
  sem_destroy(&CodecRegMutex);
}

#define VHIGHPRIOTIYFROMSDRSIZE 60

void start_saturn_high_priority_thread() {
  t_print("%s: \n", __FUNCTION__);
  saturn_high_priority_thread_id = g_thread_new( "SATURN HP OUT", saturn_high_priority_thread, NULL);

  if (!saturn_high_priority_thread_id) {
    t_print("%s: g_thread_new failed\n", __FUNCTION__);
    exit(-1);
  }
}

static gpointer saturn_high_priority_thread(gpointer arg) {
  uint8_t Byte;                                   // data being encoded
  uint16_t Word;                                  // data being encoded
  int Error;
  uint8_t UDPBuffer[VHIGHPRIOTIYFROMSDRSIZE];
  //
  // variables for outgoing UDP frame
  //
  struct sockaddr_in DestAddr;
  struct iovec iovecinst;
  struct msghdr datagram;

  while (!Exiting) {
    uint32_t SequenceCounter = 0;                       // sequence count
    uint32_t SequenceCounter2 = 0;

    while (!SDRActive) {
      usleep(10000);
    }

    memcpy(&DestAddr, &reply_addr, sizeof(struct
                                          sockaddr_in));           // local copy of PC destination address (reply_addr is global)
    memset(&iovecinst, 0, sizeof(struct iovec));
    memset(&datagram, 0, sizeof(struct msghdr));
    memset(UDPBuffer, 0, sizeof(UDPBuffer));
    iovecinst.iov_len = VHIGHPRIOTIYFROMSDRSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &DestAddr;                   // MAC addr & port to send to
    datagram.msg_namelen = sizeof(DestAddr);
    t_print("starting %s\n", __FUNCTION__);

    //
    // this is the main loop. SDR is running. transfer data;
    // also check for changes to DDC enabled, and DDC interleaved
    //
    // potential race conditions: thread execution order is underfined.
    // when a DDC becomes enabled, its paired DDC may not know yet and may still be set to interleaved.
    // when a DDC is set to interleaved, the paired DDC may not have been disabled yet.
    //
    while (SDRActive) {                            // main loop
      uint16_t SleepCount;                                      // counter for sending next message
      uint8_t PTTBits;                                          // PTT bits - and change means a new message needed
      mybuffer *mybuf = get_my_buffer(HPMYBUF);
      ReadStatusRegister();
      PTTBits = (uint8_t)GetP2PTTKeyInputs();
      *(uint8_t *)(UDPBuffer + 4) = *(uint8_t *)(mybuf->buffer + 4) = PTTBits;
      Byte = (uint8_t)GetADCOverflow();
      *(uint8_t *)(UDPBuffer + 5) = *(uint8_t *)(mybuf->buffer + 5) = Byte;
      Byte = (uint8_t)GetUserIOBits();                                              // user I/O bits
      *(uint8_t *)(UDPBuffer + 59) = *(uint8_t *)(mybuf->buffer + 59) = Byte;
      Word = (uint16_t)GetAnalogueIn(4);
      *(uint16_t *)(UDPBuffer + 6) = *(uint16_t *)(mybuf->buffer + 6) = htons(Word); // exciter power
      Word = (uint16_t)GetAnalogueIn(0);
      *(uint16_t *)(UDPBuffer + 14) = *(uint16_t *)(mybuf->buffer + 14) = htons(Word); // forward power
      Word = (uint16_t)GetAnalogueIn(1);
      *(uint16_t *)(UDPBuffer + 22) = *(uint16_t *)(mybuf->buffer + 22) = htons(Word); // reverse power
      Word = (uint16_t)GetAnalogueIn(5);
      *(uint16_t *)(UDPBuffer + 49) = *(uint16_t *)(mybuf->buffer + 49) = htons(Word); // supply voltage
      Word = (uint16_t)GetAnalogueIn(2);
      *(uint16_t *)(UDPBuffer + 57) = *(uint16_t *)(mybuf->buffer + 57) = htons(Word); // AIN3 user_analog1
      Word = (uint16_t)GetAnalogueIn(3);
      *(uint16_t *)(UDPBuffer + 55) = *(uint16_t *)(mybuf->buffer + 55) = htons(Word); // AIN4 user_analog2

      if (TXActive != 2) {
        *(uint32_t *)mybuf->buffer = htonl(SequenceCounter++);       // add sequence count
        saturn_post_high_priority(mybuf);
      } else {
        mybuf->free = 1;
      }

      if (ServerActive) {
        if (TXActive != 1) {
          *(uint32_t *)UDPBuffer = htonl(SequenceCounter2++);        // add sequence count
          iovecinst.iov_base = UDPBuffer;
          memcpy(&DestAddr, &reply_addr, sizeof(struct
                                                sockaddr_in));           // local copy of PC destination address (reply_addr is global)
          Error = sendmsg(SocketData[VPORTHIGHPRIORITYFROMSDR].Socketid, &datagram, 0);

          if (Error == -1) {
            t_print("Send Error, errno=%d, socket id = %d\n",
                    errno, SocketData[VPORTHIGHPRIORITYFROMSDR].Socketid);
            exit(-1);
          }
        }
      } else {
        SequenceCounter2 = 0;
      }

      //
      // now we need to sleep for 1ms (in TX) or 200ms (not in TX)
      // BUT if any of the PTT or key inputs change, send a message immediately
      // so break up the 200ms period with smaller sleeps
      //
      SleepCount = (MOXAsserted) ? 2 : 400;

      while (SleepCount-- > 0) {
        ReadStatusRegister();

        if ((uint8_t)GetP2PTTKeyInputs() != PTTBits) {
          break;
        }

        usleep(500);
      }
    }
  }

  t_print("ending: %s\n", __FUNCTION__);
  return NULL;
}

void start_saturn_micaudio_thread() {
  t_print("%s\n", __FUNCTION__);
  saturn_micaudio_thread_id = g_thread_new( "SATURN MIC", saturn_micaudio_thread, NULL);

  if (!saturn_micaudio_thread_id) {
    t_print("%s: g_thread_new failed\n", __FUNCTION__);
    exit(-1);
  }
}

static gpointer saturn_micaudio_thread(gpointer arg) {
  t_print( "%s\n", __FUNCTION__);
  //
  // variables for DMA buffer
  //
  uint8_t* MicReadBuffer = NULL;              // data for DMA read from DDC
  uint32_t MicBufferSize = VDMAMICBUFFERSIZE;
  unsigned char* MicBasePtr;                // ptr to DMA location in mic memory
  uint32_t Depth;
  int DMAReadfile_fd = -1;                  // DMA read file device
  uint32_t RegisterValue;
  bool FIFOOverflow, FIFOUnderflow, FIFOOverThreshold;
  unsigned int Current;                     // current occupied locations in FIFO
  uint8_t UDPBuffer[VMICPACKETSIZE];
  int Error;
  //
  // variables for outgoing UDP frame
  //
  struct sockaddr_in DestAddr;
  struct iovec iovecinst;
  struct msghdr datagram;
  //
  // setup DMA buffer
  //
  posix_memalign((void**)&MicReadBuffer, VALIGNMENT, MicBufferSize);

  if (!MicReadBuffer) {
    t_print("%s: mic read buffer allocation failed\n", __FUNCTION__);
    exit(-1);
  }

  MicBasePtr = MicReadBuffer + VBASE;
  memset(MicReadBuffer, 0, MicBufferSize);
  //
  // open DMA device driver
  //
  DMAReadfile_fd = open(VMICDMADEVICE, O_RDWR);

  if (DMAReadfile_fd < 0) {
    t_print("%s: XDMA read device open failed for mic data\n", __FUNCTION__);
    exit(-1);
  }

  //
  // now initialise Saturn hardware.
  // clear FIFO
  // then read depth
  //
  SetupFIFOMonitorChannel(eMicCodecDMA, false);
  ResetDMAStreamFIFO(eMicCodecDMA);
  RegisterValue = ReadFIFOMonitorChannel(eMicCodecDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                         &Current);  // read the FIFO Depth register
  t_print("%s: mic FIFO Depth register = %08x (should be ~0)\n", __FUNCTION__, RegisterValue);

  //
  // planned strategy: just DMA mic data when available; don't copy and DMA a larger amount.
  // if sufficient FIFO data available: DMA that data and transfer it out.
  // if it turns out to be too inefficient, we'll have to try larger DMA.
  //
  while (!Exiting) {
    uint32_t SequenceCounter = 0;
    uint32_t SequenceCounter2 = 0;

    while (!SDRActive) {
      usleep(10000);
    }

    memcpy(&DestAddr, &reply_addr, sizeof(struct
                                          sockaddr_in));           // local copy of PC destination address (reply_addr is global)
    memset(&iovecinst, 0, sizeof(struct iovec));
    memset(&datagram, 0, sizeof(struct msghdr));
    iovecinst.iov_len = VMICPACKETSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &DestAddr;                   // MAC addr & port to send to
    datagram.msg_namelen = sizeof(DestAddr);
    t_print("starting %s\n", __FUNCTION__);

    while (SDRActive) {
      //
      // now wait until there is data, then DMA it
      //
      Depth = ReadFIFOMonitorChannel(eMicCodecDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                     &Current);  // read the FIFO Depth register. 4 mic words per 64 bit word.
#ifdef DISPLAY_OVER_UNDER_FLOWS

      if (FIFOOverThreshold) {
        t_print("Codec Mic FIFO Overthreshold, depth now = %d\n", Current);
      }

      // note this would often generate a message because we deliberately read it down to zero.
      // this isn't a problem as we can send the data on without the code becoming blocked.
      //if(FIFOUnderflow)
      //  t_print("Codec Mic FIFO Underflowed, depth now = %d\n", Depth);
#endif

      while (Depth < (VMICSAMPLESPERFRAME / 4)) {         // 16 locations = 64 samples
        usleep(1000);                       // 1ms wait
        Depth = ReadFIFOMonitorChannel(eMicCodecDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                       &Current);  // read the FIFO Depth register
#ifdef DISPLAY_OVER_UNDER_FLOWS

        if (FIFOOverThreshold) {
          t_print("Codec Mic FIFO Overthreshold, depth now = %d\n", Current);
        }

        // note this would often generate a message because we deliberately read it down to zero.
        // this isn't a problem as we can send the data on without the code becoming blocked.
        //if(FIFOUnderflow)
        //  t_print("Codec Mic FIFO Underflowed, depth now = %d\n", Depth);
#endif
      }

      DMAReadFromFPGA(DMAReadfile_fd, MicBasePtr, VDMAMICTRANSFERSIZE, VADDRMICSTREAMREAD);
      // create the packet
      mybuffer *mybuf = get_my_buffer(MICMYBUF);
      *(uint32_t*)mybuf->buffer = htonl(SequenceCounter++);        // add sequence count

      if (TXActive == 2) {
        memset(mybuf->buffer + 4, 0, VDMAMICTRANSFERSIZE);  // copy in mic samples
      } else {
        memcpy(mybuf->buffer + 4, MicBasePtr, VDMAMICTRANSFERSIZE);  // copy in mic samples
      }

      saturn_post_micaudio(VMICPACKETSIZE, mybuf);

      if (ServerActive) {
        iovecinst.iov_base = UDPBuffer;
        memcpy(&DestAddr, &reply_addr, sizeof(struct
                                              sockaddr_in));           // local copy of PC destination address (reply_addr is global)
        // create the packet into UDPBuffer
        *(uint32_t*)UDPBuffer = htonl(SequenceCounter2++);        // add sequence count

        if (TXActive == 1) {
          memset(UDPBuffer + 4, 0, VDMAMICTRANSFERSIZE);  // copy in mic samples
        } else {
          memcpy(UDPBuffer + 4, MicBasePtr, VDMAMICTRANSFERSIZE);  // copy in mic samples
        }

        Error = sendmsg(SocketData[VPORTMICAUDIO].Socketid, &datagram, 0);

        if (Error == -1) {
          t_perror("sendmsg, Mic Audio");
          exit(-1);
        }
      } else {
        SequenceCounter2 = 0;
      }
    }
  }

  t_print("ending: %s\n", __FUNCTION__);
  return NULL;
}

void start_saturn_receive_thread() {
  t_print("%s\n", __FUNCTION__);
  saturn_rx_thread_id = g_thread_new( "SATURN RX", saturn_rx_thread, NULL);

  if (!saturn_rx_thread_id) {
    t_print("%s: g_thread_new failed\n", __FUNCTION__);
    exit(-1);
  }
}

extern struct ThreadSocketData SocketData[VPORTTABLESIZE];
extern struct sockaddr_in reply_addr;

static gpointer saturn_rx_thread(gpointer arg) {
  t_print( "%s\n", __FUNCTION__);
  //
  // memory buffers
  //
  uint32_t DMATransferSize;
  uint32_t ResidueBytes;
  uint32_t Depth;
  int IQReadfile_fd = -1;                     // DMA read file device
  uint32_t RegisterValue;
  bool FIFOOverflow, FIFOUnderflow, FIFOOverThreshold;
  int DDC;                                                    // iterator
  int Error;
  //
  // variables for outgoing UDP frame
  //
  struct sockaddr_in DestAddr[VNUMDDC];
  struct iovec iovecinst[VNUMDDC];                            // instance of iovec
  struct msghdr datagram[VNUMDDC];
  uint32_t SequenceCounter[VNUMDDC];                          // UDP sequence count
  //
  // variables for analysing a DDC frame
  //
  uint32_t FrameLength = 0;                                       // number of words per frame
  uint32_t DDCCounts[VNUMDDC];                                // number of samples per DDC in a frame
  uint32_t RateWord = 0;                                          // DDC rate word from buffer
  uint32_t HdrWord;                                           // check word read form DMA's data
  uint16_t* SrcWordPtr, * DestWordPtr;                        // 16 bit read & write pointers
  uint32_t PrevRateWord;                                      // last used rate word
  uint32_t Cntr;                                              // sample word counter
  bool HeaderFound;
  uint32_t DecodeByteCount;                                   // bytes to decode
  unsigned int Current;                                       // current occupied locations in FIFO
  //
  // initialise. Create memory buffers and open DMA file devices
  //
  PrevRateWord = 0xFFFFFFFF;                                  // illegal value to forc re-calculation of rates
  DMATransferSize = VDMATRANSFERSIZE;                         // initial size, but can be changed

  if (CreateDynamicMemory()) {
    t_print("%s: CreateDynamicMemory Failed\n", __FUNCTION__);
    exit(-1);
  }

  //
  // open DMA device driver
  //
  IQReadfile_fd = open(VDDCDMADEVICE, O_RDWR);

  if (IQReadfile_fd < 0) {
    t_print("%s: XDMA read device open failed for DDC data\n", __FUNCTION__);
    exit(-1);
  }

  //
  // now initialise Saturn hardware.
  // ***This is debug code at the moment. ***
  // clear FIFO
  // then read depth
  //
  //    RegisterWrite(0x1010, 0x0000002A);      // disable DDC data transfer; DDC2=test source
  SetRXDDCEnabled(false);
  usleep(1000);                           // give FIFO time to stop recording
  SetupFIFOMonitorChannel(eRXDDCDMA, false);
  ResetDMAStreamFIFO(eRXDDCDMA);
  RegisterValue = ReadFIFOMonitorChannel(eRXDDCDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                         &Current); // read the FIFO Depth register
  t_print("%s: DDC FIFO Depth register = %08x (should be ~0)\n", __FUNCTION__, RegisterValue);
  SetByteSwapping(true);                                            // h/w to generate network byte order
  //
  // thread loop. runs continuously until commanded by main loop to exit
  // for now: add 1 RX data + mic data at 48KHz sample rate. Mic data is constant zero.
  // while there is enough I/Q data, make outgoing packets;
  // when not enough data, read more.
  //
  //
  // enable Saturn DDC to transfer data
  //
  t_print("%s: enable data transfer\n", __FUNCTION__);
  SetRXDDCEnabled(true);
  HeaderFound = false;

  while (!Exiting) {
    while (!SDRActive) {
      usleep(10000);
    }

    for (DDC = 0; DDC < VNUMDDC; DDC++) {
      SequenceCounter[DDC] = 0;
      memcpy(&DestAddr[DDC], &reply_addr, sizeof(struct
             sockaddr_in));           // local copy of PC destination address (reply_addr is global)
      memset(&iovecinst[DDC], 0, sizeof(struct iovec));
      memset(&datagram[DDC], 0, sizeof(struct msghdr));
      iovecinst[DDC].iov_len = VDDCPACKETSIZE;
      datagram[DDC].msg_iov = &iovecinst[DDC];
      datagram[DDC].msg_iovlen = 1;
      datagram[DDC].msg_name = &DestAddr[DDC];                   // MAC addr & port to send to
      datagram[DDC].msg_namelen = sizeof(DestAddr);
    }

    t_print("starting %s\n", __FUNCTION__);

    while (SDRActive) {
      //
      // loop through all DDC I/Q buffers.
      // while there is enough I/Q data for this DDC in local (ARM) memory, make DDC Packets
      // then put any residues at the heads of the buffer, ready for new data to come in
      //
      for (DDC = 0; DDC < VNUMDDC; DDC++) {
        while ((IQHeadPtr[DDC] - IQReadPtr[DDC]) > VIQBYTESPERFRAME) {
          //                    t_print("enough data for packet: DDC= %d\n", DDC);
          mybuffer *mybuf = get_my_buffer(DDCMYBUF);
          *(uint32_t*)mybuf->buffer = htonl(SequenceCounter[DDC]++);     // add sequence count
          memset(mybuf->buffer + 4, 0, 8);                               // clear the timestamp data
          *(uint16_t*)(mybuf->buffer + 12) = htons(24);                  // bits per sample
          *(uint16_t*)(mybuf->buffer + 14) = htons(VIQSAMPLESPERFRAME);  // I/Q samples for ths frame
          //
          // now add I/Q data & post outgoing packet
          //
          memcpy(mybuf->buffer + 16, IQReadPtr[DDC], VIQBYTESPERFRAME);
          IQReadPtr[DDC] += VIQBYTESPERFRAME;

          if (DDC < 6) {
            if (ServerActive) {
              iovecinst[DDC].iov_base = mybuf->buffer;
              memcpy(&DestAddr[DDC], &reply_addr, sizeof(struct
                     sockaddr_in));           // local copy of PC destination address (reply_addr is global)
              Error = sendmsg(SocketData[VPORTDDCIQ0 + DDC].Socketid, &datagram[DDC], 0);

              if (Error == -1) {
                t_print("Send Error, DDC=%d, errno=%d, socket id = %d\n", DDC,
                        errno, SocketData[VPORTDDCIQ0 + DDC].Socketid);
                exit(-1);
              }
            } else {
              SequenceCounter[DDC] = 0;
            }

            mybuf->free = 1;
          } else {
            saturn_post_iq_data(DDC - 6, mybuf);
          }
        }

        //
        // now copy any residue to the start of the buffer (before the data copy in point)
        // unless the buffer already starts at or below the base
        // if we do a copy, the 1st free location is always base addr
        //
        ResidueBytes = IQHeadPtr[DDC] - IQReadPtr[DDC];

        //    t_print("Residue = %d bytes\n",ResidueBytes);
        if (IQReadPtr[DDC] > IQBasePtr[DDC]) {                              // move data down
          if (ResidueBytes != 0) {  // if there is residue to move
            memcpy(IQBasePtr[DDC] - ResidueBytes, IQReadPtr[DDC], ResidueBytes);
            IQReadPtr[DDC] = IQBasePtr[DDC] - ResidueBytes;
          } else {
            IQReadPtr[DDC] = IQBasePtr[DDC];
          }

          IQHeadPtr[DDC] = IQBasePtr[DDC];                            // ready for new data at base
        }
      }

      //
      // P2 packet sending complete.There are no DDC buffers with enough data to send out.
      // bring in more data by DMA if there is some, else sleep for a while and try again
      // we have the same issue with DMA: a transfer isn't exactly aligned to the amount we can read out
      // according to the DDC settings. So we either need to have the part-used DDC transfer variables
      // persistent across DMAs, or we need to recognise an incomplete fragment of a frame as such
      // and copy it like we do with IQ data so the next readout begins at a new frame
      // the latter approach seems easier!
      //
      Depth = ReadFIFOMonitorChannel(eRXDDCDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                     &Current);  // read the FIFO Depth register
#ifdef DISPLAY_OVER_UNDER_FLOWS

      if (FIFOOverThreshold) {
        t_print("RX DDC FIFO Overthreshold, depth now = %d\n", Current);
      }

      // note this could often generate a message at low sample rate because we deliberately read it down to zero.
      // this isn't a problem as we can send the data on without the code becoming blocked. so not a useful trap.
      //if(FIFOUnderflow)
      //  t_print("RX DDC FIFO Underflowed, depth now = %d\n", Current);
      //  t_print("read: depth = %d\n", Depth);
#endif

      //    t_print("read: depth = %d\n", Depth);
      while (Depth < (DMATransferSize / 8U)) { // 8 bytes per location
        usleep(500);               // 1ms wait
        Depth = ReadFIFOMonitorChannel(eRXDDCDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                       &Current);  // read the FIFO Depth register
#ifdef DISPLAY_OVER_UNDER_FLOWS

        if (FIFOOverThreshold) {
          t_print("RX DDC FIFO Overthreshold, depth now = %d\n", Current);
        }

        // note this could often generate a message at low sample rate because we deliberately read it down to zero.
        // this isn't a problem as we can send the data on without the code becoming blocked. so not a useful trap.
        //if(FIFOUnderflow)
        //  t_print("RX DDC FIFO Underflowed, depth now = %d\n", Current);
        //  t_print("read: depth = %d\n", Depth);
#endif
      }

      if (Depth > 4096) {
        DMATransferSize = 32768;
      } else if (Depth > 2048) {
        DMATransferSize = 16384;
      } else if (Depth > 1024) {
        DMATransferSize = 8192;
      } else {
        DMATransferSize = 4096;
      }

      //            t_print("DDC DMA read %d bytes from destination to base\n", DMATransferSize);
      DMAReadFromFPGA(IQReadfile_fd, DMAHeadPtr, DMATransferSize, VADDRDDCSTREAMREAD);
      DMAHeadPtr += DMATransferSize;

      //
      // find header: may not be the 1st word
      //
      //            DumpMemoryBuffer(DMAReadPtr, DMATransferSize);
      if (HeaderFound == false)                                                   // 1st time: look for header
        for (Cntr = 16; Cntr < (DMAHeadPtr - DMAReadPtr); Cntr += 8) {           // search for rate word; ignoring 1st
          if (*(DMAReadPtr + Cntr + 7) == 0x80) {
            //                        t_print("found header at offset=%x\n", Cntr);
            HeaderFound = true;
            DMAReadPtr += Cntr;                                             // point read buffer where header is
            break;
          }
        }

      if (HeaderFound == false) {                                      // if rate flag not set
        t_print("%s: Rate word not found when expected. rate= %08x\n", __FUNCTION__, RateWord);
        exit(1);
      }

      //
      // finally copy data to DMA buffers according to the embedded DDC rate words
      // the 1st word is pointed by DMAReadPtr and it should point to a DDC rate word
      // search for it if not!
      // (it should always be left in that state).
      // the top half of the 1st 64 bit word should be 0x8000
      // and that is located in the 2nd 32 bit location.
      // assume that DMA is > 1 frame.
      //            t_print("headptr = %x readptr = %x\n", DMAHeadPtr, DMAReadPtr);
      DecodeByteCount = DMAHeadPtr - DMAReadPtr;

      while (DecodeByteCount >= 16) {                     // minimum size to try!
        if (*(DMAReadPtr + 7) != 0x80) {
          t_print("%s: header not found for rate word at addr %p\n", __FUNCTION__, DMAReadPtr);
          exit(1);
        } else {                                                                          // analyse word, then process
          // cppcheck-suppress constVariablePointer
          uint32_t *LongWordPtr = (uint32_t*)DMAReadPtr;                                  // get 32 bit ptr
          RateWord = *LongWordPtr;                                                        // read rate word

          if (RateWord != PrevRateWord) {
            FrameLength = AnalyseDDCHeader(RateWord, &DDCCounts[0]);                      // read new settings
            //                        t_print("new framelength = %d\n", FrameLength);
            PrevRateWord = RateWord;                                                      // so so we know its analysed
          }

          if (DecodeByteCount >= ((FrameLength + 1) * 8)) {                               // if bytes for header & frame
            //THEN COPY DMA DATA TO I / Q BUFFERS
            DMAReadPtr += 8;                                                              // point to 1st location past rate word
            SrcWordPtr = (uint16_t*)DMAReadPtr;                                           // read sample data in 16 bit chunks

            for (DDC = 0; DDC < VNUMDDC; DDC++) {
              HdrWord = DDCCounts[DDC];                                                   // number of words for this DDC. reuse variable

              if (HdrWord != 0) {
                DestWordPtr = (uint16_t *)IQHeadPtr[DDC];

                for (Cntr = 0; Cntr < HdrWord; Cntr++) {                                  // count 64 bit words
                  *DestWordPtr++ = *SrcWordPtr++;                                         // move 48 bits of sample data
                  *DestWordPtr++ = *SrcWordPtr++;
                  *DestWordPtr++ = *SrcWordPtr++;
                  SrcWordPtr++;                                                           // and skip 16 bits where theres no data
                }

                IQHeadPtr[DDC] += 6 * HdrWord;                                            // 6 bytes per sample
              }

              // read N samples; write at head ptr
            }

            DMAReadPtr += FrameLength * 8;                                                // that's how many bytes we read out
            DecodeByteCount -= (FrameLength + 1) * 8;
          } else {
            break;  // if not enough left, exit loop
          }
        }
      }

      //
      // now copy any residue to the start of the buffer (before the data copy in point)
      // unless the buffer already starts at or below the base
      // if we do a copy, the 1st free location is always base addr
      //
      ResidueBytes = DMAHeadPtr - DMAReadPtr;

      //    t_print("Residue = %d bytes\n",ResidueBytes);
      if (DMAReadPtr > DMABasePtr) {                              // move data down
        if (ResidueBytes != 0) {  // if there is residue to move
          memcpy(DMABasePtr - ResidueBytes, DMAReadPtr, ResidueBytes);
          DMAReadPtr = DMABasePtr - ResidueBytes;
        } else {
          DMAReadPtr = DMABasePtr;
        }

        DMAHeadPtr = DMABasePtr;                            // ready for new data at base
      }
    }
  }

  t_print("ending: %s\n", __FUNCTION__);
  return NULL;
}

void saturn_init() {
  saturn_init_speaker_audio();
  saturn_init_duc_iq();
  start_saturn_receive_thread();
  start_saturn_micaudio_thread();
  start_saturn_high_priority_thread();
}

void saturn_handle_high_priority(bool FromNetwork, unsigned char *UDPInBuffer) {
  bool RunBit;                                          // true if "run" bit set
  uint8_t Byte, Byte2;                                  // received dat being decoded
  uint32_t LongWord;
  uint16_t Word;
  int i;                                                // counter
  ESoftwareID FPGASWID;                                 // preprod/release etc
  unsigned int FPGAVersion;                             // firmware version
  bool PAEnable;
  int DDCLoop = (FromNetwork) ? 6 : 4;
  int DDCOffset = (FromNetwork) ? 0 : 6;
  FPGAVersion = GetFirmwareVersion(&FPGASWID); // get version of FPGA code

  //t_print("high priority %sbuffer received\n", (FromNetwork)?"network ":"");
  //
  // now properly decode DDC frequencies
  //
  for (i = 0; i < DDCLoop; i++) {
    LongWord = ntohl(*(uint32_t *)(UDPInBuffer + i * 4 + 9));
    SetDDCFrequency(i + DDCOffset, LongWord, true);                 // temporarily set above
  }

  Byte = (uint8_t)(UDPInBuffer[4]);
  RunBit = (bool)(Byte & 1);
  IsTXMode = (bool)(Byte & 2);

  //if(!IsTXMode) TXActive = 0;
  if (FromNetwork) {
    if (RunBit) {
      StartBitReceived = true;

      if (ReplyAddressSet && StartBitReceived) {
        ServerActive = true;  // only set active if we have replay address too
      }
    } else {
      ServerActive = false;                                       // set state of whole app

      for (i = 4; i < VNUMDDC; i++) {        // disable upper bank of DDCs
        SetP2SampleRate(i, false, 48, false);
      }

      WriteP2DDCRateRegister();
      t_print("Server set to inactive by client app\n");
      StartBitReceived = false;
    }

    // for now just return until client TX issues can be worked out
    return;

    if (TXActive == 1) { return; }

    TXActive = (IsTXMode && client_enable_tx) ? 2 : 0;
  } else {
    if (RunBit) {
      SDRActive = true;
      SetTXEnable(true);
    } else {
      SDRActive = false;
      SetTXEnable(false);
      IsTXMode = false;
      SetMOX(false);
      EnableCW(false, false);
    }

    if (TXActive == 2) { return; }

    TXActive = (IsTXMode) ? 1 : 0;
  }

  SetMOX((bool)TXActive);
  //
  // DUC frequency & drive level
  //
  LongWord = ntohl(*(uint32_t *)(UDPInBuffer + 329));
  SetDUCFrequency(LongWord, true);
  Byte = (uint8_t)(UDPInBuffer[345]);
  SetTXDriveLevel(Byte);
  //
  // CAT port (if set)
  Word = ntohs(*(uint16_t *)(UDPInBuffer + 1398));
  //t_print("CAT over TCP port = %x\n", Word);
  //
  // transverter, speaker mute, open collector, user outputs
  // open collector data is in bits 7:1; move to 6:0
  //
  Byte = (uint8_t)(UDPInBuffer[1400]);
  SetXvtrEnable((bool)(Byte & 1));
  SetSpkrMute((bool)((Byte >> 1) & 1));
  Byte = (uint8_t)(UDPInBuffer[1401]);
  // According to  P2, the seven OC bits are b1:7
  SetOpenCollectorOutputs(Byte >> 1);
  Byte = (uint8_t)(UDPInBuffer[1402]);
  SetUserOutputBits(Byte);
  //
  // Alex
  // behaviour needs to be FPGA version specific: at V12, separate register added for Alex TX antennas
  // if new FPGA version: we write the word with TX ANT (byte 1428) to a new register, and the "old" word to original register
  // if we don't have a new TX ant bit set, just write "old" word data (byte 1432) to both registers
  // this is to allow safe operation with legacy client apps
  // 1st read bytes and see if a TX ant bit is set
  //
  // The word from 1398 is not yet used and overwritten here
  // cppcheck-suppress redundantAssignment
  //
  Word = ntohs(*(uint16_t *)(UDPInBuffer + 1428));
  Word = (Word >> 8) & 0x0007;                          // new data TX ant bits. if not set, must be legacy client app

  if ((FPGAVersion >= 12) && (Word != 0)) {             // if new firmware && client app supports it
    //t_print("new FPGA code, new client data\n");
    Word = ntohs(*(uint16_t *)(UDPInBuffer + 1428));    // copy word with TX ant settings to filt/TXant register
    PAEnable = (bool)((Word >> 11) & 1);
    //t_print("new FPGA code, legacy client data, PA enable = %d\n", (int)PAEnable);
    AlexManualTXFilters(Word, true);
    Word = ntohs(*(uint16_t *)(UDPInBuffer + 1432));    // copy word with RX ant settings to filt/RXant register
    AlexManualTXFilters(Word, false);
  } else if (FPGAVersion >= 12) {                       // new hardware but no client app support
    //t_print("new FPGA code, new client data\n");
    Word = ntohs(*(uint16_t *)(UDPInBuffer + 1432));    // copy word with TX/RX ant settings to both registers
    PAEnable = (bool)((Word >> 11) & 1);
    //t_print("new FPGA code, legacy client data, PA enable = %d\n", (int)PAEnable);
    AlexManualTXFilters(Word, true);
    AlexManualTXFilters(Word, false);
  } else {                                              // old FPGA hardware
    //t_print("old FPGA code\n");
    Word = ntohs(*(uint16_t *)(UDPInBuffer + 1432));    // copy word with TX/RX ant settings to original register
    PAEnable = (bool)((Word >> 11) & 1);
    //t_print("new FPGA code, legacy client data, PA enable = %d\n", (int)PAEnable);
    AlexManualTXFilters(Word, false);
  }

  SetPAEnabled(PAEnable); // activate PA if client app wants it
  // RX filters
  Word = ntohs(*(uint16_t *)(UDPInBuffer + 1430));
  AlexManualRXFilters(Word, 2);
  Word = ntohs(*(uint16_t *)(UDPInBuffer + 1434));
  AlexManualRXFilters(Word, 0);
  //
  // RX atten during TX and RX
  //
  Byte2 = (uint8_t)(UDPInBuffer[1442]);     // RX2 atten
  Byte = (uint8_t)(UDPInBuffer[1443]);      // RX1 atten
  SetADCAttenuator(eADC1, Byte, true, false);
  SetADCAttenuator(eADC2, Byte2, true, false);
  //
  // CWX bits
  //
  Byte = (uint8_t)(UDPInBuffer[5]);      // CWX
  SetCWXBits((bool)(Byte & 1), (bool)((Byte >> 2) & 1), (bool)((Byte >> 1) & 1)); // enabled, dash, dot
  return;
}

void saturn_handle_general_packet(bool FromNetwork, uint8_t *PacketBuffer) {
  uint16_t Port;                                  // port number from table
  uint8_t Byte;

  //t_print("general %sbuffer received\n", (FromNetwork)?"network ":"");
  if (FromNetwork) { return; } //RRK

  //
  // now set the other data carried by this packet
  // wideband capture data:
  //
  Byte = *(uint8_t*)(PacketBuffer + 23);              // get wideband enables
  SetWidebandEnable(eADC1, (bool)(Byte & 1));
  SetWidebandEnable(eADC2, (bool)(Byte & 2));
  Port = ntohs(*(uint16_t*)(PacketBuffer + 24));      // wideband sample count
  SetWidebandSampleCount(Port);
  Byte = *(uint8_t*)(PacketBuffer + 26);              // wideband sample size
  SetWidebandSampleSize(Byte);
  Byte = *(uint8_t*)(PacketBuffer + 27);              // wideband update rate
  SetWidebandUpdateRate(Byte);
  Byte = *(uint8_t*)(PacketBuffer + 28);              // wideband packets per frame
  SetWidebandPacketsPerFrame(Byte);
  //
  // envelope PWM data:
  //
  Port = ntohs(*(uint16_t*)(PacketBuffer + 33));      // PWM min
  SetMinPWMWidth(Port);
  Port = ntohs(*(uint16_t*)(PacketBuffer + 35));      // PWM max
  SetMaxPWMWidth(Port);
  //
  // various bits
  //
  Byte = *(uint8_t*)(PacketBuffer + 37);              // flag bits
  EnableTimeStamp((bool)(Byte & 1));
  EnableVITA49((bool)(Byte & 2));
  SetFreqPhaseWord((bool)(Byte & 8));
  Byte = *(uint8_t*)(PacketBuffer + 38);              // enable timeout
  HW_Timer_Enable = ((bool)(Byte & 1));
  Byte = *(uint8_t*)(PacketBuffer + 58);              // Only Bit0 used for "PA enable"
  SetPAEnabled((bool)(Byte & 1));
  //SetApolloEnabled((bool)(Byte & 2));
  Byte = *(uint8_t*)(PacketBuffer + 59);              // Alex enable bits
  SetAlexEnabled(Byte);
  return;
}

void saturn_handle_ddc_specific(bool FromNetwork, unsigned char *UDPInBuffer) {
  uint8_t Byte1, Byte2;                                 // received data
  uint16_t Word;                                        // 16 bit read value
  EADCSelect ADC = eADC1;                               // ADC to use for a DDC
  int DDCLoop = (FromNetwork) ? 6 : 4;
  int DDCOffset = (FromNetwork) ? 0 : 6;

  //t_print("DDC specific %sbuffer received\n", (FromNetwork)?"network ":"");
  if (!FromNetwork) {
    bool Dither, Random;                                  // ADC bits
    // get ADC details:
    Byte1 = *(uint8_t*)(UDPInBuffer + 4);                 // get ADC count
    SetADCCount(Byte1);
    Byte1 = *(uint8_t*)(UDPInBuffer + 5);                 // get ADC Dither bits
    Byte2 = *(uint8_t*)(UDPInBuffer + 6);                 // get ADC Random bits
    Dither  = (bool)(Byte1 & 1);
    Random  = (bool)(Byte2 & 1);
    SetADCOptions(eADC1, false, Dither, Random);          // ADC1 settings
    Byte1 = Byte1 >> 1;                                   // move onto ADC bits
    Byte2 = Byte2 >> 1;
    Dither  = (bool)(Byte1 & 1);
    Random  = (bool)(Byte2 & 1);
    SetADCOptions(eADC2, false, Dither, Random);          // ADC2 settings
  }

  //
  // main settings for each DDC
  // reuse "dither" for interleaved with next;
  // reuse "random" for DDC enabled.
  // be aware an interleaved "odd" DDC will usually be set to disabled, and we need to revert this!
  //
  Word = *(uint16_t*)(UDPInBuffer + 7);                 // get DDC enables 15:0 (note it is already low byte 1st!)

  for (int i = 0; i < DDCLoop; i++) {
    uint16_t Word2;                                   // 16 bit read value
    bool Enabled, Interleaved;                        // DDC settings
    Enabled = (bool)(Word & 1);                       // get enable state
    Byte1 = *(uint8_t*)(UDPInBuffer + i * 6 + 17);    // get ADC for this DDC
    Word2 = *(uint16_t*)(UDPInBuffer + i * 6 + 18);   // get sample rate for this DDC
    Word2 = ntohs(Word2);                             // swap byte order
    Byte2 = *(uint8_t*)(UDPInBuffer + i * 6 + 22);    // get sample size for this DDC
    SetDDCSampleSize(i + DDCOffset, Byte2);

    if (Byte1 == 0) {
      ADC = eADC1;
    } else if (Byte1 == 1) {
      ADC = eADC2;
    } else if (Byte1 == 2) {
      ADC = eTXSamples;
    }

    SetDDCADC(i + DDCOffset, ADC);
    Interleaved = false;                                 // assume no synch

    // finally DDC synchronisation: my implementation it seems isn't what the spec intended!
    // check: is DDC1 programmed to sync with DDC0;
    // check: is DDC3 programmed to sync with DDC2;
    // check: is DDC5 programmed to sync with DDC4;
    // check: is DDC7 programmed to sync with DDC6;
    // check: if DDC1 synch to DDC0, enable it;
    // check: if DDC3 synch to DDC2, enable it;
    // check: if DDC5 synch to DDC4, enable it;
    // check: if DDC7 synch to DDC6, enable it;
    // (reuse the Dither variable)
    switch (i) {
    case 0:
      Byte1 = *(uint8_t*)(UDPInBuffer + 1363);          // get DDC0 synch

      if (Byte1 == 0b00000010) {
        Interleaved = true;  // set interleave
      }

      break;

    case 1:
      Byte1 = *(uint8_t*)(UDPInBuffer + 1363);          // get DDC0 synch

      if (Byte1 == 0b00000010) {                        // if synch to DDC1
        Enabled = true;  // enable DDC1
      }

      break;

    case 2:
      Byte1 = *(uint8_t*)(UDPInBuffer + 1365);          // get DDC2 synch

      if (Byte1 == 0b00001000) {
        Interleaved = true;  // set interleave
      }

      break;

    case 3:
      Byte1 = *(uint8_t*)(UDPInBuffer + 1365);          // get DDC2 synch

      if (Byte1 == 0b00001000) {                        // if synch to DDC3
        Enabled = true;  // enable DDC3
      }

      break;

    case 4:
      Byte1 = *(uint8_t*)(UDPInBuffer + 1367);          // get DDC4 synch

      if (Byte1 == 0b00100000) {
        Interleaved = true;  // set interleave
      }

      break;

    case 5:
      Byte1 = *(uint8_t*)(UDPInBuffer + 1367);          // get DDC4 synch

      if (Byte1 == 0b00100000) {                        // if synch to DDC5
        Enabled = true;  // enable DDC5
      }

      break;

    case 6:
      Byte1 = *(uint8_t*)(UDPInBuffer + 1369);          // get DDC6 synch

      if (Byte1 == 0b10000000) {
        Interleaved = true;  // set interleave
      }

      break;

    case 7:
      Byte1 = *(uint8_t*)(UDPInBuffer + 1369);          // get DDC6 synch

      if (Byte1 == 0b10000000) {                        // if synch to DDC7
        Enabled = true;  // enable DDC7
      }

      break;
    }

    SetP2SampleRate(i + DDCOffset, Enabled, Word2, Interleaved);
    Word = Word >> 1;                                 // move onto next DDC enabled bit
  }

  WriteP2DDCRateRegister();
  return;
}

void saturn_handle_duc_specific(bool FromNetwork, unsigned char *UDPInBuffer) {
  uint8_t Byte;
  uint16_t SidetoneFreq;                                // freq for audio sidetone
  uint8_t IambicSpeed;                                  // WPM
  uint8_t IambicWeight;                                 //
  uint8_t SidetoneVolume;
  uint8_t CWRFDelay;
  uint16_t CWHangDelay;
  uint8_t CWRampTime;

  //t_print("DUC specific %sbuffer received\n", (FromNetwork)?"network ":"");
  if (FromNetwork) {
    if (TXActive == 1 || !client_enable_tx) { return; }
  } else {
    if (TXActive == 2) { return; }
  }

  // iambic settings
  IambicSpeed = *(uint8_t*)(UDPInBuffer + 9);             // keyer speed
  IambicWeight = *(uint8_t*)(UDPInBuffer + 10);           // keyer weight
  Byte = *(uint8_t*)(UDPInBuffer + 5);                    // keyer bool bits
  SetCWIambicKeyer(IambicSpeed, IambicWeight, (bool)((Byte >> 2) & 1), (bool)((Byte >> 5) & 1),
                   (bool)((Byte >> 6) & 1), (bool)((Byte >> 3) & 1), (bool)((Byte >> 7) & 1));
  // general CW settings
  SetCWSidetoneEnabled((bool)((Byte >> 4) & 1));
  EnableCW((bool)((Byte >> 1) & 1), (bool)((Byte >> 7) & 1)); // CW enabled bit, breakin bit
  SidetoneVolume = *(uint8_t*)(UDPInBuffer + 6);          // keyer speed
  SidetoneFreq = *(uint16_t*)(UDPInBuffer + 7);           // get frequency
  SidetoneFreq = ntohs(SidetoneFreq);                     // convert from big endian
  SetCWSidetoneVol(SidetoneVolume);
  SetCWSidetoneFrequency(SidetoneFreq);
  CWRFDelay = *(uint8_t*)(UDPInBuffer + 13);              // delay before CW on
  CWHangDelay = *(uint16_t*)(UDPInBuffer + 11);           // delay before CW off
  CWHangDelay = ntohs(CWHangDelay);                       // convert from big endian
  SetCWPTTDelay(CWRFDelay);
  SetCWHangTime(CWHangDelay);
  CWRampTime = *(uint8_t*)(UDPInBuffer + 17);             // ramp transition time

  if (CWRampTime != 0) {                                  // if ramp period supported by client app
    uint32_t CWRampTime_us = 1000 * CWRampTime;
    InitialiseCWKeyerRamp(true, CWRampTime_us);         // create required ramp, P2
  }

  // mic and line in options
  Byte = *(uint8_t*)(UDPInBuffer + 50);                   // mic/line options
  SetMicBoost((bool)((Byte >> 1) & 1));
  SetMicLineInput((bool)(Byte & 1));
  SetOrionMicOptions((bool)((Byte >> 3) & 1), (bool)((Byte >> 4) & 1), (bool)((~Byte >> 2) & 1));
  SetBalancedMicInput((bool)((Byte >> 5) & 1));
  Byte = *(uint8_t*)(UDPInBuffer + 51);                   // line in gain
  SetCodecLineInGain(Byte);
  Byte = *(uint8_t*)(UDPInBuffer + 58);                   // ADC1 att on TX
  SetADCAttenuator(eADC2, Byte, false, true);
  Byte = *(uint8_t*)(UDPInBuffer + 59);                   // ADC1 att on TX
  SetADCAttenuator(eADC1, Byte, false, true);
  return;
}

//
// Some functions calls to get the "allowed" SATURN FPGA version numbers
//
int saturn_minor_version_min() { return FIRMWARE_MIN_MINOR; }

int saturn_minor_version_max() { return FIRMWARE_MAX_MINOR; }

int saturn_major_version_min() { return FIRMWARE_MIN_MAJOR; }

int saturn_major_version_max() { return FIRMWARE_MAX_MAJOR; }
