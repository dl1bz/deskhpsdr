/* Copyright (C)
* 2022 - Christoph van Wüllen, DL1YCF
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

///////////////////////////////////////////////////////////////////////////////////////
//
// HPSDR protocol1 bootloader emulator.
//
// This program successfully communicates with the "HPSDR protocol1 bootloader".
// It must be run as root since it "sniffs" on the Ethernet interface to
// look for packets from the host.
//
// Arguments:
//
// -d            Display IPv4 ethernet adapters present in the system,
//               together with their IP and MAC address.
//
// -i <adapter>  Use specific adapter. The name <adapter> must match
//               one of the adapters displayed with -d
//
// -o outfile    If a firmware file is "uploaded", it will be stored
//               in that file.
//
// Note -d and -i are mutually exclusive, the last one wins. If neither
// -d nor -i is given, -d is assumed.
//
///////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pcap.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <fcntl.h>
#include <net/if_dl.h>
#include <string.h>

void sendRawCommand(pcap_t *handle, unsigned char hw[6], unsigned char command, unsigned char *data, int datalen);

// HPSDR P1 bootloader command codes

#define PROGRAM_METIS_FLASH 0x01
#define ERASE_METIS_FLASH   0x02
#define READ_METIS_MAC      0x03
#define READ_METIS_IP       0x04
#define WRITE_METIS_IP      0x05
#define GET_JTAG_DEVICE_ID  0x06
#define PROGRAM_MERCURY     0x07
#define PROGRAM_PENELOPE    0x08
#define JTAG_ERASE_FLASH    0x09
#define PROGRAM_FLASH       0x0A

// HPSDR bootloader reply codes

#define ERASE_DONE       0x01
#define SEND_MORE        0x02
#define HAVE_MAC_ADDRESS 0x03
#define HAVE_IP_ADDRESS  0x04

int main(int argc, char **argv) {
  int i, j, k, l;
  char *dev = NULL;                     // Which ethernet adapter to use
  char errbuf[PCAP_ERRBUF_SIZE];        // error message from pcap
  pcap_t* descr;                        // main pcap 'handle'
  const u_char *packet;                 // received ethernet packet raw data
  struct pcap_pkthdr hdr;               // received ethernet packet header
  unsigned char mymac[6];               // MAC address of radio (that is, of THIS host)
  unsigned char myip[4];                // IP  address of radio (that is, of THIS host)
  unsigned char hismac[6];              // MAC address of PC running the bootloader
  pcap_if_t *devlist,*ifp;              // Data used in the ethernet card detection loop
  pcap_addr_t *addr;
  struct sockaddr *sa;
  struct sockaddr_dl *link;
  char string[256];
  int have_addr, have_mac;
  int do_display, do_lookup;
  char *outfile = NULL;
  int outfd;
  int count;
  int progress;
  //
  // Set defaults and analyze arguments
  //
  do_display = 1;
  do_lookup = 0;
  i = 1;

  while (i < argc) {
    if (!strcmp(argv[i], "-d")) {
      do_display = 1;
      do_lookup = 0;
    }

    if (!strcmp(argv[i], "-i") && i + 1 < argc) {
      dev = argv[++i];
      do_display = 0;
      do_lookup = 1;
    }

    if (!strcmp(argv[i], "-o") && i + 1 < argc) {
      outfile = argv[++i];
    }

    i++;
  }

  if (do_display || do_lookup) {
    //
    // loop through all Ethernet devices,
    // either simply list them (do_display)
    // or get data of the target device (do_lookup)
    //
    if (pcap_findalldevs(&devlist, errbuf) == -1) {
      printf("Error in pcap_findalldevs_ex: %s\n", errbuf);
      exit(1);
    }

    ifp = devlist;

    while (ifp != NULL) {
      if (do_lookup && strcmp(ifp->name, dev)) {
        ifp = ifp->next;
        continue;
      }

      have_addr = 0;
      have_mac = 0;
      addr = ifp->addresses;

      while (addr != NULL) {
        sa = addr->addr;

        if (sa->sa_family == AF_INET) {
          (void) inet_ntop(AF_INET, (void *) & (((struct sockaddr_in *)sa)->sin_addr), string, 256);

          if (sscanf(string, "%d.%d.%d.%d", &i, &j, &k, &l) == 4) {
            myip[0] = i;
            myip[1] = j;
            myip[2] = k;
            myip[3] = l;

            if (i > 127) { have_addr = 1; }
          }
        }

        if (sa->sa_family == AF_LINK) {
          link = (struct sockaddr_dl *)sa->sa_data;
          unsigned char mac[link->sdl_alen];
          memcpy(mac, LLADDR(link), link->sdl_alen);

          if (link->sdl_alen == 6) {
            mymac[0] = mac[0];
            mymac[1] = mac[1];
            mymac[2] = mac[2];
            mymac[3] = mac[3];
            mymac[4] = mac[4];
            mymac[5] = mac[5];
          }

          if (link->sdl_alen > 6) {
            //
            // This happens on MacOS
            //
            mymac[0] = mac[1];
            mymac[1] = mac[2];
            mymac[2] = mac[3];
            mymac[3] = mac[4];
            mymac[4] = mac[5];
            mymac[5] = mac[6];
          }

          have_mac = 1;
        }

        addr = addr->next;
      }

      if (have_addr && have_mac) {
        printf("Interface=%-10s Address=(%3d,%3d,%3d,%3d) MAC=%02x:%02x:%02x:%02x:%02x:%02x\n", ifp->name, myip[0], myip[1],
               myip[2], myip[3],
               mymac[0], mymac[1], mymac[2], mymac[3], mymac[4], mymac[5]);
      }

      ifp = ifp->next;
    }

    pcap_freealldevs(devlist);
  }

  //
  // In this case, we are finished
  //
  if (do_display) { return 0; }

  //
  // From now on we need a valid ethernet interface
  //
  if (dev == NULL || !have_addr || !have_mac) { return 8; }

  outfd = -1;
  //
  // FROM NOW ON, we need administrator privileges
  //
  descr = pcap_open_live(dev, 1024, 1, 10, errbuf);

  if (descr == NULL) {
    printf("pcap_open_live(): %s\n", errbuf);
    exit(1);
  }

  count = 0;
  progress = 0;

  for (;;) {
    //
    // spin around "sniffing" the ethernet adapter
    //
    if (count > 0) {
      count--;

      if (count == 0) {
        close(outfd);
        outfd = -1;
        progress = 0;
        printf("\nOutput file %s closed.\n", outfile);
      }
    }

    packet = pcap_next(descr, &hdr);

    if (packet == NULL) { continue; } // Nothing arrived within time-out

    /* determine if this is a bootloader packet */
    if (hdr.len > 22 && packet[0] == 0x11 && packet[1] == 0x22 && packet[2] == 0x33
    && packet[3] == 0x44 && packet[4] == 0x55 && packet[5] == 0x66
    && packet[12] == 0xef && packet[13] == 0xfe) {
      hismac[0] = packet[6];
      hismac[1] = packet[7];
      hismac[2] = packet[8];
      hismac[3] = packet[9];
      hismac[4] = packet[10];
      hismac[5] = packet[11];

      //printf("Bootloader packet, his MAC=%02x:%02x:%02x:%02x:%02x:%02x Commands=%d:%d\n", hismac[0], hismac[1], hismac[2], hismac[3], hismac[4], hismac[5],packet[14],packet[15]);
      if (packet[14] == 3) {
        switch (packet[15]) {
        case READ_METIS_MAC:
          sendRawCommand(descr,  hismac, HAVE_MAC_ADDRESS, mymac, 6);
          printf("Sent MAC address\n");
          break;

        case READ_METIS_IP:
          sendRawCommand(descr, hismac, HAVE_IP_ADDRESS, myip, 4);
          printf("Sent IP address\n");
          break;

        case WRITE_METIS_IP:
          myip[0] = packet[16];
          myip[1] = packet[17];
          myip[2] = packet[18];
          myip[3] = packet[19];
          printf("IP address set to (%d,%d,%d,%d)\n", myip[0], myip[1], myip[2], myip[3]);
          break;

        case ERASE_METIS_FLASH:
          sleep(2);  // well, erase takes  some time
          sendRawCommand(descr, hismac, ERASE_DONE, NULL, 0);
          printf("Erased.\n");
          break;

        case PROGRAM_METIS_FLASH:

          // data is 276 bytes long
          if (outfd < 0 && outfile != NULL) {
            progress = 0;
            outfd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC);

            if (outfd < 0) {
              perror("OPEN outfile:");
              outfile = NULL;
            }
          }

          if (outfd >= 0) {
            write (outfd, packet + 20, 256);
            printf("Writing to file %s: %5d\r", outfile, progress++);
            fflush(stdout);
            count = 100; // timeout is 10msec so we wait for 1 sec.
          }

          sendRawCommand(descr, hismac, SEND_MORE, NULL, 0);
          break;

        case GET_JTAG_DEVICE_ID:
          printf("JTAG GetDeviceID received, should not happen.\n");
          break;

        case PROGRAM_MERCURY:
          printf("JTAG ProgramMercury received, should not happen.\n");
          break;

        case PROGRAM_PENELOPE:
          printf("JTAG ProgramPenelope received, should not happen.\n");
          break;

        case JTAG_ERASE_FLASH:
          printf("JTAG erase flash received, should not happen.\n");
          break;

        case PROGRAM_FLASH:
          printf("JTAG Program flash received, should not happen.\n");
          break;

        default:
          printf("UNKNOWN COMMAND=%d\n", packet[15]);
          break;
        }
      }
    }
  }
}

void sendRawCommand(pcap_t *handle, unsigned char dst[6], unsigned char command, unsigned char *data, int datalen) {
  unsigned char buffer[62];
  int i;

  if (datalen < 0 || datalen > 46) { datalen = 0; } // should not happen

  //
  // Header is:
  // dest MAC address (6 bytes)
  // source MAC address (6 bytes, value fixed at 11:22:33:44:55:55)
  // 0xEF 0xFE 0x03 (3 bytes)
  // command (1 byte)
  //
  // Then follows "some" data (0, 4, or 6 bytes)
  // and then, the packet is padded with zeroes up to 62 bytes
  //
  buffer[ 0] = dst[0];
  buffer[ 1] = dst[1];
  buffer[ 2] = dst[2];
  buffer[ 3] = dst[3];
  buffer[ 4] = dst[4];
  buffer[ 5] = dst[5];
  buffer[ 6] = 0x11;
  buffer[ 7] = 0x22;
  buffer[ 8] = 0x33;
  buffer[ 9] = 0x44;
  buffer[10] = 0x55;
  buffer[11] = 0x66;
  buffer[12] = 0xEF;
  buffer[13] = 0xFE;
  buffer[14] = 0x03;
  buffer[15] = command;

  //
  // Include data, if present
  //
  for (i = 0; i < datalen; i++) {
    buffer[i + 16] = data[i];
  }

  //
  // pad with zeroes
  //
  for (i = 16 + datalen; i <= 62; i++) {
    buffer[i + 16] = (unsigned char)0x00;
  }

  //
  // ship out packet
  //
  if (pcap_sendpacket(handle, buffer, 62) != 0) {
    printf("Send Raw failed, command=%d\n", command);
  }
}
