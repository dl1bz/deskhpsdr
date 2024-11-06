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

#ifndef _DISCOVERED_H
#define _DISCOVERED_H

#include <netinet/in.h>
#ifdef SOAPYSDR
  #include <SoapySDR/Device.h>
#endif

#define MAX_DEVICES 16

// ANAN 7000DLE and 8000DLE uses 10 as the device type in old protocol
// Newer STEMlab hpsdr emulators use 100 instead of 1
// HermesLite V2 uses V1 board ID and software version >= 40
//
// To avoid probing both the protocol and the device, device numbers
// need to be unique across protocols, and should be defined here
// for all protocols, even if piHPSDR is not compiled for that
// protocol (so define DEVICE_OZY and SOAPYSDR_USB_DEVICE in all cases!)
//
#define DEVICE_METIS               0
#define DEVICE_HERMES              1
#define DEVICE_GRIFFIN             2
#define DEVICE_ANGELIA             4
#define DEVICE_ORION               5
#define DEVICE_HERMES_LITE         6
#define DEVICE_HERMES_LITE2      506
#define DEVICE_ORION2             10
#define DEVICE_STEMLAB           100
#define DEVICE_STEMLAB_Z20       101

#define DEVICE_OZY                 7

#define NEW_DEVICE_ATLAS        1000
#define NEW_DEVICE_HERMES       1001
#define NEW_DEVICE_HERMES2      1002
#define NEW_DEVICE_ANGELIA      1003
#define NEW_DEVICE_ORION        1004
#define NEW_DEVICE_ORION2       1005
#define NEW_DEVICE_HERMES_LITE  1006
#define NEW_DEVICE_SATURN       1010
#define NEW_DEVICE_HERMES_LITE2 1506

#define SOAPYSDR_USB_DEVICE     2000

#define STATE_AVAILABLE 2
#define STATE_SENDING 3
#define STATE_INCOMPATIBLE 4

#define ORIGINAL_PROTOCOL 0
#define NEW_PROTOCOL      1
#define SOAPYSDR_PROTOCOL 2
#define STEMLAB_PROTOCOL  5

// A STEMlab discovered via Avahi will have this protocol until the SDR
// application itself is started, at which point it will be changed to the old
// protocol and proceed to be handled just like a normal HPSDR radio.
//
// Since there are multiple HPSDR applications for the STEMlab, but not all
// are always installed, we need to keep track of which are installed, so the
// user can choose which one should be started.
// The software version field will be abused for this purpose,
// and we use one bit to distinguish between fancy (STEMlab) and
// barebone (ALPINE) RedPitayas.
//
#define STEMLAB_PAVEL_RX   1    // found: sdr_receiver_hpsdr
#define STEMLAB_PAVEL_TRX  2    // found: sdr_transceiver_hpsdr
#define STEMLAB_RP_TRX     4    // found: stemlab_sdr_transceiver_hpsdr
#define HAMLAB_RP_TRX      8    // found: hamlab_sdr_transceiver_hpsdr
#define BARE_REDPITAYA    16    // barebone RedPitaya (no STEMlab)

struct _DISCOVERED {
  int protocol;
  int device;
  int use_tcp;        // Radio connection is via TCP
  int use_routing;    // Radio connection is "routed" to some IP address
  char name[64];
  int software_version;
  int fpga_version;
  int status;
  int supported_receivers;
  int supported_transmitters;
  int adcs;
  int dacs;
  double frequency_min;
  double frequency_max;
  union {
    struct network {
      unsigned char mac_address[6];
      int address_length;
      struct sockaddr_in address;
      int interface_length;
      struct sockaddr_in interface_address;
      struct sockaddr_in interface_netmask;
      char interface_name[64];
    } network;

#ifdef SOAPYSDR
    struct soapy {
      char version[128];
      char hardware_key[64];
      char driver_key[64];
      int rtlsdr_count;
      int sdrplay_count;
      int sample_rate;
      size_t rx_channels;
      size_t rx_gains;
      char **rx_gain;
      SoapySDRRange *rx_range;
      gboolean rx_has_automatic_gain;
      gboolean rx_has_automatic_dc_offset_correction;
      size_t rx_antennas;
      char **rx_antenna;
      size_t tx_channels;
      size_t tx_gains;
      char **tx_gain;
      SoapySDRRange *tx_range;
      size_t tx_antennas;
      char **tx_antenna;
      size_t sensors;
      char **sensor;
      gboolean has_temp;
      char address[64];
    } soapy;

#endif
  } info;
};

typedef struct _DISCOVERED DISCOVERED;

extern int selected_device;
extern int devices;
extern DISCOVERED discovered[MAX_DEVICES];

#endif
