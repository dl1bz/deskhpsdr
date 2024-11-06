/* Copyright (C)
* 2024 - Christoph van W"ullen, DL1YCF
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

//
// Stand-alone "UDP listener" program for use with the piHPSDR
// Text-to-Speech hook.
//
// This program will grap UDP packets (which may be broadcast)
// to port 19080.  The contents are interpreted as a text and
// fed to the text-to-speech program, which must be configured
// to produce audio on a suitable device.
//
// ESPEAK or SAY must be defined and indicate the full
// pathname of the text-to-speech application.
// If both options are defined SAY is tried first and
// ESPEAK is exec'd if SAY fails.
//
// The preferred variant for Linux is espeak (called espeak-ng
// in some distros).
// On MacOS, it is possible to install espeak via homebrew,
// but on MacOS it is preferable to use the built-in text-to-speech generator /usr/bin/say.
//
// Assuming /usr/bin/say does not exist on LINUX machines, the setting below should work
// both on MacOS (using /usr/bin/say) and LINUX (failing on /usr/bin/say thus using
// /usr/bin/espeak-ng).
//

#define SAY       "/usr/bin/say"
#define ESPEAK    "/usr/bin/espeak-ng"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main() {
  int optval;
  int udpsock;                   // UDP listening socket
  struct sockaddr_in addr;       // address to listen to
  struct sockaddr_in from;       // filled in recvfrm: where the packet came from
  socklen_t lenaddr;
  ssize_t bytes;                  // size (in bytes) of a received packet
  char msg[130];                 // buffer for message from piHPSDR, with extra space for EOL
  struct timeval tv;             // timeout for UDP receives
  //
  // On MacOS, the /bin/say program will ONLY process the input
  // line-by-line if stdin is a TTY. So we need not just a simple
  // pipe, but a pair of pseudo-tty file descriptors to "feed" data
  // packet-by-packet to the text-to-speech program.
  //
  // This also works with eSpeak although it is not necessary there.
  //
  int ptyfd = posix_openpt(O_RDWR);
  grantpt(ptyfd);
  unlockpt(ptyfd);

  if (fork() == 0) {
    //
    // Child process: connect pipe to stdin
    // connect stdout and stderr to /dev/null
    //
    char *pts = ptsname(ptyfd);
    int stdinfd = open(pts, O_RDWR);
    int fd1 = open("/dev/null", O_WRONLY);
    int fd2 = open("/dev/null", O_WRONLY);
    close (0);
    close (1);
    close (2);
    dup(stdinfd);      // assigned to stdin
    dup(fd1);          // assigned to stdout
    dup(fd2);          // assigned to stderr
    close(ptyfd);      // close unused fd
    close(fd1);        // close unused fd
    close(fd2);        // close unused fd
#ifdef SAY
    execl(SAY, SAY, (char *) NULL);
#endif
    //
    // If we arrive here, there is probably no SAY program
    // available (e.g., because we are on LINUX).
    // Silently fall through to ESPEAK.
    //
#ifdef ESPEAK
    execl(ESPEAK, ESPEAK, (char *) NULL);
#endif
    //
    // We should not arrive at this point.
    // Note if this program is terminated, it is expected that
    // the forked-off program (say or espeak) "sees" EOF on
    // stdin and then also terminates.
    //
    exit(1);
  }

  //
  // Let things settle for one second, then prepeare for UDP listening
  //
  sleep(1);

  if ((udpsock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("SOCKET:");
    exit(1);
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(19080);
  optval = 1;
  setsockopt(udpsock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
  setsockopt(udpsock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  setsockopt(udpsock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
  tv.tv_sec = 0;
  tv.tv_usec = 50000;
  setsockopt(udpsock, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv));

  if (bind(udpsock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("BIND:");
    exit(1);
  }

  for (;;) {
    //
    // Infinite loop: get UDP packet,
    //                send the text to the Text-to-Speech process
    //                forked off at the beginning.
    //                Add end-of-line to each packet contents
    //
    lenaddr = sizeof(from);
    bytes = recvfrom(udpsock, msg, 128, 0, (struct sockaddr *)&from, &lenaddr);

    if (bytes <= 0) {
      usleep(100000);
      continue;
    }

    msg[bytes++] = '\n';                                  // form EOL terminated string
    write (ptyfd, msg, bytes);                            // send it to TTS application
  }
}
