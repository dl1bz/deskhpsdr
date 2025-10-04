/* Copyright (C)
*
*   2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
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
*   hl2_eeprom_discovery.c
*
*   Build: gcc -std=c11 -Wall -O2 -D_DEFAULT_SOURCE hl2_eeprom_discovery.c -o hl2_eeprom_discovery
*
*/
#ifndef _DEFAULT_SOURCE
  #define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>
#ifndef __APPLE__
  #include <sys/time.h>     // struct timeval
  #include <sys/socket.h>   // setsockopt, SOL_SOCKET
#endif

static void mac(const uint8_t *m) { printf("%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]); }

static const char* ifname_for_src(struct in_addr src) {
  static char name[IF_NAMESIZE] = "n/a";
  struct ifaddrs *ifs = NULL,*i = NULL;

  if (getifaddrs(&ifs) != 0) { return name; }

  for (i = ifs; i; i = i->ifa_next) {
    if (!i->ifa_addr || !i->ifa_netmask) { continue; }

    if (i->ifa_addr->sa_family != AF_INET) { continue; }

    unsigned f = i->ifa_flags;

    if (!(f & IFF_UP)) { continue; }

    if (f & IFF_LOOPBACK) { continue; }

    struct sockaddr_in *a = (struct sockaddr_in * )i->ifa_addr;

    struct sockaddr_in *m = (struct sockaddr_in*)i->ifa_netmask;

    if ( ((a->sin_addr.s_addr & m->sin_addr.s_addr) ==
          (src.s_addr         & m->sin_addr.s_addr)) ) {
      strncpy(name, i->ifa_name, sizeof(name) -1);
      name[sizeof(name) -1] = '\0';
      break;
    }
  }

  freeifaddrs(ifs);
  return name;
}

static int send_discovery_all_if(int fd, const uint8_t *q, size_t qlen, uint16_t port) {
  struct ifaddrs *ifa_list = NULL, *ifa = NULL;

  if (getifaddrs(&ifa_list) != 0) { perror("getifaddrs"); return -1; }

  int sent = 0;

  for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr) { continue; }

    if (ifa->ifa_addr->sa_family != AF_INET) { continue; }

    unsigned flags = ifa->ifa_flags;

    if (!(flags & IFF_UP)) { continue; }

    if (flags & IFF_LOOPBACK || strncmp(ifa->ifa_name, "lo", 2) == 0) { continue; }

    if (!(flags & IFF_BROADCAST)) { continue; }

    struct sockaddr_in *b = (struct sockaddr_in * )ifa->ifa_broadaddr;

    if (!b) { continue; }

    if (b->sin_addr.s_addr == 0 || b->sin_addr.s_addr == htonl(INADDR_BROADCAST)) { continue; }

    static uint32_t seen[128];
    static int nseen = 0;
    int dup = 0;

    for (int i = 0; i < nseen; i++) if (seen[i] == b->sin_addr.s_addr) { dup = 1; break; }

    if (dup) { continue; }
    else if (nseen < 128) { seen[nseen++] = b->sin_addr.s_addr; }

    struct sockaddr_in dst = *b;          // Broadcast-Adresse des Interfaces

    dst.sin_port = htons(port);
    char bcast[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dst.sin_addr, bcast, sizeof(bcast));
    printf("Send discovery packet via interface %-8s → %s:%u\n", ifa->ifa_name, bcast, port);
    fflush(stdout);
    ssize_t rc = sendto(fd, q, qlen, 0, (struct sockaddr*)&dst, sizeof(dst));

    if (rc == (ssize_t)qlen) { sent++; }
    else { perror("sendto(broadcast-if)"); }
  }

  freeifaddrs(ifa_list);
  return sent;
}

int main(void) {
  uint8_t q[63] = {0};
  q[0] = 0xEF;
  q[1] = 0xFE;
  q[2] = 0x02;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  if (fd < 0) {perror("socket"); return 1;}

  int rcv = 256 * 1024;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv));
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
#ifndef __APPLE__
  struct timeval tv;
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#else
  struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));  // nur einmal
  struct sockaddr_in any = {0};
  any.sin_family = AF_INET;
  any.sin_port = 0;
  any.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(fd, (struct sockaddr * )&any, sizeof(any)) < 0) {perror("bind"); return 1;}

  if (send_discovery_all_if(fd, q, sizeof(q), 1024) <= 0) {
    fprintf(stderr, "Notice: no Broadcast sent (no usable interfaces found).\n");
  }

  int found = 0;

  for (;;) {
    uint8_t buf[512];
    struct sockaddr_in src;
    socklen_t sl = sizeof(src);
    ssize_t r = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr*)&src, &sl);

    if (r < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK) break; perror("recvfrom"); break; }

    if (src.sin_port != htons(1024)) { continue; } // nur erwarteten Quell-Port akzeptieren

    if (r < 17) { continue; }

    const char* ifname = ifname_for_src(src.sin_addr);

    if (buf[0] != 0xEF || buf[1] != 0xFE || buf[2] < 0x02 || buf[2] > 0x03) { continue; } // METIS reply

    // HL2-spezifische Erweiterung:
    // [0x03..0x08] MAC, [0x09] Gateware Major, [0x0A] BoardID(0x06),
    // [0x0B] EEPROM 0x06 (Config Flags), [0x0C] EEPROM 0x07 (Reserved),
    // [0x0D..0x10] EEPROM 0x08..0x0B (Fixed IP W.X.Y.Z)
    uint8_t *macp = &buf[3];
    uint8_t gwmaj = buf[9], board = buf[10];
    uint8_t e6 = buf[11], e7 = buf[12], ipW = buf[13], ipX = buf[14], ipY = buf[15], ipZ = buf[16];
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src.sin_addr, ipstr, sizeof ipstr);
    printf("Got answer via interface %s  ←  %s:1024\n", ifname, ipstr);
    printf("HL2 #%d @ %s\n", ++found, ipstr);
    printf("  MAC: ");
    mac(macp);
    printf("\n");
    printf("  Gateware: %u  BoardID: 0x%02X\n", gwmaj, board);
    printf("  EEPROM[0x06] Flags: 0x%02X  (ValidIP=%u, ValidMAC=%u, DHCPfav=%u)\n",
           e6, !!(e6 & 0x80), !!(e6 & 0x40), !!(e6 & 0x20));
    printf("  EEPROM[0x07] Reserved: 0x%02X\n", e7);
    printf("  EEPROM Fixed IP (0x08..0x0B): %u.%u.%u.%u\n", ipW, ipX, ipY, ipZ);
  }

  if (found == 0) { fprintf(stderr, "No HL2 found.\n"); }

  close(fd);
  return found ? 0 : 2;
}
