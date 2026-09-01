// p2_tool.c

#ifdef _MSC_VER
  #ifndef _CRT_SECURE_NO_WARNINGS
    #define _CRT_SECURE_NO_WARNINGS
  #endif
  #pragma comment(lib, "Ws2_32.lib")
  #pragma comment(lib, "Iphlpapi.lib")
#endif

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <iphlpapi.h>
#else
  #include <arpa/inet.h>
  #include <errno.h>
  #include <ifaddrs.h>
  #include <net/if.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  #ifdef __linux__
    #include <linux/if.h>
  #endif
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DISCOVERY_PORT 1024
#define DISCOVERY_LEN  60
#define SET_IP_LEN     115
#define ERASE_LEN      115
#define PROGRAM_LEN    265
#define PROGRAM_DATA   256
#define RX_TIMEOUT_SEC 2
#define ERASE_TIMEOUT_SEC 30
#define PROGRAM_RETRIES 5

#ifdef _WIN32
  typedef SOCKET p2_socket_t;
  typedef int p2_socklen_t;
  #define P2_INVALID_SOCKET INVALID_SOCKET
  #define P2_SOCKET_ERROR SOCKET_ERROR
#else
  typedef int p2_socket_t;
  typedef socklen_t p2_socklen_t;
  #define P2_INVALID_SOCKET (-1)
  #define P2_SOCKET_ERROR (-1)
#endif

#define P2_IFNAME_LEN 256

struct p2_interface {
  char name[P2_IFNAME_LEN];
  char alt_name[P2_IFNAME_LEN];
  struct in_addr address;
};

static int p2_network_init(void) {
#ifdef _WIN32
  WSADATA wsa;
  int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
  if (rc != 0) {
    fprintf(stderr, "WSAStartup failed: %d\n", rc);
    return -1;
  }
#endif
  return 0;
}

static void p2_network_cleanup(void) {
#ifdef _WIN32
  WSACleanup();
#endif
}

static void p2_close_socket(p2_socket_t sock) {
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
}

static void p2_socket_error(const char *what) {
#ifdef _WIN32
  fprintf(stderr, "%s failed: WSA error %d\n", what, WSAGetLastError());
#else
  perror(what);
#endif
}

static int p2_socket_timed_out(void) {
#ifdef _WIN32
  int err = WSAGetLastError();
  return err == WSAETIMEDOUT || err == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int p2_set_recv_timeout(p2_socket_t sock, int timeout_sec) {
#ifdef _WIN32
  DWORD timeout_ms = (DWORD)timeout_sec * 1000U;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                 (const char *)&timeout_ms, sizeof(timeout_ms)) == SOCKET_ERROR) {
    p2_socket_error("setsockopt(SO_RCVTIMEO)");
    return -1;
  }
#else
  struct timeval tv;
  tv.tv_sec = timeout_sec;
  tv.tv_usec = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    p2_socket_error("setsockopt(SO_RCVTIMEO)");
    return -1;
  }
#endif
  return 0;
}

static int p2_set_int_option(p2_socket_t sock, int level, int option, int value) {
#ifdef _WIN32
  return setsockopt(sock, level, option, (const char *)&value, sizeof(value));
#else
  return setsockopt(sock, level, option, &value, sizeof(value));
#endif
}

static int interface_matches(const struct p2_interface *iface, const char *wanted) {
  if (!wanted) {
    return 1;
  }
#ifdef _WIN32
  if (_stricmp(iface->name, wanted) == 0) {
    return 1;
  }
  return iface->alt_name[0] != '\0' && _stricmp(iface->alt_name, wanted) == 0;
#else
  return strcmp(iface->name, wanted) == 0;
#endif
}


static void print_help(const char *program) {
  printf("p2_tool - OpenHPSDR Protocol 2 utility\n\n");
  printf("Usage:\n");
  printf("  %s [options] [interface]\n\n", program);
  printf("Options:\n");
  printf("  -h, --help                 Show this help\n");
  printf("  -v                         Enable verbose output\n");
  printf("  -ip <address>              Set a static IPv4 address\n");
  printf("  -dhcp                      Enable DHCP\n");
  printf("  -rbf <file.rbf>            Program FPGA firmware\n");
  printf("  -mac <xx:xx:xx:xx:xx:xx>  Select a specific device\n\n");
  printf("Without an action, p2_tool discovers Protocol 2 devices.\n\n");
  printf("Examples:\n");
  printf("  %s\n", program);
  printf("  %s -v\n", program);
  printf("  %s -ip 192.168.1.100 en0\n", program);
  printf("  %s -dhcp en0\n", program);
  printf("  %s -rbf firmware.rbf en0\n", program);
  printf("  %s -rbf firmware.rbf -mac 00:1c:c0:a2:22:5c en0\n\n", program);
  printf("Notes:\n");
  printf("  -ip, -dhcp and -rbf are mutually exclusive.\n");
  printf("  Static IPv4 addresses ending in .0 or .255 are rejected.\n");
  printf("  0/8, 127/8, multicast (224/4) and reserved (240/4)\n");
  printf("  IPv4 address ranges are rejected for static configuration.\n");
  printf("  If multiple Protocol 2 devices are found, use -mac to select\n");
  printf("  the target device.\n");
#ifdef _WIN32
  printf("  On Windows, interface may be an adapter Friendly Name or\n");
  printf("  internal adapter name.\n");
#endif
  printf("  A power-cycle of the radio is required after changing the IP\n");
  printf("  configuration or programming FPGA firmware.\n");
}

static int verbose = 0;
static int set_ip_requested = 0;
static struct in_addr set_ip_addr;
static int target_mac_requested = 0;
static unsigned char target_mac[6];
static int rbf_requested = 0;
static const char *rbf_filename = NULL;

#define MAX_DISCOVERED_DEVICES 64

struct discovered_device {
  unsigned char mac[6];
  struct p2_interface iface;
  struct sockaddr_in addr;
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

static void hexdump(const unsigned char *buf, int len) {
  for (int i = 0; i < len; i++) {
    if ((i % 16) == 0) {
      printf("\n%04x: ", i);
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

static void remember_device(const unsigned char *mac,
                            const struct p2_interface *iface,
                            const struct sockaddr_in *addr) {
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
  discovered_devices[discovered_count].iface = *iface;
  discovered_devices[discovered_count].addr = *addr;
  discovered_devices[discovered_count].addr.sin_port = htons(DISCOVERY_PORT);
  discovered_count++;
}

static int send_set_ip_packet(p2_socket_t sock, const unsigned char *mac) {
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
  if (sendto(sock, (const char *)packet, (int)sizeof(packet), 0,
             (struct sockaddr *)&dst, sizeof(dst)) == P2_SOCKET_ERROR) {
    p2_socket_error("sendto(set-ip)");
    return -1;
  }
  return 0;
}

static void discover_on_interface(const struct p2_interface *iface) {
  p2_socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == P2_INVALID_SOCKET) {
    p2_socket_error("socket");
    return;
  }
  int optval = 1;
  p2_set_int_option(sock, SOL_SOCKET, SO_REUSEADDR, optval);
#ifdef SO_REUSEPORT
  p2_set_int_option(sock, SOL_SOCKET, SO_REUSEPORT, optval);
#endif
  p2_set_int_option(sock, SOL_SOCKET, SO_BROADCAST, optval);
#ifdef SO_BINDTODEVICE
  if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE,
                 iface->name, strlen(iface->name)) < 0) {
    if (verbose) {
      p2_socket_error("setsockopt(SO_BINDTODEVICE)");
    }
  }
#endif
  struct sockaddr_in bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr = iface->address;
  bind_addr.sin_port = htons(0);
  if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == P2_SOCKET_ERROR) {
    if (verbose) {
      p2_socket_error("bind");
    }
    p2_close_socket(sock);
    return;
  }
  if (p2_set_recv_timeout(sock, RX_TIMEOUT_SEC) != 0) {
    p2_close_socket(sock);
    return;
  }
  unsigned char packet[DISCOVERY_LEN];
  memset(packet, 0, sizeof(packet));
  packet[4] = 0x02;
  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(DISCOVERY_PORT);
  dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  if (sendto(sock, (const char *)packet, (int)sizeof(packet), 0,
             (struct sockaddr *)&dst, sizeof(dst)) == P2_SOCKET_ERROR) {
    if (verbose) {
      p2_socket_error("sendto");
    }
    p2_close_socket(sock);
    return;
  }
  for (;;) {
    unsigned char buf[2048];
    struct sockaddr_in src;
    p2_socklen_t srclen = (p2_socklen_t)sizeof(src);
    int n = recvfrom(sock, (char *)buf, (int)sizeof(buf), 0,
                     (struct sockaddr *)&src, &srclen);
    if (n == P2_SOCKET_ERROR) {
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
      printf(" if=%s len=%d", iface->name, n);
    }
    printf("\n");
    if (verbose) {
      hexdump(buf, n);
    }
    remember_device(&buf[5], iface, &src);
  }
  p2_close_socket(sock);
}

static int set_ip_on_interface(const struct p2_interface *iface,
                               const unsigned char *mac) {
  p2_socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == P2_INVALID_SOCKET) {
    p2_socket_error("socket");
    return -1;
  }
  int optval = 1;
  p2_set_int_option(sock, SOL_SOCKET, SO_BROADCAST, optval);
#ifdef SO_BINDTODEVICE
  if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE,
                 iface->name, strlen(iface->name)) < 0) {
    if (verbose) {
      p2_socket_error("setsockopt(SO_BINDTODEVICE)");
    }
  }
#endif
  struct sockaddr_in bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr = iface->address;
  bind_addr.sin_port = htons(0);
  if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == P2_SOCKET_ERROR) {
    p2_socket_error("bind(set-ip)");
    p2_close_socket(sock);
    return -1;
  }
  int rc = send_set_ip_packet(sock, mac);
  p2_close_socket(sock);
  return rc;
}

static int select_device(void) {
  if (discovered_count == 0) {
    fprintf(stderr, "ERROR: No Protocol 2 device found.\n");
    return -1;
  }
  if (target_mac_requested) {
    for (int i = 0; i < discovered_count; i++) {
      if (memcmp(discovered_devices[i].mac, target_mac, 6) == 0) {
        return i;
      }
    }
    fprintf(stderr,
            "ERROR: Target MAC %02x:%02x:%02x:%02x:%02x:%02x was not found.\n",
            target_mac[0], target_mac[1], target_mac[2],
            target_mac[3], target_mac[4], target_mac[5]);
    return -1;
  }
  if (discovered_count > 1) {
    fprintf(stderr,
            "ERROR: More than one Protocol 2 device found. "
            "Specify the target with -mac xx:xx:xx:xx:xx:xx.\n");
    return -1;
  }
  return 0;
}

static int apply_ip_change(void) {
  int selected = select_device();
  if (selected < 0) {
    return 1;
  }
  char new_ip[INET_ADDRSTRLEN];
  if (!inet_ntop(AF_INET, &set_ip_addr, new_ip, sizeof(new_ip))) {
    strcpy(new_ip, "?");
  }
  const unsigned char *mac = discovered_devices[selected].mac;
  const struct p2_interface *iface = &discovered_devices[selected].iface;
  if (set_ip_on_interface(iface, mac) != 0) {
    return 1;
  }
  printf("set-ip sent to %02x:%02x:%02x:%02x:%02x:%02x new_ip=%s if=%s\n",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
         new_ip, iface->name);
  return 0;
}

static uint32_t read_be32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) |
         ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) |
         (uint32_t)p[3];
}

static void write_be32(unsigned char *p, uint32_t value) {
  p[0] = (unsigned char)(value >> 24);
  p[1] = (unsigned char)(value >> 16);
  p[2] = (unsigned char)(value >> 8);
  p[3] = (unsigned char)value;
}

static p2_socket_t open_device_socket(const struct discovered_device *dev,
                                      int timeout_sec) {
  p2_socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == P2_INVALID_SOCKET) {
    p2_socket_error("socket");
    return P2_INVALID_SOCKET;
  }
  int optval = 1;
  p2_set_int_option(sock, SOL_SOCKET, SO_REUSEADDR, optval);
#ifdef SO_REUSEPORT
  p2_set_int_option(sock, SOL_SOCKET, SO_REUSEPORT, optval);
#endif
#ifdef SO_BINDTODEVICE
  if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE,
                 dev->iface.name, strlen(dev->iface.name)) < 0) {
    if (verbose) {
      p2_socket_error("setsockopt(SO_BINDTODEVICE)");
    }
  }
#endif
  struct sockaddr_in bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr = dev->iface.address;
  bind_addr.sin_port = htons(0);
  if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == P2_SOCKET_ERROR) {
    p2_socket_error("bind(program)");
    p2_close_socket(sock);
    return P2_INVALID_SOCKET;
  }
  if (p2_set_recv_timeout(sock, timeout_sec) != 0) {
    p2_close_socket(sock);
    return P2_INVALID_SOCKET;
  }
  return sock;
}

static int recv_program_reply(p2_socket_t sock, const struct discovered_device *dev,
                              unsigned char expected_type, uint32_t expected_seq) {
  unsigned char buf[2048];
  for (;;) {
    struct sockaddr_in src;
    p2_socklen_t srclen = (p2_socklen_t)sizeof(src);
    int n = recvfrom(sock, (char *)buf, (int)sizeof(buf), 0,
                     (struct sockaddr *)&src, &srclen);
    if (n == P2_SOCKET_ERROR) {
      if (p2_socket_timed_out()) {
        return 0;
      }
      p2_socket_error("recvfrom(program)");
      return -1;
    }
    if (n < 5) {
      continue;
    }
    if (src.sin_addr.s_addr != dev->addr.sin_addr.s_addr) {
      continue;
    }
    if (verbose) {
      printf("program reply type=%u seq=%u len=%d\n",
             buf[4], read_be32(buf), n);
      hexdump(buf, n);
    }
    if (buf[4] == expected_type && read_be32(buf) == expected_seq) {
      return 1;
    }
  }
}

static int erase_device(p2_socket_t sock, const struct discovered_device *dev) {
  unsigned char packet[ERASE_LEN];
  memset(packet, 0, sizeof(packet));
  packet[4] = 0x04;
  printf("Erasing FPGA flash...\n");
  if (sendto(sock, (const char *)packet, (int)sizeof(packet), 0,
             (const struct sockaddr *)&dev->addr, sizeof(dev->addr)) == P2_SOCKET_ERROR) {
    p2_socket_error("sendto(erase)");
    return -1;
  }
  /*
   * The Protocol 2 programmer receives two type-3, sequence-0 replies:
   * the first acknowledges the erase request, the second signals completion.
   */
  for (int reply = 0; reply < 2; reply++) {
    int rc = recv_program_reply(sock, dev, 0x03, 0);
    if (rc <= 0) {
      fprintf(stderr, "ERROR: FPGA flash erase %s.\n",
              rc == 0 ? "timed out" : "failed");
      return -1;
    }
  }
  printf("Erase complete.\n");
  return 0;
}

static int program_rbf_device(const struct discovered_device *dev,
                              const char *filename) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    perror(filename);
    return 1;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    perror("fseek");
    fclose(fp);
    return 1;
  }
  long file_size = ftell(fp);
  if (file_size <= 0) {
    fprintf(stderr, "ERROR: RBF file is empty or its size cannot be determined.\n");
    fclose(fp);
    return 1;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    perror("fseek");
    fclose(fp);
    return 1;
  }
  uint64_t blocks64 = ((uint64_t)file_size + PROGRAM_DATA - 1) / PROGRAM_DATA;
  if (blocks64 > UINT32_MAX) {
    fprintf(stderr, "ERROR: RBF file is too large.\n");
    fclose(fp);
    return 1;
  }
  uint32_t blocks = (uint32_t)blocks64;
  char ip[INET_ADDRSTRLEN];
  if (!inet_ntop(AF_INET, &dev->addr.sin_addr, ip, sizeof(ip))) {
    strcpy(ip, "?");
  }
  printf("RBF: %s\n", filename);
  printf("Target: %s %02x:%02x:%02x:%02x:%02x:%02x\n",
         ip,
         dev->mac[0], dev->mac[1], dev->mac[2],
         dev->mac[3], dev->mac[4], dev->mac[5]);
  printf("Size: %ld bytes, %u blocks\n", file_size, blocks);
  p2_socket_t sock = open_device_socket(dev, ERASE_TIMEOUT_SEC);
  if (sock == P2_INVALID_SOCKET) {
    fclose(fp);
    return 1;
  }
  if (erase_device(sock, dev) != 0) {
    p2_close_socket(sock);
    fclose(fp);
    return 1;
  }
  if (p2_set_recv_timeout(sock, RX_TIMEOUT_SEC) != 0) {
    p2_close_socket(sock);
    fclose(fp);
    return 1;
  }
  printf("Programming...\n");
  for (uint32_t seq = 0; seq < blocks; seq++) {
    unsigned char packet[PROGRAM_LEN];
    memset(packet, 0, sizeof(packet));
    write_be32(&packet[0], seq);
    packet[4] = 0x05;
    write_be32(&packet[5], blocks);
    memset(&packet[9], 0xff, PROGRAM_DATA);
    size_t nread = fread(&packet[9], 1, PROGRAM_DATA, fp);
    if (nread == 0 && ferror(fp)) {
      perror("fread");
      p2_close_socket(sock);
      fclose(fp);
      return 1;
    }
    int acknowledged = 0;
    for (int attempt = 0; attempt < PROGRAM_RETRIES; attempt++) {
      if (sendto(sock, (const char *)packet, (int)sizeof(packet), 0,
                 (const struct sockaddr *)&dev->addr, sizeof(dev->addr)) == P2_SOCKET_ERROR) {
        p2_socket_error("sendto(program)");
        p2_close_socket(sock);
        fclose(fp);
        return 1;
      }
      int rc = recv_program_reply(sock, dev, 0x04, seq);
      if (rc < 0) {
        p2_close_socket(sock);
        fclose(fp);
        return 1;
      }
      if (rc > 0) {
        acknowledged = 1;
        break;
      }
      if (verbose) {
        fprintf(stderr, "program block %u: timeout, retry %d/%d\n",
                seq, attempt + 1, PROGRAM_RETRIES);
      }
    }
    if (!acknowledged) {
      fprintf(stderr, "ERROR: No acknowledgement for RBF block %u.\n", seq);
      p2_close_socket(sock);
      fclose(fp);
      return 1;
    }
    unsigned int percent =
            (unsigned int)(((uint64_t)(seq + 1) * 100U) / blocks);
    if (seq == 0 || seq + 1 == blocks || ((seq + 1) % 100U) == 0) {
      printf("\rProgramming: %u%% (%u/%u)", percent, seq + 1, blocks);
      fflush(stdout);
    }
  }
  printf("\nProgram complete.\n");
  p2_close_socket(sock);
  fclose(fp);
  return 0;
}

static int apply_rbf_program(void) {
  int selected = select_device();
  if (selected < 0) {
    return 1;
  }
  return program_rbf_device(&discovered_devices[selected], rbf_filename);
}

#ifdef _WIN32
static int wide_to_utf8(const wchar_t *src, char *dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0) {
    return -1;
  }
  int rc = WideCharToMultiByte(CP_UTF8, 0, src, -1,
                               dst, (int)dst_size, NULL, NULL);
  if (rc <= 0) {
    dst[0] = '\0';
    return -1;
  }
  return 0;
}

static int discover_windows_interfaces(const char *wanted_if) {
  ULONG size = 0;
  ULONG flags = GAA_FLAG_SKIP_ANYCAST |
                GAA_FLAG_SKIP_MULTICAST |
                GAA_FLAG_SKIP_DNS_SERVER;
  DWORD rc = GetAdaptersAddresses(AF_INET, flags, NULL, NULL, &size);
  if (rc != ERROR_BUFFER_OVERFLOW) {
    fprintf(stderr, "GetAdaptersAddresses(size) failed: %lu\n",
            (unsigned long)rc);
    return -1;
  }
  IP_ADAPTER_ADDRESSES *addrs = (IP_ADAPTER_ADDRESSES *)malloc(size);
  if (!addrs) {
    fprintf(stderr, "ERROR: Out of memory enumerating network adapters.\n");
    return -1;
  }
  rc = GetAdaptersAddresses(AF_INET, flags, NULL, addrs, &size);
  if (rc != NO_ERROR) {
    fprintf(stderr, "GetAdaptersAddresses failed: %lu\n", (unsigned long)rc);
    free(addrs);
    return -1;
  }
  for (IP_ADAPTER_ADDRESSES *aa = addrs; aa; aa = aa->Next) {
    if (aa->OperStatus != IfOperStatusUp ||
        aa->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
      continue;
    }
    for (IP_ADAPTER_UNICAST_ADDRESS *ua = aa->FirstUnicastAddress;
         ua; ua = ua->Next) {
      if (!ua->Address.lpSockaddr ||
          ua->Address.lpSockaddr->sa_family != AF_INET) {
        continue;
      }
      struct p2_interface iface;
      memset(&iface, 0, sizeof(iface));
      if (aa->FriendlyName) {
        wide_to_utf8(aa->FriendlyName, iface.name, sizeof(iface.name));
      }
      if (iface.name[0] == '\0' && aa->AdapterName) {
        snprintf(iface.name, sizeof(iface.name), "%s", aa->AdapterName);
      }
      if (aa->AdapterName) {
        snprintf(iface.alt_name, sizeof(iface.alt_name), "%s", aa->AdapterName);
      }
      iface.address =
              ((struct sockaddr_in *)ua->Address.lpSockaddr)->sin_addr;
      if (!interface_matches(&iface, wanted_if)) {
        continue;
      }
      discover_on_interface(&iface);
    }
  }
  free(addrs);
  return 0;
}
#else
static int discover_unix_interfaces(const char *wanted_if) {
  struct ifaddrs *addrs = NULL;
  if (getifaddrs(&addrs) != 0) {
    perror("getifaddrs");
    return -1;
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
    struct p2_interface iface;
    memset(&iface, 0, sizeof(iface));
    snprintf(iface.name, sizeof(iface.name), "%s", ifa->ifa_name);
    iface.address = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
    if (!interface_matches(&iface, wanted_if)) {
      continue;
    }
    discover_on_interface(&iface);
  }
  freeifaddrs(addrs);
  return 0;
}
#endif

int main(int argc, char **argv) {
  const char *wanted_if = NULL;
  const char *usage =
          "Usage: %s [-v] [-ip ip-address | -dhcp | -rbf file.rbf] "
          "[-mac xx:xx:xx:xx:xx:xx] [interface]\n";
  if (p2_network_init() != 0) {
    return 1;
  }
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_help(argv[0]);
      p2_network_cleanup();
      return 0;
    } else if (strcmp(argv[i], "-v") == 0) {
      verbose = 1;
    } else if (strcmp(argv[i], "-ip") == 0) {
      if (set_ip_requested || rbf_requested || ++i >= argc ||
          inet_pton(AF_INET, argv[i], &set_ip_addr) != 1) {
        fprintf(stderr, usage, argv[0]);
        p2_network_cleanup();
        return 1;
      }
      uint32_t host_ip = ntohl(set_ip_addr.s_addr);
      uint8_t first_octet = (uint8_t)((host_ip >> 24) & 0xffU);
      uint8_t last_octet = (uint8_t)(host_ip & 0xffU);
      if (last_octet == 0U || last_octet == 255U) {
        fprintf(stderr,
                "ERROR: Static IPv4 address must not end in .0 or .255.\n");
        p2_network_cleanup();
        return 1;
      }
      if (first_octet == 0U || first_octet == 127U || first_octet >= 224U) {
        fprintf(stderr,
                "ERROR: IPv4 address is not valid as a static host address.\n");
        p2_network_cleanup();
        return 1;
      }
      set_ip_requested = 1;
    } else if (strcmp(argv[i], "-dhcp") == 0) {
      if (set_ip_requested || rbf_requested) {
        fprintf(stderr, usage, argv[0]);
        p2_network_cleanup();
        return 1;
      }
      set_ip_addr.s_addr = htonl(INADDR_ANY);
      set_ip_requested = 1;
    } else if (strcmp(argv[i], "-rbf") == 0) {
      if (set_ip_requested || rbf_requested || ++i >= argc) {
        fprintf(stderr, usage, argv[0]);
        p2_network_cleanup();
        return 1;
      }
      rbf_filename = argv[i];
      rbf_requested = 1;
    } else if (strcmp(argv[i], "-mac") == 0) {
      if (++i >= argc || target_mac_requested ||
          parse_mac(argv[i], target_mac) != 0) {
        fprintf(stderr, usage, argv[0]);
        p2_network_cleanup();
        return 1;
      }
      target_mac_requested = 1;
    } else if (!wanted_if) {
      wanted_if = argv[i];
    } else {
      fprintf(stderr, usage, argv[0]);
      p2_network_cleanup();
      return 1;
    }
  }
  if (target_mac_requested && !set_ip_requested && !rbf_requested) {
    fprintf(stderr, usage, argv[0]);
    p2_network_cleanup();
    return 1;
  }
#ifdef _WIN32
  if (discover_windows_interfaces(wanted_if) != 0) {
    p2_network_cleanup();
    return 1;
  }
#else
  if (discover_unix_interfaces(wanted_if) != 0) {
    p2_network_cleanup();
    return 1;
  }
#endif
  int rc = 0;
  if (set_ip_requested) {
    rc = apply_ip_change();
  } else if (rbf_requested) {
    rc = apply_rbf_program();
  }
  p2_network_cleanup();
  return rc;
}
