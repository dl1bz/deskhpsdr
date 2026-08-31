// p2_tool.c

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
#define SET_IP_LEN     115
#define RX_TIMEOUT_SEC 2

static int verbose = 0;
static int set_ip_requested = 0;
static struct in_addr set_ip_addr;
static int target_mac_requested = 0;
static unsigned char target_mac[6];

#define MAX_DISCOVERED_DEVICES 64

struct discovered_device {
  unsigned char mac[6];
  struct ifaddrs *ifa;
};

static struct discovered_device discovered_devices[MAX_DISCOVERED_DEVICES];
static int discovered_count = 0;

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

static int parse_mac(const char *text, unsigned char *mac) {
  unsigned int b[6];
  char tail;
  if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%c",
             &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &tail) != 6) {
    return -1;
  }
  for (int i = 0; i < 6; i++) {
    mac[i] = (unsigned char)b[i];
  }
  return 0;
}

static void remember_device(const unsigned char *mac, struct ifaddrs *ifa) {
  for (int i = 0; i < discovered_count; i++) {
    if (memcmp(discovered_devices[i].mac, mac, 6) == 0) {
      return;
    }
  }
  if (discovered_count >= MAX_DISCOVERED_DEVICES) {
    fprintf(stderr, "Too many Protocol 2 devices detected; ignoring additional devices.\n");
    return;
  }
  memcpy(discovered_devices[discovered_count].mac, mac, 6);
  discovered_devices[discovered_count].ifa = ifa;
  discovered_count++;
}

static int send_set_ip_packet(int sock, const unsigned char *mac) {
  unsigned char packet[SET_IP_LEN];
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
    remember_device(&buf[5], ifa);
  }
  close(sock);
}

static int set_ip_on_interface(struct ifaddrs *ifa, const unsigned char *mac) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    perror("socket");
    return -1;
  }
  int optval = 1;
  setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
#ifdef __linux__
  if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE,
                 ifa->ifa_name, strlen(ifa->ifa_name)) < 0) {
    if (verbose) {
      perror("setsockopt(SO_BINDTODEVICE)");
    }
  }
#endif
  struct sockaddr_in bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
  bind_addr.sin_port = htons(0);
  if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
    perror("bind(set-ip)");
    close(sock);
    return -1;
  }
  int rc = send_set_ip_packet(sock, mac);
  close(sock);
  return rc;
}

static int apply_ip_change(void) {
  int selected = -1;
  if (discovered_count == 0) {
    fprintf(stderr, "ERROR: No Protocol 2 device found.\n");
    return 1;
  }
  if (target_mac_requested) {
    for (int i = 0; i < discovered_count; i++) {
      if (memcmp(discovered_devices[i].mac, target_mac, 6) == 0) {
        selected = i;
        break;
      }
    }
    if (selected < 0) {
      fprintf(stderr,
              "ERROR: Target MAC %02x:%02x:%02x:%02x:%02x:%02x was not found.\n",
              target_mac[0], target_mac[1], target_mac[2],
              target_mac[3], target_mac[4], target_mac[5]);
      return 1;
    }
  } else {
    if (discovered_count > 1) {
      fprintf(stderr,
              "ERROR: More than one Protocol 2 device found. "
              "Specify the target with -mac xx:xx:xx:xx:xx:xx.\n");
      return 1;
    }
    selected = 0;
  }
  char new_ip[INET_ADDRSTRLEN];
  if (!inet_ntop(AF_INET, &set_ip_addr, new_ip, sizeof(new_ip))) {
    strcpy(new_ip, "?");
  }
  const unsigned char *mac = discovered_devices[selected].mac;
  struct ifaddrs *ifa = discovered_devices[selected].ifa;
  if (set_ip_on_interface(ifa, mac) != 0) {
    return 1;
  }
  printf("set-ip sent to %02x:%02x:%02x:%02x:%02x:%02x new_ip=%s if=%s\n",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
         new_ip, ifa->ifa_name);
  return 0;
}

int main(int argc, char **argv) {
  const char *wanted_if = NULL;
  const char *usage =
          "Usage: %s [-v] [-ip ip-address | -dhcp] "
          "[-mac xx:xx:xx:xx:xx:xx] [interface]\n";
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-v") == 0) {
      verbose = 1;
    } else if (strcmp(argv[i], "-ip") == 0) {
      if (set_ip_requested || ++i >= argc ||
          inet_pton(AF_INET, argv[i], &set_ip_addr) != 1) {
        fprintf(stderr, usage, argv[0]);
        return 1;
      }
      set_ip_requested = 1;
    } else if (strcmp(argv[i], "-dhcp") == 0) {
      if (set_ip_requested) {
        fprintf(stderr, usage, argv[0]);
        return 1;
      }
      set_ip_addr.s_addr = htonl(INADDR_ANY);
      set_ip_requested = 1;
    } else if (strcmp(argv[i], "-mac") == 0) {
      if (++i >= argc || target_mac_requested ||
          parse_mac(argv[i], target_mac) != 0) {
        fprintf(stderr, usage, argv[0]);
        return 1;
      }
      target_mac_requested = 1;
    } else if (!wanted_if) {
      wanted_if = argv[i];
    } else {
      fprintf(stderr, usage, argv[0]);
      return 1;
    }
  }
  if (target_mac_requested && !set_ip_requested) {
    fprintf(stderr, usage, argv[0]);
    return 1;
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
  int rc = 0;
  if (set_ip_requested) {
    rc = apply_ip_change();
  }
  freeifaddrs(addrs);
  return rc;
}
