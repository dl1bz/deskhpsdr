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
// Test program sending UDP packets containing text intended
// for a text-to-speech program. Mainly used to debug udplistener.
//


#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

//
// tts_send: send broadcast UDP packet containing a string
//
void tts_send(char *msg) {
  int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int optval = 1;
  struct sockaddr_in addr;
  printf("%s: sending >>>%s<<<\n", __FUNCTION__, msg);
  setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  addr.sin_port = htons(19080);
  sendto(sock, msg, strlen(msg), 0, (struct sockaddr * ) &addr, sizeof(addr));
  close(sock);
}

int main() {
  int turn = 0;
  char msg[64];

  for (;;) {
    snprintf(msg, sizeof(msg), "This is Test No. %d", ++turn);
    tts_send(msg);
    sleep(5);
  }
}
