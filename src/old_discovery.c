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

#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

#ifdef __linux__
  #include <unistd.h>
#endif

#ifdef __APPLE__
  #include <sys/types.h>
  #include <sys/sysctl.h>
#endif

#include "discovered.h"
#include "discovery.h"
#include "old_discovery.h"
#include "stemlab_discovery.h"
#include "message.h"

static char interface_name[64];
static struct sockaddr_in interface_addr = {0};
static struct sockaddr_in interface_netmask = {0};

#define DISCOVERY_PORT 1024
static int discovery_socket;

static GThread *discover_thread_id;
static gpointer discover_receive_thread(gpointer data);

//
// discflag = 1:   discover by sending UDP broadcast packet
// discflag = 2:   discover by sending UDP backet to Radio IP address
// discflag = 3:   discover by connecting via TCP
//
static void discover(struct ifaddrs* iface, int discflag) {
  int rc;
  struct sockaddr_in *sa = (struct sockaddr_in *)&interface_addr;
  struct sockaddr_in *mask = (struct sockaddr_in *)&interface_netmask;
  char addr[16];
  char net_mask[16];
  struct sockaddr_in to_addr = {0};
  int flags;
  struct timeval tv;
  int optval;
  socklen_t optlen;
  fd_set fds;
  unsigned char buffer[1032];
  int i, len;

  switch (discflag) {
  case 1:
    //
    // Send METIS discovery packet to broadcast address on interface iface
    //
    g_strlcpy(interface_name, iface->ifa_name, sizeof(interface_name));
    t_print("discover: looking for HPSDR devices on %s\n", interface_name);
    // send a broadcast to locate hpsdr boards on the network
    discovery_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (discovery_socket < 0) {
      t_perror("discover: create socket failed for discovery_socket:");
      return;
    }

    if (iface->ifa_addr->sa_family == AF_INET) {
      memcpy(&interface_addr, iface->ifa_addr, sizeof(interface_addr));
      memcpy(&interface_netmask, iface->ifa_netmask, sizeof(interface_netmask));
    }

    // bind to this interface and the discovery port
    interface_addr.sin_family = AF_INET;
    interface_addr.sin_port = htons(0); // system assigned port

    if (bind(discovery_socket, (struct sockaddr * )&interface_addr, sizeof(interface_addr)) < 0) {
      t_perror("discover: bind socket failed for discovery_socket:");
      close (discovery_socket);
      return;
    }

    g_strlcpy(addr, inet_ntoa(sa->sin_addr), sizeof(addr));
    g_strlcpy(net_mask, inet_ntoa(mask->sin_addr), sizeof(net_mask));
    t_print("%s: bound to interface %s address %s mask %s\n", __FUNCTION__, interface_name, addr, net_mask);
    // allow broadcast on the socket
    int on = 1;
    rc = setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

    if (rc != 0) {
      t_print("discover: cannot set SO_BROADCAST: rc=%d\n", rc);
      close (discovery_socket);
      return;
    }

    // setup to address
    to_addr.sin_family = AF_INET;
    to_addr.sin_port = htons(DISCOVERY_PORT);
    to_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    //
    // This will use the subnet-specific broadcast address
    // instead of 255.255.255.255
    //
    //  to_addr.sin_addr.s_addr = htonl(ntohl(interface_addr.sin_addr.s_addr)
    //          | (ntohl(interface_netmask.sin_addr.s_addr) ^ 0xFFFFFFFF));
    //
#ifdef __APPLE__

    //
    // MacOS fails for broadcasts to the loopback interface(s).
    // so if this is a loopback, simply use the loopback addr
    //
    if ((iface->ifa_flags & IFF_LOOPBACK) == IFF_LOOPBACK) {
      //
      // No broadcast on loopback interfaces. Send UDP packet
      // to interface address
      //
      to_addr.sin_addr = interface_addr.sin_addr;
    }

#endif
    break;

  case 2:
    //
    // Send METIS detection packet via UDP to ipaddr_radio
    // To be able to connect later, we have to specify INADDR_ANY
    //
    interface_addr.sin_family = AF_INET;
    interface_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    memset(&to_addr, 0, sizeof(to_addr));
    to_addr.sin_family = AF_INET;
    to_addr.sin_port = htons(DISCOVERY_PORT);

    if (inet_aton(ipaddr_radio, &to_addr.sin_addr) == 0) {
      return;
    }

    t_print("discover: looking for HPSDR device with IP %s\n", ipaddr_radio);
    discovery_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (discovery_socket < 0) {
      t_perror("discover: create socket failed for discovery_socket:");
      return;
    }

    break;

  case 3:
    //
    // Send METIS detection packet via TCP to ipaddr_radio
    // This is rather tricky, one must avoid "hanging" when the
    // connection does not succeed.
    //
    memset(&to_addr, 0, sizeof(to_addr));
    to_addr.sin_family = AF_INET;
    to_addr.sin_port = htons(DISCOVERY_PORT);

    if (inet_aton(ipaddr_radio, &to_addr.sin_addr) == 0) {
      return;
    }

    t_print("Trying to detect via TCP with IP %s\n", ipaddr_radio);
    discovery_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (discovery_socket < 0) {
      t_perror("discover: create socket failed for TCP discovery_socket\n");
      return;
    }

    //
    // Here I tried a bullet-proof approach to connect() such that the program
    // does not "hang" under any circumstances.
    // - First, one makes the socket non-blocking. Then, the connect() will
    //   immediately return with error EINPROGRESS.
    // - Then, one uses select() to look for *writeability* and check
    //   the socket error if everything went right. Since one calls select()
    //   with a time-out, one either succeed within this time or gives up.
    // - Do not forget to make the socket blocking again.
    //
    // Step 1. Make socket non-blocking and connect()
    flags = fcntl(discovery_socket, F_GETFL, 0);
    fcntl(discovery_socket, F_SETFL, flags | O_NONBLOCK);
    rc = connect(discovery_socket, (const struct sockaddr *)&to_addr, sizeof(to_addr));

    if ((rc < 0) && (errno != EINPROGRESS)) {
      t_perror("discover: connect() failed for TCP discovery_socket:");
      close(discovery_socket);
      return;
    }

    // Step 2. Use select to wait for the connection
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(discovery_socket, &fds);
    rc = select(discovery_socket + 1, NULL, &fds, NULL, &tv);

    if (rc < 0) {
      t_perror("discover: select() failed on TCP discovery_socket:");
      close(discovery_socket);
      return;
    }

    // If no connection occured, return
    if (rc == 0) {
      // select timed out
      t_print("discover: select() timed out on TCP discovery socket\n");
      close(discovery_socket);
      return;
    }

    // Step 3. select() succeeded. Check success of connect()
    optlen = sizeof(int);
    rc = getsockopt(discovery_socket, SOL_SOCKET, SO_ERROR, &optval, &optlen);

    if (rc < 0) {
      // this should very rarely happen
      t_perror("discover: getsockopt() failed on TCP discovery_socket:");
      close(discovery_socket);
      return;
    }

    if (optval != 0) {
      // connect did not succeed
      t_print("discover: connect() on TCP socket did not succeed\n");
      close(discovery_socket);
      return;
    }

    // Step 4. reset the socket to normal (blocking) mode
    fcntl(discovery_socket, F_SETFL, flags &  ~O_NONBLOCK);
    break;

  default:
    return;
    break;
  }

  optval = 1;
  setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
  rc = devices;
  // start a receive thread to collect discovery response packets
  discover_thread_id = g_thread_new( "old discover receive", discover_receive_thread, NULL);

  // send discovery packet
  // If this is a TCP connection, send a "long" packet
  switch (discflag) {
  case 1:
  case 2:
    len = 63; // send UDP packet
    break;

  case 3:
    len = 1032; // send TCP packet
    break;
  }

  buffer[0] = 0xEF;
  buffer[1] = 0xFE;
  buffer[2] = 0x02;

  for (i = 3; i < len; i++) {
    buffer[i] = 0x00;
  }

#if defined (__APPLE__) && defined (__TAHOEFIX__)
  t_print("%s: execute TAHOE hotfix\n", __FUNCTION__);

  //-- start fix for Tahoe --
  // Send discovery packet 3x to mitigate macOS first-UDP-drop
  for (int n = 0; n < 3; n++) {
    if (sendto(discovery_socket, buffer, len, 0, (struct sockaddr * )&to_addr, sizeof(to_addr)) < 0) {
      t_perror("discover: sendto socket failed for discovery_socket:");
      close(discovery_socket);
      return;
    }

    usleep(30000); // 30 ms
  }

  //-- end fix for Tahoe --
#else

  if (sendto(discovery_socket, buffer, len, 0, (struct sockaddr * )&to_addr, sizeof(to_addr)) < 0) {
    t_perror("discover: sendto socket failed for discovery_socket:");
    close (discovery_socket);
    return;
  }

#endif
  // wait for receive thread to complete
  g_thread_join(discover_thread_id);
  close(discovery_socket);

  switch (discflag) {
  case 1:
    t_print("discover: exiting discover for %s\n", iface->ifa_name);
    break;

  case 2:
    t_print("discover: exiting HPSDR discover for IP %s\n", ipaddr_radio);

    if (devices == rc + 1) {
      //
      // METIS detection UDP packet sent to fixed IP address got a valid response.
      //
      memcpy((void *)&discovered[rc].info.network.address, (void *)&to_addr, sizeof(to_addr));
      discovered[rc].info.network.address_length = sizeof(to_addr);
      g_strlcpy(discovered[rc].info.network.interface_name, "UDP", sizeof(discovered[rc].info.network.interface_name));
      discovered[rc].use_routing = 1;
    }

    break;

  case 3:
    t_print("discover: exiting TCP discover for IP %s\n", ipaddr_radio);

    if (devices == rc + 1) {
      //
      // METIS detection TCP packet sent to fixed IP address got a valid response.
      // Patch the IP addr into the device field
      // and set the "use TCP" flag.
      //
      memcpy((void*)&discovered[rc].info.network.address, (void*)&to_addr, sizeof(to_addr));
      discovered[rc].info.network.address_length = sizeof(to_addr);
      g_strlcpy(discovered[rc].info.network.interface_name, "TCP", sizeof(discovered[rc].info.network.interface_name));
      discovered[rc].use_routing = 1;
      discovered[rc].use_tcp = 1;
    }

    break;
  }
}

static gpointer discover_receive_thread(gpointer data) {
  struct sockaddr_in addr;
  socklen_t len;
  unsigned char buffer[2048];
  struct timeval tv;
  int i;
  t_print("discover_receive_thread\n");
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));
  len = sizeof(addr);

  while (1) {
    int bytes_read = recvfrom(discovery_socket, buffer, sizeof(buffer), 1032, (struct sockaddr*)&addr, &len);

    if (bytes_read < 0) {
      t_print("discovery: bytes read %d\n", bytes_read);
      t_perror("old_discovery: recvfrom socket failed for discover_receive_thread");
      break;
    }

    if (bytes_read == 0) { break; }

    t_print("old_discovery: received %d bytes\n", bytes_read);

    if ((buffer[0] & 0xFF) == 0xEF && (buffer[1] & 0xFF) == 0xFE) {
      int status = buffer[2] & 0xFF;

      if (status == 2 || status == 3) {
#if defined (__APPLE__) && defined (__TAHOEFIX__)
        t_print("%s: execute TAHOE hotfix\n", __FUNCTION__);
        // -- start fix for Tahoe: de-duplicate discovery responses by MAC --
        unsigned char mac_tmp[6];

        for (i = 0; i < 6; i++) { mac_tmp[i] = buffer[i + 3]; }

        int duplicate = 0;

        for (int d = 0; d < devices; d++) {
          if (discovered[d].protocol == ORIGINAL_PROTOCOL &&
              memcmp(discovered[d].info.network.mac_address, mac_tmp, 6) == 0) {
            // update last-seen address and skip creating a duplicate
            memcpy(&discovered[d].info.network.address, &addr, sizeof(addr));
            discovered[d].info.network.address_length = sizeof(addr);
            duplicate = 1;
            break;
          }
        }

        if (duplicate) { continue; } // skip adding a second time

        // -- end fix for Tahoe --
#endif

        if (devices < MAX_DEVICES) {
          discovered[devices].protocol = ORIGINAL_PROTOCOL;
          discovered[devices].device = buffer[10] & 0xFF;
          discovered[devices].software_version = buffer[9] & 0xFF;

          switch (discovered[devices].device) {
          case DEVICE_METIS:
            g_strlcpy(discovered[devices].name, "Metis", sizeof(discovered[devices].name));
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_HERMES:
            g_strlcpy(discovered[devices].name, "Hermes", sizeof(discovered[devices].name));
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_GRIFFIN:
            g_strlcpy(discovered[devices].name, "Griffin", sizeof(discovered[devices].name));
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_ANGELIA:
            g_strlcpy(discovered[devices].name, "Angelia", sizeof(discovered[devices].name));
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_ORION:
            g_strlcpy(discovered[devices].name, "Orion", sizeof(discovered[devices].name));
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_HERMES_LITE:
            //
            // HermesLite V2 boards use
            // DEVICE_HERMES_LITE as the ID and a software version
            // that is larger or equal to 40, while the original
            // (V1) HermesLite boards have software versions up to 31.
            // Furthermode, HL2 uses a minor version in buffer[21]
            // so the official version number e.g. 73.2 stems from buf9=73 and buf21=2
            //
            discovered[devices].software_version = 10 * (buffer[9] & 0xFF) + (buffer[21] & 0xFF);

            if (discovered[devices].software_version < 400) {
              g_strlcpy(discovered[devices].name, "HermesLite V1", sizeof(discovered[devices].name));
            } else {
              g_strlcpy(discovered[devices].name, "HermesLite V2", sizeof(discovered[devices].name));
              discovered[devices].device = DEVICE_HERMES_LITE2;
              // t_print("discovered HL2: Gateware Major Version=%d Minor Version=%d\n", buffer[9], buffer[21]);
              t_print("==> HL2: Gateware Major Version=%d Minor Version=%d\n", buffer[9], buffer[21]);

              if (buffer[11] & 0xA0) {
                t_print("==> HL2: fixed IP %d.%d.%d.%d (DHCP overrides)\n", buffer[13], buffer[14], buffer[15], buffer[16]);
              } else if (buffer[11] & 0x80) {
                t_print("==> HL2: fixed IP %d.%d.%d.%d (DHCP ignored)\n", buffer[13], buffer[14], buffer[15], buffer[16]);
              }

              if (buffer[11] & 0x40) {
                t_print("==> HL2 MAC addr modified: <...>:%02x:%02x\n", buffer[17], buffer[18]);
              }
            }

            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 38400000.0;
            break;

          case DEVICE_ORION2:
            g_strlcpy(discovered[devices].name, "Orion2", sizeof(discovered[devices].name));
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_STEMLAB:
            // This is in principle the same as HERMES but has two ADCs
            // (and therefore, can do DIVERSITY).
            // There are some problems with the 6m band on the RedPitaya
            // but with additional filtering it can be used.
            g_strlcpy(discovered[devices].name, "STEMlab", sizeof(discovered[devices].name));
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_STEMLAB_Z20:
            // This is in principle the same as HERMES but has two ADCs
            // (and therefore, can do DIVERSITY).
            // There are some problems with the 6m band on the RedPitaya
            // but with additional filtering it can be used.
            g_strlcpy(discovered[devices].name, "STEMlab-Zync7020", sizeof(discovered[devices].name));
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          default:
            g_strlcpy(discovered[devices].name, "Unknown", sizeof(discovered[devices].name));
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;
          }

          for (i = 0; i < 6; i++) {
            discovered[devices].info.network.mac_address[i] = buffer[i + 3];
          }

          discovered[devices].status = status;
          memcpy((void*)&discovered[devices].info.network.address, (void*)&addr, sizeof(addr));
          discovered[devices].info.network.address_length = sizeof(addr);
          memcpy((void*)&discovered[devices].info.network.interface_address, (void*)&interface_addr, sizeof(interface_addr));
          memcpy((void*)&discovered[devices].info.network.interface_netmask, (void*)&interface_netmask,
                 sizeof(interface_netmask));
          discovered[devices].info.network.interface_length = sizeof(interface_addr);
          g_strlcpy(discovered[devices].info.network.interface_name, interface_name,
                    sizeof(discovered[devices].info.network.interface_name));
          discovered[devices].use_tcp = 0;
          discovered[devices].use_routing = 0;
          discovered[devices].supported_receivers = 2;
          t_print("%s: device=%d name=%s software_version=%d status=%d\n",
                  __FUNCTION__,
                  discovered[devices].device,
                  discovered[devices].name,
                  discovered[devices].software_version,
                  discovered[devices].status);
          t_print("%s: address=%s (%02X:%02X:%02X:%02X:%02X:%02X) on %s min=%0.3f MHz max=%0.3f MHz\n",
                  __FUNCTION__,
                  inet_ntoa(discovered[devices].info.network.address.sin_addr),
                  discovered[devices].info.network.mac_address[0],
                  discovered[devices].info.network.mac_address[1],
                  discovered[devices].info.network.mac_address[2],
                  discovered[devices].info.network.mac_address[3],
                  discovered[devices].info.network.mac_address[4],
                  discovered[devices].info.network.mac_address[5],
                  discovered[devices].info.network.interface_name,
                  discovered[devices].frequency_min * 1E-6,
                  discovered[devices].frequency_max * 1E-6);
          devices++;
        }
      }
    }
  }

  t_print("discovery: exiting discover_receive_thread\n");
  g_thread_exit(NULL);
  return NULL;
}

// Funktion zum Überprüfen, ob es ein Raspberry Pi ist
static int is_raspberry_pi_linux() {
  FILE *fp = fopen("/proc/cpuinfo", "r");

  if (fp == NULL) {
    return 0; // Fehler beim Öffnen der Datei
  }

  char line[256];

  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "Model", 5) == 0) {
      if (strstr(line, "Raspberry Pi")) {
        fclose(fp);
        return 1; // Raspberry Pi gefunden
      }
    }
  }

  fclose(fp);
  return 0; // Kein Raspberry Pi gefunden
}

// Funktion zum Überprüfen, ob es ein macOS-System ist
static int is_macos() {
#ifdef __APPLE__
  // Wir können sysctl verwenden, um die Hardware zu überprüfen
  size_t len = 0;
  char *model = NULL;

  if (sysctlbyname("hw.model", NULL, &len, NULL, 0) == 0) {
    model = (char*)malloc(len);

    if (model != NULL) {
      if (sysctlbyname("hw.model", model, &len, NULL, 0) == 0) {
        if (strstr(model, "MacBook") || strstr(model, "iMac") || strstr(model, "Mac mini")) {
          free(model);
          return 1; // macOS erkannt
        }
      }

      free(model);
    }
  }

#endif
  return 0; // Kein macOS erkannt
}

void old_discovery() {
  struct ifaddrs *addrs, *ifa;
  int i, is_local;
  int ist_macos, ist_raspi;
  t_print("old_discovery\n");

  //
  // In the second phase of the STEMlab (RedPitaya) discovery,
  // we know that it can be reached by a specific IP address
  // and need no discovery any more
  //
  if (!discover_only_stemlab) {
    getifaddrs(&addrs);
    ifa = addrs;

    while (ifa) {
      g_main_context_iteration(NULL, 0);

      //
      // Sometimes there are many (virtual) interfaces, and some
      // of them are very unlikely to offer a radio connection.
      // These are skipped.
      // Note the "loopback" interfaces are checked:
      // the RadioBerry for example, is handled by a driver
      // which connects to HPSDR software via a loopback interface.
      //
      if (ifa->ifa_addr) {
        if (
          ifa->ifa_addr->sa_family == AF_INET
#ifdef __APPLE__
          && (ifa->ifa_flags & IFF_LOOPBACK) != IFF_LOOPBACK
#endif
          && (ifa->ifa_flags & IFF_UP) == IFF_UP
          && (ifa->ifa_flags & IFF_RUNNING) == IFF_RUNNING
#ifndef __APPLE__
          && strncmp("veth", ifa->ifa_name, 4)
          && strncmp("dock", ifa->ifa_name, 4)
          && strncmp("hass", ifa->ifa_name, 4)
#endif
        ) {
          discover(ifa, 1);   // send UDP broadcast packet to interface
        }
      }

      ifa = ifa->ifa_next;
    }

    freeifaddrs(addrs);
  }

  //
  // If an IP address has already been "discovered" via a
  // METIS broadcast package, it makes no sense to re-discover
  // it via a routed UDP packet.
  //
  is_local = 0;

  for (i = 0; i < devices; i++) {
    if (!strncmp(inet_ntoa(discovered[i].info.network.address.sin_addr), ipaddr_radio, 20)
        && discovered[i].protocol == ORIGINAL_PROTOCOL) {
      is_local = 1;
    }
  }

  if (!is_local) { discover(NULL, 2); }

  discover(NULL, 3);
  t_print( "discovery found %d devices\n", devices);

  for (i = 0; i < devices; i++) {
    t_print("discovery: found device=%d software_version=%d status=%d address=%s (%02X:%02X:%02X:%02X:%02X:%02X) on %s\n",
            discovered[i].device,
            discovered[i].software_version,
            discovered[i].status,
            inet_ntoa(discovered[i].info.network.address.sin_addr),
            discovered[i].info.network.mac_address[0],
            discovered[i].info.network.mac_address[1],
            discovered[i].info.network.mac_address[2],
            discovered[i].info.network.mac_address[3],
            discovered[i].info.network.mac_address[4],
            discovered[i].info.network.mac_address[5],
            discovered[i].info.network.interface_name);
  }

  ist_macos = is_macos() ? 1 : 0;
  ist_raspi = is_raspberry_pi_linux() ? 1 : 0;
  t_print("%s: macOS = %d Raspberry Pi = %d Lokal = %d\n", __FUNCTION__, ist_macos, ist_raspi, is_local);
}
