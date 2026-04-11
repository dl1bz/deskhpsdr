/* Copyright (C)
*
*   2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
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

#include "nw_toolset.h"

#include <string.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#ifdef __APPLE__
  #include <net/if_types.h>
  #include <net/if_dl.h>
#endif

int nw_is_wired(const char *remote_ip) {
  int sock = -1;
  struct sockaddr_in remote;
  struct sockaddr_in local;
  socklen_t len;
  int result = -1;

  if (remote_ip == NULL) {
    return -1;
  }

#ifdef __APPLE__
  memset(&remote, 0, sizeof(remote));
  memset(&local, 0, sizeof(local));
  sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
    return -1;
  }

  remote.sin_family = AF_INET;
  remote.sin_port = htons(1025);

  if (inet_pton(AF_INET, remote_ip, &remote.sin_addr) != 1) {
    close(sock);
    return -1;
  }

  if (connect(sock, (struct sockaddr *)&remote, sizeof(remote)) < 0) {
    close(sock);
    return -1;
  }

  len = sizeof(local);

  if (getsockname(sock, (struct sockaddr *)&local, &len) < 0) {
    close(sock);
    return -1;
  }

  close(sock);
  struct ifaddrs *ifaddr = NULL;
  struct ifaddrs *ifa;
  struct ifaddrs *ifa2;

  if (getifaddrs(&ifaddr) != 0) {
    return -1;
  }

  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    struct sockaddr_in ifa_addr;

    if (ifa->ifa_addr == NULL) {
      continue;
    }

    if (ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }

    memcpy(&ifa_addr, ifa->ifa_addr, sizeof(ifa_addr));

    if (ifa_addr.sin_addr.s_addr != local.sin_addr.s_addr) {
      continue;
    }

    for (ifa2 = ifaddr; ifa2 != NULL; ifa2 = ifa2->ifa_next) {
      struct sockaddr_dl sdl;

      if (ifa2->ifa_addr == NULL) {
        continue;
      }

      if (strcmp(ifa->ifa_name, ifa2->ifa_name) != 0) {
        continue;
      }

      if (ifa2->ifa_addr->sa_family != AF_LINK) {
        continue;
      }

      memcpy(&sdl, ifa2->ifa_addr, sizeof(sdl));

      if (sdl.sdl_type == IFT_ETHER) {
        result = 1;
      } else {
        result = 0;
      }

      break;
    }

    break;
  }

  freeifaddrs(ifaddr);
  return result;
#else
  (void)remote_ip;
  return -1;
#endif
}
