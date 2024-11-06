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
// tts stands for "Text to Speech". This feature is meant to make
// piHPSDR accessible for persons with limited eye-sight or are
// even blind.
//
// To pull down expectations: I cannot implement Text-to-Speech in
// piHPSDR, this is simply out-of-scope. What is done here is
// to provide hooks to use Text-to-Speech together with piHPSDR.
//
// To this end, the core function tts_send puts a message (character
// string) into a promiscous (broadcast) UDP packet and sends it out
// to port nr. 19080 (this number is currently hard-wired).
//
// Then there is a bunch of "reporter" functions e.g. tts_freq(),
// that form a meaningful string containing the frequency of the
// active receiver sending this string via tts_send().
//
// The keyboard event handler then calls one of those functions
// if a function key is pressed, for example, pressing F1
// calls tts_freq() (see main.c)
//
// A very simple "UDP listener" (udplistener.c) is provided for
// demonstration purpose. This program is not part of piHPSDR but
// a self-contained program that can be
// run on the same computer or another computer on the same network.
// This UDP listener starts either "eSpeak" or "festival" as
// text-to-speech program, receives broadcast UDP packets on port
// 19080, and sends the text to eSpeak/festival.
//

#include <gtk/gtk.h>

#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "message.h"
#include "radio.h"
#include "receiver.h"
#include "vfo.h"

//
// tts_send: send broadcast UDP packet containing a string
//
void tts_send(char *msg) {
  int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int optval = 1;
  struct sockaddr_in addr;
  //t_print("%s: sending >>>%s<<<\n", __FUNCTION__, msg);
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

//
// tts_freq: form a message reporting the current frequency,
//           and send this string via tts_send
//
void tts_freq() {
  long long freq;
  long kilo;
  int hertz;
  int v = active_receiver->id;
  char msg[128];

  if (vfo[v].ctun) {
    freq = vfo[v].ctun_frequency;
  } else {
    freq = vfo[v].frequency;
  }

  kilo = freq / 1000;
  hertz = freq - 1000 * kilo;
  snprintf(msg, sizeof(msg), "Frequency %ld.%03d kilo hertz", kilo, hertz);
  tts_send(msg);
}

//
// tts_mode: form a string reporting the current mode, and
//           send it via tts_send
//
void tts_mode() {
  int v = active_receiver->id;
  int m = vfo[v].mode;

  switch (m) {
  case modeLSB:
    tts_send("Mode L S B");
    break;

  case modeUSB:
    tts_send("Mode U S B");
    break;

  case modeDSB:
    tts_send("Mode D S B");
    break;

  case modeCWL:
    tts_send("Mode C W L");
    break;

  case modeCWU:
    tts_send("Mode C W U");
    break;

  case modeFMN:
    tts_send("Mode F M");
    break;

  case modeAM:
    tts_send("Mode A M");
    break;

  case modeDIGU:
    tts_send("Mode U S B digital");
    break;

  case modeDIGL:
    tts_send("Mode L S B digital");
    break;
  }
}

//
// tts_filter: form a string reporting the filter width, and send
//             it via tts_send
//
void tts_filter() {
  char msg[128];
  int w = active_receiver->filter_high - active_receiver->filter_low;
  snprintf(msg, sizeof(msg), "Filter width %d Hertz", w);
  tts_send(msg);
}

//
// tts_smeter: form a string reporting the S-meter, and send it
//             via tts_send
//             NOTE: no averaging performed here!
//
void tts_smeter() {
  int s;
  int plus;
  int val = (int) active_receiver->meter;
  char msg[128];

  if (vfo[active_receiver->id].frequency > 30000000LL) {
    val += 20;
  }

  plus = 0;
  s = (val + 127) / 6;

  if (s < 0) {
    s = 0;
  }

  if (s > 9) {
    s = 9;
    // for "plus" only use 10, 20, ...
    plus = 10 * (( val + 73) / 10);
  }

  if (plus <= 0) {
    snprintf(msg, sizeof(msg), "Meter S %d", s);
  } else {
    snprintf(msg, sizeof(msg), "Meter S %d plus %d", s, plus);
  }

  tts_send(msg);
}

//
// tts_txdrive: report value of TX drive slider
//
void tts_txdrive() {
  if (can_transmit) {
    char msg[128];
    snprintf(msg, sizeof(msg), "T X drive %d", transmitter->drive);
    tts_send(msg);
  }
}

//
// tts_atten: report preamp or attenuator value
//
void tts_atten() {
  char msg[128];
  int level = adc[active_receiver->adc].attenuation - adc[active_receiver->adc].gain;

  if (filter_board == CHARLY25 && active_receiver->adc == 0) {
    level += 12 * active_receiver->alex_attenuation
             - 18 * active_receiver->preamp
             - 18 * active_receiver->dither;
  }

  if (filter_board == ALEX && active_receiver->adc == 0) {
    level += 10 * active_receiver->alex_attenuation;
  }

  // If level is < 0, this is gain
  if (level < 0) {
    snprintf(msg, sizeof(msg), "R F gain is %d", -level);
  } else {
    snprintf(msg, sizeof(msg), "R F attenuation is %d", level);
  }

  tts_send(msg);
}
