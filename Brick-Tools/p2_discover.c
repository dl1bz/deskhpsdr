// p2_discover.c

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __linux__
  #include <linux/if.h>
#endif

#define DISCOVERY_PORT 1024
#define DISCOVERY_LEN  60
#define RX_TIMEOUT_SEC 2

static int verbose = 0;
static int set_ip_requested = 0;
static struct in_addr set_ip_addr;

#define NEW_DEVICE_ATLAS        1000
#define NEW_DEVICE_HERMES       1001
#define NEW_DEVICE_HERMES2      1002
#define NEW_DEVICE_ANGELIA      1003
#define NEW_DEVICE_ORION        1004
#define NEW_DEVICE_ORION2       1005
#define NEW_DEVICE_HERMES_LITE  1006
#define NEW_DEVICE_SATURN       1010

static int new_protocol_device_id(int raw_id) {
  return 1000 + raw_id;
}

static const char *device_name(int id, int software_version) {
  switch (id) {
  case NEW_DEVICE_ATLAS:
    return "Atlas";
  case NEW_DEVICE_HERMES:
    return "Hermes";
  case NEW_DEVICE_HERMES2:
    return "Hermes2";
  case NEW_DEVICE_ANGELIA:
    return "Angelia";
  case NEW_DEVICE_ORION:
    return "Orion";
  case NEW_DEVICE_ORION2:
    return "Orion2";
  case NEW_DEVICE_SATURN:
    return "Saturn/G2";
  case NEW_DEVICE_HERMES_LITE:
    return software_version < 40 ? "Hermes Lite V1" : "Hermes Lite V2";
  default:
    return "Unknown";
  }
}

static void hexdump(const unsigned char *buf, ssize_t len) {
  for (ssize_t i = 0; i < len; i++) {
    if ((i % 16) == 0) {
      printf("\n%04zd: ", i);
    }
    printf("%02x ", buf[i]);
  }
  printf("\n");
}

static int send_set_ip_packet(int sock, const unsigned char *mac) {
  unsigned char packet[DISCOVERY_LEN];
  struct sockaddr_in dst;
  memset(packet, 0, sizeof(packet));
  packet[4] = 0x03;
  memcpy(&packet[5], mac, 6);
  memcpy(&packet[11], &set_ip_addr.s_addr, 4);
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(DISCOVERY_PORT);
  dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  if (sendto(sock, packet, sizeof(packet), 0,
             (struct sockaddr *)&dst, sizeof(dst)) < 0) {
    perror("sendto(set-ip)");
    return -1;
  }
  return 0;
}

static void discover_on_interface(struct ifaddrs *ifa) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    perror("socket");
    return;
  }
  int optval = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#ifdef SO_REUSEPORT
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
#endif
  setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
#ifdef __linux__
  if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE,
                 ifa->ifa_name, strlen(ifa->ifa_name)) < 0) {
    if (verbose) {
      perror("setsockopt(SO_BINDTODEVICE)");
    }
  }
#endif
  struct sockaddr_in *if_addr = (struct sockaddr_in *)ifa->ifa_addr;
  struct sockaddr_in bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr = if_addr->sin_addr;
  bind_addr.sin_port = htons(0);
  if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
    if (verbose) {
      perror("bind");
    }
    close(sock);
    return;
  }
  struct timeval tv;
  tv.tv_sec = RX_TIMEOUT_SEC;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  unsigned char packet[DISCOVERY_LEN];
  memset(packet, 0, sizeof(packet));
  packet[4] = 0x02;
  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(DISCOVERY_PORT);
  dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  if (sendto(sock, packet, sizeof(packet), 0,
             (struct sockaddr *)&dst, sizeof(dst)) < 0) {
    if (verbose) {
      perror("sendto");
    }
    close(sock);
    return;
  }
  for (;;) {
    unsigned char buf[2048];
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);
    ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&src, &srclen);
    if (n < 0) {
      break;
    }
    if (n < 24) {
      continue;
    }
    if (buf[0] != 0x00 || buf[1] != 0x00 ||
        buf[2] != 0x00 || buf[3] != 0x00) {
      continue;
    }
    int status = buf[4] & 0xff;
    if (status != 2 && status != 3) {
      continue;
    }
    int raw_device_id = buf[11] & 0xff;
    int device_id = new_protocol_device_id(raw_device_id);
    int p2_version = buf[12] & 0xff;
    int software_version = buf[13] & 0xff;
    int rx_count = buf[20] & 0xff;
    int beta_version = buf[23] & 0xff;
    char ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip))) {
      continue;
    }
    printf("%s %02x:%02x:%02x:%02x:%02x:%02x",
           ip,
           buf[5], buf[6], buf[7],
           buf[8], buf[9], buf[10]);
    printf(" device=%s id=%d raw=%d p2=%d.%d sw=%d.%d beta=%d rx=%d status=%d",
           device_name(device_id, software_version),
           device_id,
           raw_device_id,
           p2_version / 10,
           p2_version % 10,
           software_version / 10,
           software_version % 10,
           beta_version,
           rx_count,
           status);
    if (verbose) {
      printf(" if=%s len=%zd", ifa->ifa_name, n);
    }
    printf("\n");
    if (verbose) {
      hexdump(buf, n);
    }
    if (set_ip_requested) {
      char new_ip[INET_ADDRSTRLEN];
      if (!inet_ntop(AF_INET, &set_ip_addr, new_ip, sizeof(new_ip))) {
        strcpy(new_ip, "?");
      }
      if (send_set_ip_packet(sock, &buf[5]) == 0) {
        printf("set-ip sent to %02x:%02x:%02x:%02x:%02x:%02x new_ip=%s if=%s\n",
               buf[5], buf[6], buf[7], buf[8], buf[9], buf[10],
               new_ip, ifa->ifa_name);
      }
      break;
    }
  }
  close(sock);
}

int main(int argc, char **argv) {
  const char *wanted_if = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-v") == 0) {
      verbose = 1;
    } else if (strcmp(argv[i], "-s") == 0) {
      if (++i >= argc || inet_pton(AF_INET, argv[i], &set_ip_addr) != 1) {
        fprintf(stderr, "Usage: %s [-v] [-s ip-address] [interface]\n", argv[0]);
        return 1;
      }
      set_ip_requested = 1;
    } else if (!wanted_if) {
      wanted_if = argv[i];
    } else {
      fprintf(stderr, "Usage: %s [-v] [-s ip-address] [interface]\n", argv[0]);
      return 1;
    }
  }
  struct ifaddrs *addrs = NULL;
  if (getifaddrs(&addrs) != 0) {
    perror("getifaddrs");
    return 1;
  }
  for (struct ifaddrs *ifa = addrs; ifa != NULL; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr) {
      continue;
    }
    if (ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    if (!(ifa->ifa_flags & IFF_UP)) {
      continue;
    }
    if (ifa->ifa_flags & IFF_LOOPBACK) {
      continue;
    }
    if (wanted_if && strcmp(ifa->ifa_name, wanted_if) != 0) {
      continue;
    }
    discover_on_interface(ifa);
  }
  freeifaddrs(addrs);
  return 0;
}
