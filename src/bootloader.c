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

///////////////////////////////////////////////////////////////////////////
//
// HPSDR protocol-1 bootloader
//
// This program successfully communicates with the protocol-1 bootloader
// insider the SDR.
// It must be run as root since it "sniffs" on the Ethernet interface to
// look for packets from the radio (using the pcap library),
// so this is "spy-ware" but that's the requirement of the P1 bootloader.
//
// This is the reason why you cannot use this program accross a VPN or
// a managed switch. This is based on illegal (or let's say non-routable)
// data packets. You either have to use a "dump switch" or (better)
// a direct-cable-connection.
//
// For older ANAN radios, you have to place a jumper on the HPSDR board
// to put the board into "bootloader" mode. More recent radios are more
// comfortable, you simply have to change a switch (at the bottom or
// the back panel of the case) from "normal" to "bootloader" position.
//
// The reason to have a bootloader depending on "non-routable" packets is,
// that the radio probably does not even need an IP address to communicate
// with the bootloader. All packets to/from the radio are identified by
// the (bogus) MAC address 11:22:33:44:55:66 that the radio is using. The
// main advantage of this "archaic" protocol is that with most ANAN radios,
// you can use it even if the firmware that you have uploaded last time is
// corrupt. In the "community slang", this can be used to "un-brick" your
// radio.
//
// The core parts of the program have been taken from John Melton's
// "bootloader" code from the TAPR github repository, and converted
// to a command-line program for Linux and MacOS contained in a single file.
//
///////////////////////////////////////////////////////////////////////////
//
// Arguments:
//
// -d            Display IPv4 ethernet adapters present in the system,
//               together with their IP and MAC address.
//
// -i <adapter>  Use specific adapter. The name <adapter> must match
//               one of the adapters displayed with -d
//
// -s <ip-addr>  Set IP addr of radio to <ip-addr>  (If addr=0.0.0.0, use DHCP)
//
// -f <file>     The name of the firmware file to be uploaded
//
///////////////////////////////////////////////////////////////////////////
//
// -d is the default, it is over-ridden if -i is given.
//
// Without specifying valid device using the -i option, the -s and -f options
// cannot work.
//
///////////////////////////////////////////////////////////////////////////

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
#ifdef __APPLE__
  #include <net/if_dl.h>
#else
  #include <sys/ioctl.h>
  #include <net/if.h>
#endif
#include <string.h>

//
// Forward declaration for the two functions that send something to the radio
//
void sendRawCommand(pcap_t *handle, unsigned char hw[6], unsigned char command, unsigned char *data, int datalen);
void sendRawData(pcap_t *handle, unsigned char hw[6], unsigned char *data, off_t ptr);

// HPSDR P1 bootloader command codes (we don't need those related to JTAG)

#define PROGRAM_METIS_FLASH 0x01
#define ERASE_METIS_FLASH   0x02
#define READ_METIS_MAC      0x03
#define READ_METIS_IP       0x04
#define WRITE_METIS_IP      0x05

// HPSDR bootloader reply codes

#define ERASE_DONE       0x01  // Sent when the ERASE_METIS_FLASH command is completed.
#define SEND_MORE        0x02  // Sent when the data of the PROGRAM_METIS_FLASH command has been processed.
#define HAVE_MAC_ADDRESS 0x03  // Sent as response to READ_METIS_MAC
#define HAVE_IP_ADDRESS  0x04  // Sent as response to READ_METIS_IP

// The state of our program.

#define STATE_QUERYMAC  0  // Query MAC address of radio
#define STATE_QUERYIP   1  // Query IP address of radio
#define STATE_SETIP     2  // Set IP addr.     Skipped if -s argument not given
#define STATE_ERASE     3  // Erase EEPROM.    Skipped if -f argument not given
#define STATE_PROGRAM   4  // Upload rbf file. Skipped if -f argument not given
#define STATE_WAIT      5  // Wait for response from radio
#define STATE_DONE      6  // All done.

int main(int argc, char **argv) {
  int i;
  char *dev = NULL;
  char errbuf[PCAP_ERRBUF_SIZE];
  pcap_t* descr;
  struct pcap_pkthdr hdr;
  const u_char *packet;
  unsigned char mymac[6];  // Mac address of host
  unsigned char myip[4];   // IP  address of host
  unsigned char  hisip[4]; // IP  address to burn into radio
  pcap_if_t *devlist,*ifp;
  pcap_addr_t *addr;
  struct sockaddr *sa;
  struct sockaddr_dl *link;
  char string[256];
  int have_addr;
  int do_display, do_lookup, do_burnip;
  char *rbffile;
  int rbffd;
  int timeout;
  off_t rbflen, rbfxfr, rbfptr;
  int state;
  unsigned char *rbfcontent;  // the whole .rbf file (+ padding)
  char *cp;
  do_display = 1;
  do_burnip = 0;
  do_lookup = 0;
  hisip[0] = 0;
  hisip[1] = 0;
  hisip[2] = 0;
  hisip[3] = 0;
  //
  // Set "bogus" mac addr,
  // if MAC addr cannot be determined neither
  // using AF_LINK or SOICGIFHWADDR.
  //
  mymac[0] = 0x00;
  mymac[1] = 0xC0;
  mymac[2] = 0x11;
  mymac[3] = 0x12;
  mymac[4] = 0x13;
  mymac[5] = 0x14;
  rbffile = NULL;
  i = 0;

  if (argc < 2) {
    printf("Usage: bootloader [-i <iface>]  [-s <addr>] [-f <file>r] \n");
    printf("<iface> is ethernet adapter to use\n");
    printf("<addr>  is ethernet addr to burn into the radio\n");
    printf("<file>  is *.rbf firmware file to load into the radio\n\n");
    printf("The -i, -s, -f options need administrator (root) privileges\n");
    printf("If no arguments are given, the adapters in the system are listed.\n\n");
  }

  while (++i < argc) {
    if (!strcmp(argv[i], "-i") && i + 1 < argc) {
      dev = argv[++i];
      do_display = 0;
      do_lookup = 1;
      continue;
    }

    if (!strcmp(argv[i], "-f") && i + 1 < argc) {
      rbffile = argv[++i];
      continue;
    }

    if (!strcmp(argv[i], "-s") && i + 1 < argc) {
      int i1, i2, i3, i4;
      i++;

      if (sscanf(argv[i], "%d.%d.%d.%d", &i1, &i2, &i3, &i4) < 4) {
        printf("Could not determine IP addr from string %s\n", argv[i]);
      } else {
        hisip[0] = i1 & 0xff;
        hisip[1] = i2 & 0xff;
        hisip[2] = i3 & 0xff;
        hisip[3] = i4 & 0xff;
        do_burnip = 1;
      }

      continue;
    }
  }

  //
  // list devices
  //
  if (pcap_findalldevs(&devlist, errbuf) == -1) {
    printf("Error in pcap_findalldevs_ex: %s\n", errbuf);
    exit(1);
  }

  ifp = devlist;
  have_addr = 0;

  while (ifp != NULL) {
    if (do_lookup && strcmp(ifp->name, dev)) {
      // we are looking for a specific interface, skip all others
      ifp = ifp->next;
      continue;
    }

    if (do_display) {
      have_addr = 0;
    }

    addr = ifp->addresses;

    while (addr != NULL) {
      sa = addr->addr;

      if (sa->sa_family == AF_INET) {
        int i1, i2, i3, i4;
        (void) inet_ntop(AF_INET, (void *) & (((struct sockaddr_in *)sa)->sin_addr), string, 256);

        if (sscanf(string, "%d.%d.%d.%d", &i1, &i2, &i3, &i4) == 4) {
          myip[0] = i1;
          myip[1] = i2;
          myip[2] = i3;
          myip[3] = i4;

          if (myip[0] > 127) { have_addr = 1; }
        }
      }

#ifdef __APPLE__

      //
      // BSD code (e.g. MacOS), need  to include net/if_dl.h
      //
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
      }

#endif
      addr = addr->next;
    }

#ifdef SIOCGIFHWADDR
    //
    // Alternate method to query the MAC address.
    // This works on RaspPi.
    //
    struct ifreq ifr;
    int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    strcpy(ifr.ifr_name, ifp->name);

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
      memcpy(mymac, ifr.ifr_addr.sa_data, 6);
    }

#endif

    if (have_addr) {
      printf("Interface=%-10s MAC=%02x:%02x:%02x:%02x:%02x:%02x Address=%d.%d.%d.%d\n",
             ifp->name,
             mymac[0], mymac[1], mymac[2], mymac[3], mymac[4], mymac[5],
             myip[0], myip[1], myip[2], myip[3]);
    }

    ifp = ifp->next;
  }

  pcap_freealldevs(devlist);

  //
  // If no suitable interface has been specified
  // (or if this is a "display" run), give up.
  //
  if (dev == NULL) { return 0; }  // this was a "display" run

  if (!have_addr) { printf("Interface %s has no IP  address (exiting).\n", dev); }

  if (!have_addr) { return 8; }

  //
  //  Check some assumptions about RBF file
  //
  rbffd = -1;

  if (rbffile != NULL) {
    rbffd = open(rbffile, O_RDONLY);

    if (rbffd < 0) {
      perror("Open RBF file");
    }

    i = strlen(rbffile);

    if (i < 5) {
      rbffd = -1;
    } else {
      if (strcmp(rbffile + i - 4, ".rbf")) {
        printf("Firmware file name does not end in .rbf, ignoring.\n");
        rbffd = -1;
      }
    }

    if (rbffd >= 0) {
      rbflen = lseek(rbffd, 0, SEEK_END);

      if (rbflen < 100000UL) {
        printf("RBF file seems unreasonably short,  ignoring.\n");
        close(rbffd);
        rbffd = -1;
      } else {
        printf("RBF file %s length = %ld bytes\n", rbffile, (long) rbflen);

        if (rbflen % 256 != 0) {
          rbfxfr = 256 * (rbflen / 256 + 1);
        } else {
          rbfxfr = rbflen;
        }

        printf("RBF transfer length = %ld bytes (%d blocks)\n", (long) rbfxfr, (int) (rbfxfr / 256));
      }
    }
  }

  if (do_burnip) {
    printf("Shall burn IP addr = (%d,%d,%d,%d) into radio.\n", hisip[0], hisip[1], hisip[2], hisip[3]);
  }

  if (do_burnip || rbffd >= 0) {
    printf("Is this OK (y/n)?\n");
    i = getchar();

    if (i != 'y' && i != 'Y') { return 0; }
  }

  //
  // Read rbf file into memory
  //
  if (rbffd >= 0) {
    rbfcontent = malloc(rbfxfr);

    if (rbfcontent == NULL) {
      perror("Allocation of memory for RBF file");
      return 0;
    }

    lseek(rbffd, 0, SEEK_SET);

    if (read(rbffd, rbfcontent, rbflen) != rbflen) {
      printf("RBF file read error, terminating\n");
      return 0;
    }

    close(rbffd);

    // pad with 0xFF up to the next multiple of 256
    for (i = rbflen; i < rbfxfr; i++) {
      rbfcontent[i] = 0xff;
    }

    rbfptr = 0;
    printf("RBF file read into memory.\n");
  }

  //
  // From this point on we need admin privileges
  //
  descr = pcap_open_live(dev, 1024, 1, 10, errbuf);

  if (descr == NULL) {
    printf("pcap_open_live(): %s\n", errbuf);
    exit(1);
  }

  //
  // On some systems, pcap_next() hangs when no packet arrives at all
  // To cope with these cases, set "nonblocking" mode and explicitly
  // wait 10 msec before when doing pcap_next
  //
  pcap_setnonblock(descr, 1, errbuf);
  state = STATE_QUERYMAC;
  timeout = 0;

  //
  // We usually use a time-out of 500 msec (that is generous)
  // except when erasing, since this may take MUCH longer
  //
  for (;;) {
    //
    // Perform action associated with the state
    //
    switch (state) {
    case STATE_QUERYMAC:
      sendRawCommand(descr, mymac, READ_METIS_MAC, NULL, 0);
      timeout = 50;
      state = STATE_WAIT;
      break;

    case STATE_QUERYIP:
      sendRawCommand(descr, mymac, READ_METIS_IP, NULL, 0);
      timeout = 50;
      state = STATE_WAIT;
      break;

    case STATE_SETIP:
      sendRawCommand(descr, mymac, WRITE_METIS_IP, hisip, 4);
      printf("New IP address written to board.\n");
      // Since there will be no answer, wait 100 msecs before proceeding
      state = STATE_DONE;

      if (rbffd >= 0) {
        state = STATE_ERASE;
      }

      break;

    case STATE_ERASE:
      sendRawCommand(descr, mymac, ERASE_METIS_FLASH, NULL, 0);
      printf("Erasing (THIS TAKES SOME TIME!)\n");
      timeout = 18000; // up to 3 minutes
      state = STATE_WAIT;
      break;

    case STATE_PROGRAM:
      sendRawData(descr, mymac, rbfcontent, rbfptr);
      rbfptr += 256;

      if (rbfptr % 1024 == 0) {
        printf("Data sent: %6ld k-Bytes\r", (long) rbfptr / 1024);
        fflush(stdout);
      }

      timeout = 50;
      state = STATE_WAIT;
      break;

    case STATE_WAIT:
      //
      // Terminate program if timeout is reached.
      // The "wait" state is changed if a valid response
      // packet is captured.
      //
      timeout--;

      if (timeout == 0) {
        printf("TIMEOUT reached - terminating bootloader.\n");
        return (0);
      }

      break;

    case STATE_DONE:
      printf("All Done.\n");
      return (0);
    }

    //
    // Obtain next packet. If it is already there, take it.
    // Otherwise retry after 10 milli seconds. This is why
    // all the above time outs are in units of 10 ms.
    //
    packet = pcap_next(descr, &hdr);

    if (packet == NULL) {
      usleep(10000);
      packet = pcap_next(descr, &hdr);
    }

    if (packet == NULL) { continue; } // Nothing arrived within 10 msec

    /*
     A packet has arrived, but this could be anything.
     So check whether
       - it has mymac               as the "to"   address
       - it has 11:22:33:44:55:66   as the "from" address
       - it has 0xEF 0xFE 0x03      as boot loader header

       and only then process the contents (otherwise get next packet)
     */
    if (hdr.len > 22
    && packet[ 0] == mymac[0] && packet[ 1] == mymac[1] && packet[ 2] == mymac[2]
    && packet[ 3] == mymac[3] && packet[ 4] == mymac[4] && packet[ 5] == mymac[5]
    && packet[ 6] == 0x11     && packet[ 7] == 0x22     && packet[ 8] == 0x33
    && packet[ 9] == 0x44     && packet[10] == 0x55     && packet[11] == 0x66
    && packet[12] == 0xef     && packet[13] == 0xfe     && packet[14] == 0x03) {
      switch (packet[15]) {
      case HAVE_MAC_ADDRESS:
        printf("HPSDR board detected, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n", packet[16], packet[17], packet[18], packet[19],
               packet[20], packet[21]);
        state = STATE_QUERYIP;
        break;

      case HAVE_IP_ADDRESS:
        printf("Board has IP addr (%d,%d,%d,%d).\n", packet[16], packet[17], packet[18], packet[19]);
        state = STATE_DONE;

        if (rbffd >= 0) { state = STATE_ERASE; }   // if there is something to upload

        if (do_burnip) { state = STATE_SETIP; } // if we want to set an IP address

        break;

      case ERASE_DONE:
        if (rbffd >= 0) {
          printf("Board erased, about to upload new RBF file.\n");
          state = STATE_PROGRAM;
        } else {
          // this cannot happen
          state = STATE_DONE;
        }

        break;

      case SEND_MORE:
        if (rbffd < 0 || rbfptr > rbfxfr) {
          printf("\nRBF file upload complete.\n");
          state = STATE_DONE;
        } else {
          state = STATE_PROGRAM;
        }

        break;

      default:
        printf("UNKNOWN REPLY=%d\n", packet[15]);
        break;
      }
    }
  }
}

//
// Send a 62-byte buffer to the radio containing a "command"
//
void sendRawCommand(pcap_t *handle, unsigned char hw[6], unsigned char command, unsigned char *data, int datalen) {
  unsigned char buffer[62];
  int i;
  /*set the frame header*/
  buffer[0] = 0x11; // use "bogus" radio MAC address for destination
  buffer[1] = 0x22;
  buffer[2] = 0x33;
  buffer[3] = 0x44;
  buffer[4] = 0x55;
  buffer[5] = 0x66;
  buffer[6] = hw[0]; // use own MAC address as source
  buffer[7] = hw[1];
  buffer[8] = hw[2];
  buffer[9] = hw[3];
  buffer[10] = hw[4];
  buffer[11] = hw[5];
  buffer[12] = 0xEF; // protocol
  buffer[13] = 0xFE;
  buffer[14] = 0x03;
  buffer[15] = command;

  /*fill the frame with 0x00*/
  for (i = 0; i < 46; i++) {
    buffer[i + 16] = (unsigned char)0x00;
  }

  /* possibly fill in data */
  for (i = 0; i < datalen; i++) {
    buffer[i + 16] = data[i];
  }

  if (pcap_sendpacket(handle, buffer, 62) != 0) {
    printf("Send Raw failed, command=%d\n", command);
  }
}

//
// Send a 276-byte buffer to the radio that contains data from the rbf file
//
void sendRawData(pcap_t *handle, unsigned char hw[6], unsigned char *data, off_t ptr) {
  unsigned char buffer[276];
  /*set the frame header*/
  buffer[0] = 0x11; // use "bogus" MAC address of radio for destination
  buffer[1] = 0x22;
  buffer[2] = 0x33;
  buffer[3] = 0x44;
  buffer[4] = 0x55;
  buffer[5] = 0x66;
  buffer[6] = hw[0]; // use "own" MAC address as source
  buffer[7] = hw[1];
  buffer[8] = hw[2];
  buffer[9] = hw[3];
  buffer[10] = hw[4];
  buffer[11] = hw[5];
  buffer[12] = 0xEF; // protocol
  buffer[13] = 0xFE;
  buffer[14] = 0x03;
  buffer[15] = PROGRAM_METIS_FLASH;
  buffer[16] = 0x00;
  buffer[17] = 0x00;
  buffer[18] = 0x00;
  buffer[19] = 0x00;

  for (int i = 0; i < 256; i++) {
    buffer[i + 20] = data[i + ptr];
  }

  if (pcap_sendpacket(handle, buffer, 276) != 0) {
    printf("Send data block failed!\n");
  }
}
