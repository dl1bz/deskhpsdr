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
*   hl2_ip_tool.c
*
*   Build: gcc -std=c11 -O2 -D_DEFAULT_SOURCE -Wall hl2_ip_tool.c -o hl2_ip_tool
*
*   HL2 Fixed-IP setzen/löschen (UDP:1025) + Reboot via C&C-Frame (UDP:1025, Addr 0x3A)
*/
#ifndef _DEFAULT_SOURCE
  #define _DEFAULT_SOURCE
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PORT_CMD   1025
#define HL2_ADDR   0x3D
#define HL2_REBOOT_ADDR 0x3A
#define I2C_ADDR   0xAC

// Flags @ EEPROM[0x06]
#define FLAG_USE_EEPROM_IP  0x80
#define FLAG_USE_EEPROM_MAC 0x40
#define FLAG_FAVOR_DHCP     0x20

static void die(const char* m) { perror(m); exit(1); }

static int udpsock(void) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  if (fd < 0) { die("socket"); }

  int yes = 1;

  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) { die("SO_REUSEADDR"); }

  struct sockaddr_in any;

  memset(&any, 0, sizeof any);
  any.sin_family = AF_INET;
  any.sin_port = htons(0);
  any.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(fd, (struct sockaddr * )&any, sizeof any) < 0) { die("bind"); }

  return fd;
}

static int recv60(int fd, uint8_t out[60], int ms, struct sockaddr_in* from) {
  fd_set rf;
  FD_ZERO(&rf);
  FD_SET(fd, &rf);
  struct timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  int r = select(fd + 1, &rf, NULL, NULL, &tv);

  if (r <= 0) { return 0; }

  socklen_t sl = sizeof *from;
  ssize_t n = recvfrom(fd, out, 60, 0, (struct sockaddr*)from, &sl);
  return (n == 60 && out[0] == 0xEF && out[1] == 0xFE) ? 60 : 0;
}

static int discover(int fd, struct sockaddr_in* dst) {
  uint8_t msg[60];
  memset(msg, 0, sizeof msg);
  msg[0] = 0xEF;
  msg[1] = 0xFE;
  msg[2] = 0x02;
  struct sockaddr_in b;
  memset(&b, 0, sizeof b);
  b.sin_family = AF_INET;
  b.sin_port = htons(PORT_CMD);
  inet_pton(AF_INET, "255.255.255.255", &b.sin_addr);
  int one = 1;

  if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one) < 0) { die("SO_BROADCAST"); }

  if (sendto(fd, msg, 60, 0, (struct sockaddr * )&b, sizeof b) != (ssize_t)60) { die("sendto discover"); }

  uint8_t buf[60];
  struct sockaddr_in src;

  if (recv60(fd, buf, 1000, &src) == 60) { *dst = src; return 1; }

  return 0;
}

// C&C: EF FE 05 7F <addr<<1> <C1..C4> <pad>; Antwort→response_data (BE32 @ 0x17..0x1A)
static int hl2_cmd(int fd, const struct sockaddr_in* dst, uint8_t addr,
                   uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint32_t* resp_be32) {
  uint8_t msg[60];
  memset(msg, 0, sizeof msg);
  msg[0] = 0xEF;
  msg[1] = 0xFE;
  msg[2] = 0x05;
  msg[3] = 0x7F;
  msg[4] = addr << 1;
  msg[5] = c1;
  msg[6] = c2;
  msg[7] = c3;
  msg[8] = c4;

  if (sendto(fd, msg, 60, 0, (const struct sockaddr * )dst, sizeof *dst) != (ssize_t)60) { return -1; }

  uint8_t r[60];
  struct sockaddr_in src;

  if (!recv60(fd, r, 1000, &src)) { return 0; }

  if (resp_be32) { *resp_be32 = (r[0x17] << 24) | (r[0x18] << 16) | (r[0x19] << 8) | r[0x1A]; }

  return 1;
}

// EEPROM lesen: C1=0x07, C2=I2C_ADDR, C3=(reg<<4)|0x0C, C4=0x00 → (resp>>8)&0xFF
static int eeprom_read(int fd, const struct sockaddr_in* dst, uint8_t reg, uint8_t* val) {
  uint32_t resp = 0;
  uint8_t raddr = (uint8_t)(((reg & 0xFF) << 4) | 0x0C);
  int ok = hl2_cmd(fd, dst, HL2_ADDR, 0x07, I2C_ADDR, raddr, 0x00, &resp);

  if (ok != 1) { return ok; }

  *val = (uint8_t)((resp >> 8) & 0xFF);
  return 1;
}

// EEPROM schreiben: C1=0x06, C2=I2C_ADDR, C3=(reg<<4), C4=data
static int eeprom_write(int fd, const struct sockaddr_in* dst, uint8_t reg, uint8_t val) {
  uint8_t waddr = (uint8_t)((reg & 0xFF) << 4);
  return hl2_cmd(fd, dst, HL2_ADDR, 0x06, I2C_ADDR, waddr, val, NULL);
}

// Read-Verify mit Retries
static int eeprom_read_retry(int fd, const struct sockaddr_in* dst, uint8_t reg,
                             uint8_t expect, uint8_t* out) {
  const int max_try = 12; // ~1.8 s
  uint8_t v = 0;

  for (int t = 0; t < max_try; t++) {
    int rc = eeprom_read(fd, dst, reg, &v);

    if (rc == 1) {
      if (expect == 0xFF || v == expect) { if (out) *out = v; return 1; }
    }

    usleep(150000);
  }

  if (out) { *out = v; }

  return 0;
}

// Reboot via C&C (Python-kompatibel): EF FE 05 7F (0x3A<<1) 00 00 00 01 @ 1025
static int hl2_reboot_cmd_1025(int fd, const struct sockaddr_in* cmd_dst) {
  uint8_t msg[60];
  memset(msg, 0, sizeof msg);
  msg[0] = 0xEF;
  msg[1] = 0xFE;
  msg[2] = 0x05;
  msg[3] = 0x7F;
  msg[4] = HL2_REBOOT_ADDR << 1;
  msg[5] = 0x00;
  msg[6] = 0x00;
  msg[7] = 0x00;
  msg[8] = 0x01;

  if (sendto(fd, msg, 60, 0, (const struct sockaddr * )cmd_dst, sizeof *cmd_dst) != (ssize_t)60) { return -1; }

  return 0; // Fire-and-Forget
}

static void usage(const char* p) {
  fprintf(stderr,
          "Usage:\n"
          "  %s --ip A.B.C.D --set A.B.C.D\n"
          "  %s --ip A.B.C.D --clear\n"
          "  %s --ip A.B.C.D --dhcp-first\n"
          "  %s --ip A.B.C.D --clear-dhcp-first\n"
          "  %s --ip A.B.C.D --reboot\n"
          "  %s --set A.B.C.D  # without --ip => autodiscovery\n", p, p, p, p, p, p);
}

int main(int argc, char** argv) {
  const char* dev_ip = NULL;
  const char* set_ip = NULL;
  bool do_clear = false, do_reboot = false, do_dhcp = false, do_clear_dhcp = false;
  static struct option opts[] = {
    {"ip",               required_argument, 0, 'i'},
    {"set",              required_argument, 0, 's'},
    {"clear",            no_argument,      0, 'c'},
    {"dhcp-first",       no_argument,      0, 'd'},
    {"clear-dhcp-first", no_argument,      0, 'D'},
    {"reboot",           no_argument,      0, 'r'},
    {0, 0, 0, 0}
  };
  int o;

  while ((o = getopt_long(argc, argv, "i:s:cdDr", opts, NULL)) != -1) {
    if (o == 'i') { dev_ip = optarg; }
    else if (o == 's') { set_ip = optarg; }
    else if (o == 'c') { do_clear = true; }
    else if (o == 'd') { do_dhcp = true; }
    else if (o == 'D') { do_clear_dhcp = true; }
    else if (o == 'r') { do_reboot = true; }
    else { usage(argv[0]); return 2; }
  }

  // Exakt ein Modus
  if ( (!!set_ip + (do_clear ? 1 : 0) + (do_dhcp ? 1 : 0) + (do_clear_dhcp ? 1 : 0) + (do_reboot ? 1 : 0)) != 1 ) { usage(argv[0]); return 2; }

  // IP-String → Oktette + Validierung .0/.255 forbidden
  uint8_t new_oct[4] = {0};

  if (set_ip) {
    unsigned b0, b1, b2, b3;

    if (sscanf(set_ip, "%u.%u.%u.%u", &b0, &b1, &b2, &b3) != 4 ||
        b0 > 255 || b1 > 255 || b2 > 255 || b3 > 255 || b3 == 0 || b3 == 255) {
      fprintf(stderr, "Invalid IP --set IP (Host .0/.255 forbidden)\n");
      return 2;
    }

    new_oct[0] = (uint8_t)b0;
    new_oct[1] = (uint8_t)b1;
    new_oct[2] = (uint8_t)b2;
    new_oct[3] = (uint8_t)b3;
  }

  int fd = udpsock();
  struct sockaddr_in dst;
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  dst.sin_port = htons(PORT_CMD);

  if (dev_ip) {
    if (inet_pton(AF_INET, dev_ip, &dst.sin_addr) != 1) { fprintf(stderr, "Invalid IP --ip target address\n"); return 2; }
  } else {
    if (!discover(fd, &dst)) { fprintf(stderr, "Discovery failed\n"); return 3; }
  }

  if (do_reboot) {
    int rc = hl2_reboot_cmd_1025(fd, &dst);

    if (rc != 0) { fprintf(stderr, "Reboot(0x3A) Error rc=%d\n", rc); close(fd); return 5; }

    printf("Reboot-Command (0x3A) sent @ %s:1025\n", inet_ntoa(dst.sin_addr));
    close(fd);
    return 0;
  }

  // Flags lesen
  uint8_t flags = 0;
  int rc = eeprom_read(fd, &dst, 0x06, &flags);

  if (rc != 1) { fprintf(stderr, "Read Error @0x06 (rc=%d)\n", rc); close(fd); return 4; }

  if (set_ip) {
    uint8_t regs[4] = {0x08, 0x09, 0x0A, 0x0B};

    for (int i = 0; i < 4; i++) {
      if (eeprom_write(fd, &dst, regs[i], new_oct[i]) != 1) { fprintf(stderr, "Write Error @0x%02X\n", regs[i]); close(fd); return 5; }

      uint8_t v = 0;

      if (!eeprom_read_retry(fd, &dst, regs[i], new_oct[i], &v)) { fprintf(stderr, "Verify-Timeout @0x%02X (is=0x%02X, need=0x%02X)\n", regs[i], v, new_oct[i]); close(fd); return 6; }
    }

    uint8_t nf = (uint8_t)(flags | FLAG_USE_EEPROM_IP);

    if (eeprom_write(fd, &dst, 0x06, nf) != 1) { fprintf(stderr, "Write Error @0x06\n"); close(fd); return 5; }

    uint8_t v6 = 0;

    if (!eeprom_read_retry(fd, &dst, 0x06, nf, &v6)) { fprintf(stderr, "Verify-Timeout @0x06 (is=0x%02X, need=0x%02X)\n", v6, nf); close(fd); return 6; }

    uint8_t v[4] = {0};

    for (int i = 0; i < 4; i++) { if (eeprom_read_retry(fd, &dst, regs[i], 0xFF, &v[i]) != 1) { fprintf(stderr, "Read Error @0x%02X\n", regs[i]); close(fd); return 6; } }

    printf("Fixed IP set: %u.%u.%u.%u  Flags 0x06: 0x%02X -> 0x%02X\n", v[0], v[1], v[2], v[3], flags, v6);
    printf("Activation needs Reboot or Power cycle of your HL2.\n");
    close(fd);
    return 0;
  }

  if (do_clear) {
    // Flag IP + DHCP löschen, IP-Bytes nullen
    uint8_t nf = (uint8_t)(flags & ~(FLAG_USE_EEPROM_IP | FLAG_FAVOR_DHCP));

    if (eeprom_write(fd, &dst, 0x06, nf) != 1) { fprintf(stderr, "Write Error @0x06\n"); close(fd); return 5; }

    uint8_t v6 = 0;

    if (!eeprom_read_retry(fd, &dst, 0x06, nf, &v6)) { fprintf(stderr, "Verify-Timeout @0x06 (is=0x%02X, need=0x%02X)\n", v6, nf); close(fd); return 6; }

    uint8_t regs[4] = {0x08, 0x09, 0x0A, 0x0B};

    for (int i = 0; i < 4; i++) {
      if (eeprom_write(fd, &dst, regs[i], 0x00) != 1) { fprintf(stderr, "Write Error @0x%02X\n", regs[i]); close(fd); return 5; }

      uint8_t v = 0;

      if (!eeprom_read_retry(fd, &dst, regs[i], 0x00, &v)) { fprintf(stderr, "Verify-Timeout @0x%02X (is=0x%02X, need=0x00)\n", regs[i], v); close(fd); return 6; }
    }

    uint8_t v[4] = {0};

    for (int i = 0; i < 4; i++) { if (eeprom_read_retry(fd, &dst, regs[i], 0xFF, &v[i]) != 1) { fprintf(stderr, "Read Error @0x%02X\n", regs[i]); close(fd); return 6; } }

    printf("Fixed IP and Flag [DHCP-first] cleared. Flags 0x06: 0x%02X -> 0x%02X  Bytes[08..0B]=%02X.%02X.%02X.%02X\n",
           flags, v6, v[0], v[1], v[2], v[3]);
    printf("Activation needs Reboot or Power cycle of your HL2.\n");
    close(fd);
    return 0;
  }

  if (do_dhcp) {
    uint8_t nf = (uint8_t)(flags | FLAG_FAVOR_DHCP);

    if (eeprom_write(fd, &dst, 0x06, nf) != 1) { fprintf(stderr, "Write Error @0x06\n"); close(fd); return 5; }

    uint8_t v6 = 0;

    if (!eeprom_read_retry(fd, &dst, 0x06, nf, &v6)) { fprintf(stderr, "Verify-Timeout @0x06 (is=0x%02X, need=0x%02X)\n", v6, nf); close(fd); return 6; }

    printf("Flag [DHCP-first] set. Flags 0x06: 0x%02X -> 0x%02X\n", flags, v6);
    printf("Activation needs Reboot or Power cycle of your HL2.\n");
    close(fd);
    return 0;
  }

  if (do_clear_dhcp) {
    uint8_t nf = (uint8_t)(flags & ~FLAG_FAVOR_DHCP);

    if (eeprom_write(fd, &dst, 0x06, nf) != 1) { fprintf(stderr, "Write Error @0x06\n"); close(fd); return 5; }

    uint8_t v6 = 0;

    if (!eeprom_read_retry(fd, &dst, 0x06, nf, &v6)) { fprintf(stderr, "Verify-Timeout @0x06 (is=0x%02X, need=0x%02X)\n", v6, nf); close(fd); return 6; }

    printf("Flag [DHCP-first] cleared. Flags 0x06: 0x%02X -> 0x%02X\n", flags, v6);
    printf("Activation needs Reboot or Power cycle of your HL2.\n");
    close(fd);
    return 0;
  }

  close(fd);
  return 0;
}
