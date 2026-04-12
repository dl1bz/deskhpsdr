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
  #include <sys/ioctl.h>
  #include <net/if_media.h>
#endif

NW_SETTINGS nw_settings = {
  .is_wired = 1
};

int nw_get_ifname_for_remote_ip(const char *remote_ip, char *ifname, size_t ifname_len) {
  int sock = -1;
  struct sockaddr_in remote;
  struct sockaddr_in local;
  socklen_t len;
  struct ifaddrs *ifaddr = NULL;
  struct ifaddrs *ifa;
  int rc = -1;

  if (remote_ip == NULL || ifname == NULL || ifname_len == 0) {
    return -1;
  }

  memset(&remote, 0, sizeof(remote));
  memset(&local, 0, sizeof(local));
  ifname[0] = '\0';
  sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
    return -1;
  }

  remote.sin_family = AF_INET;
  remote.sin_port = htons(1025);  // nur für Routenwahl

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
  sock = -1;

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

    if (ifa->ifa_name == NULL || ifa->ifa_name[0] == '\0') {
      continue;
    }

    strncpy(ifname, ifa->ifa_name, ifname_len - 1);
    ifname[ifname_len - 1] = '\0';
    rc = 0;
    break;
  }

  freeifaddrs(ifaddr);
  return rc;
}

#ifdef __APPLE__
static int nw_is_wired_macos(const char *remote_ip) {
  char ifname[IFNAMSIZ];
  struct ifmediareq ifmr;
  int sock;
  int type;

  if (nw_get_ifname_for_remote_ip(remote_ip, ifname, sizeof(ifname)) != 0) {
    return -1;
  }

  sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
    return -1;
  }

  memset(&ifmr, 0, sizeof(ifmr));
  strncpy(ifmr.ifm_name, ifname, sizeof(ifmr.ifm_name) - 1);
  ifmr.ifm_name[sizeof(ifmr.ifm_name) - 1] = '\0';

  if (ioctl(sock, SIOCGIFMEDIA, &ifmr) < 0) {
    close(sock);
    return -1;
  }

  close(sock);
  type = IFM_TYPE(ifmr.ifm_active);

  if (type == IFM_ETHER) {
    return 1;
  }

#ifdef IFM_IEEE80211

  if (type == IFM_IEEE80211) {
    return 0;
  }

#endif
  return 0;
}
#endif

int nw_is_wired(const char *remote_ip) {
#ifdef __APPLE__
  return nw_is_wired_macos(remote_ip);
#elif defined(__linux__)
  (void)remote_ip;
  return 1;
#else
  (void)remote_ip;
  return -1;
#endif
}
