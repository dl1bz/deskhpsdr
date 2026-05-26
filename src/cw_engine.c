/* Copyright (C)
*  2016 Steve Wilson <wevets@gmail.com>
* 2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
*   This source code has been forked and was adapted from piHPSDR by DL1YCF to deskHPSDR in October 2024
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*/

#include <gtk/gtk.h>
#include <glib.h>
#include <unistd.h>

#include "cw_engine.h"
#include "ext.h"
#include "mode.h"
#include "radio.h"
#include "transmitter.h"
#include "vfo.h"
#include "new_protocol.h"

//
//  CW ring buffer
//

static char cw_buf[CW_ENGINE_BUF_SIZE];
static int  cw_buf_in = 0, cw_buf_out = 0;
static GThread *cw_engine_thread_id = NULL;

static int dotsamples;
static int dashsamples;

//
// send_dash()         send a "key-down" of a dashlen, followed by a "key-up" of a dotlen
// send_dot()          send a "key-down" of a dotlen,  followed by a "key-up" of a dotlen
// send_space(int len) send a "key_down" of zero,      followed by a "key-up" of len*dotlen
//
// The "trick" to get proper timing is, that we really specify  the number of samples
// for the next element (dash/dot/nothing) and the following pause. 30 wpm is no
// problem, and without too much "busy waiting". We just take a nap until 10 msec
// before we have to act, and then wait several times for 1 msec until we can shoot.
//
static void send_dash(void) {
  for (;;) {
    int TimeToGo = cw_key_up + cw_key_down;
    // TimeToGo is invalid if local CW keying has set in
    if (cw_key_hit || cw_not_ready) { return; }
    if (TimeToGo == 0) { break; }
    // sleep until 10 msec before ignition
    if (TimeToGo > 500) { usleep((long)(TimeToGo - 500) * 20L); }
    // sleep 1 msec
    usleep(1000L);
  }
  // If local CW keying has set in, do not interfere
  if (cw_key_hit || cw_not_ready) { return; }
  cw_key_down = dashsamples;
  cw_key_up   = dotsamples;
}

static void send_dot(void) {
  for (;;) {
    int TimeToGo = cw_key_up + cw_key_down;
    // TimeToGo is invalid if local CW keying has set in
    if (cw_key_hit || cw_not_ready) { return; }
    if (TimeToGo == 0) { break; }
    // sleep until 10 msec before ignition
    if (TimeToGo > 500) { usleep((long)(TimeToGo - 500) * 20L); }
    // sleep 1 msec
    usleep(1000L);
  }
  // If local CW keying has set in, do not interfere
  if (cw_key_hit || cw_not_ready) { return; }
  cw_key_down = dotsamples;
  cw_key_up   = dotsamples;
}

static void send_space(int len) {
  for (;;) {
    int TimeToGo = cw_key_up + cw_key_down;
    // TimeToGo is invalid if local CW keying has set in
    if (cw_key_hit || cw_not_ready) { return; }
    if (TimeToGo == 0) { break; }
    // sleep until 10 msec before ignition
    if (TimeToGo > 500) { usleep((long)(TimeToGo - 500) * 20L); }
    // sleep 1 msec
    usleep(1000L);
  }
  // If local CW keying has set in, do not interfere
  if (cw_key_hit || cw_not_ready) { return; }
  cw_key_up = len * dotsamples;
}

//
// This stores the "buffered join character" status
//
static int join_cw_characters = 0;
static int cw_engine_buffered_speed = 0;
static int cw_engine_bracket_command = 0;

static void cw_engine_send_cw_char(char cw_char) {
  char pattern[9], *ptr;
  ptr = &pattern[0];
  switch (cw_char) {
  case 'a':
  case 'A':
    g_strlcpy(pattern, ".-", 9);
    break;
  case 'b':
  case 'B':
    g_strlcpy(pattern, "-...", 9);
    break;
  case 'c':
  case 'C':
    g_strlcpy(pattern, "-.-.", 9);
    break;
  case 'd':
  case 'D':
    g_strlcpy(pattern, "-..", 9);
    break;
  case 'e':
  case 'E':
    g_strlcpy(pattern, ".", 9);
    break;
  case 'f':
  case 'F':
    g_strlcpy(pattern, "..-.", 9);
    break;
  case 'g':
  case 'G':
    g_strlcpy(pattern, "--.", 9);
    break;
  case 'h':
  case 'H':
    g_strlcpy(pattern, "....", 9);
    break;
  case 'i':
  case 'I':
    g_strlcpy(pattern, "..", 9);
    break;
  case 'j':
  case 'J':
    g_strlcpy(pattern, ".---", 9);
    break;
  case 'k':
  case 'K':
    g_strlcpy(pattern, "-.-", 9);
    break;
  case 'l':
  case 'L':
    g_strlcpy(pattern, ".-..", 9);
    break;
  case 'm':
  case 'M':
    g_strlcpy(pattern, "--", 9);
    break;
  case 'n':
  case 'N':
    g_strlcpy(pattern, "-.", 9);
    break;
  case 'o':
  case 'O':
    g_strlcpy(pattern, "---", 9);
    break;
  case 'p':
  case 'P':
    g_strlcpy(pattern, ".--.", 9);
    break;
  case 'q':
  case 'Q':
    g_strlcpy(pattern, "--.-", 9);
    break;
  case 'r':
  case 'R':
    g_strlcpy(pattern, ".-.", 9);
    break;
  case 's':
  case 'S':
    g_strlcpy(pattern, "...", 9);
    break;
  case 't':
  case 'T':
    g_strlcpy(pattern, "-", 9);
    break;
  case 'u':
  case 'U':
    g_strlcpy(pattern, "..-", 9);
    break;
  case 'v':
  case 'V':
    g_strlcpy(pattern, "...-", 9);
    break;
  case 'w':
  case 'W':
    g_strlcpy(pattern, ".--", 9);
    break;
  case 'x':
  case 'X':
    g_strlcpy(pattern, "-..-", 9);
    break;
  case 'y':
  case 'Y':
    g_strlcpy(pattern, "-.--", 9);
    break;
  case 'z':
  case 'Z':
    g_strlcpy(pattern, "--..", 9);
    break;
  case '0':
    g_strlcpy(pattern, "-----", 9);
    break;
  case '1':
    g_strlcpy(pattern, ".----", 9);
    break;
  case '2':
    g_strlcpy(pattern, "..---", 9);
    break;
  case '3':
    g_strlcpy(pattern, "...--", 9);
    break;
  case '4':
    g_strlcpy(pattern, "....-", 9);
    break;
  case '5':
    g_strlcpy(pattern, ".....", 9);
    break;
  case '6':
    g_strlcpy(pattern, "-....", 9);
    break;
  case '7':
    g_strlcpy(pattern, "--...", 9);
    break;
  case '8':
    g_strlcpy(pattern, "---..", 9);
    break;
  case '9':
    g_strlcpy(pattern, "----.", 9);
    break;
  //
  //     DL1YCF:
  //     added some signs from ITU Recommendation M.1677-1 (2009)
  //     in the order given there.
  //
  case '.':
    g_strlcpy(pattern, ".-.-.-", 9);
    break;
  case ',':
    g_strlcpy(pattern, "--..--", 9);
    break;
  case ':':
    g_strlcpy(pattern, "---..", 9);
    break;
  case '?':
    g_strlcpy(pattern, "..--..", 9);
    break;
  case '\'':
    g_strlcpy(pattern, ".----.", 9);
    break;
  case '-':
    g_strlcpy(pattern, "-....-", 9);
    break;
  case '/':
    g_strlcpy(pattern, "-..-.", 9);
    break;
  case '(':
    g_strlcpy(pattern, "-.--.", 9);
    break;
  case ')':
    g_strlcpy(pattern, "-.--.-", 9);
    break;
  case '"':
    g_strlcpy(pattern, ".-..-.", 9);
    break;
  case '=':
    g_strlcpy(pattern, "-...-", 9);
    break;
  case '+':
    g_strlcpy(pattern, ".-.-.", 9);
    break;
  case '@':
    g_strlcpy(pattern, ".--.-.", 9);
    break;
  //
  //     Often used, but not ITU: Ampersand for "wait"
  //
  case '&':
    g_strlcpy(pattern, ".-...", 9);
    break;
  default:
    g_strlcpy(pattern, "", 9);
  }
  while (*ptr != '\0') {
    if (*ptr == '-') {
      send_dash();
    }
    if (*ptr == '.') {
      send_dot();
    }
    ptr++;
  }
  // The last element (dash or dot) sent already has one dotlen space appended.
  // If the current character is another "printable" sign, we need an additional
  // pause of 2 dotlens to form the inter-character spacing of 3 dotlens.
  // However if the current character is a "space" we must produce an inter-word
  // spacing (7 dotlens) and therefore need 6 additional dotlens
  // We need no longer take care of a sequence of spaces since adjacent spaces
  // are now filtered out while filling the CW character (ring-) buffer.
  if (cw_char == ' ') {
    send_space(6);  // produce inter-word space of 7 dotlens
  } else {
    if (!join_cw_characters) { send_space(2); }  // produce inter-character space of 3 dotlens
  }
}

//
// cw_engine_thread is started once and runs forever,
// checking for data in the CW ring buffer and sending it.
//
static gpointer cw_engine_thread(gpointer data) {
  int i;
  char cwchar;
  for (;;) {
    // wait for CW data (periodically look every 100 msec)
    if (cw_buf_in == cw_buf_out) {
      cw_key_hit = 0;
      usleep(100000L);
      continue;
    }
    //
    // if TX mode is not CW, drain ring buffer
    //
    int txmode = vfo_get_tx_mode();
    if (txmode != modeCWU && txmode != modeCWL) {
      cw_buf_out = cw_buf_in;
      continue;
    }
    //
    // Take one character from the ring buffer
    //
    cwchar = cw_buf[cw_buf_out];
    i = cw_buf_out + 1;
    if (i >= CW_ENGINE_BUF_SIZE) { i = 0; }
    cw_buf_out = i;
    //
    // Special character sequences or characters:
    //
    //  [+         Increase speed by 25 %
    //  [-         Decrease speed by 25 %
    //  [          Join Characters
    //  ]          End speed change or joining
    //
    if (cw_engine_bracket_command)  {
      switch (cwchar) {
      case '+':
        cw_engine_buffered_speed = (5 * cw_keyer_speed) / 4;
        cwchar = 0;
        break;
      case '-':
        cw_engine_buffered_speed = (3 * cw_keyer_speed) / 4;
        cwchar = 0;
        break;
      case '.':
        join_cw_characters = 1;
        cwchar = 0;
        break;
      }
      cw_engine_bracket_command = 0;
    }
    if (cwchar == '[') {
      cw_engine_bracket_command = 1;
      cwchar = 0;
    }
    if (cwchar == ']') {
      cw_engine_buffered_speed = 0;
      join_cw_characters = 0;
      cwchar = 0;
    }
    // The dot and dash length may have changed, so recompute them here
    // This means that we can change the speed (KS command) while
    // the buffer is being sent
    if (cw_engine_buffered_speed > 0) {
      dotsamples = 57600 / cw_engine_buffered_speed;
      dashsamples = (3456 * cw_keyer_weight) / cw_engine_buffered_speed;
    } else {
      dotsamples = 57600 / cw_keyer_speed;
      dashsamples = (3456 * cw_keyer_weight) / cw_keyer_speed;
    }
    CAT_cw_is_active = 1;
    schedule_transmit_specific();
    if (!mox) {
      // activate PTT
      g_idle_add(ext_mox_update, GINT_TO_POINTER(1));
      // have to wait until it is really there
      // Note that if out-of-band, we would wait
      // forever here, so allow at most 200 msec
      // We also have to wait for cw_not_ready becoming zero
      i = 200;
      while ((!mox || cw_not_ready) && i-- > 0) { usleep(1000L); }
      // still no MOX? --> silently discard CW character and give up
      if (!mox) {
        CAT_cw_is_active = 0;
        schedule_transmit_specific();
        continue;
      }
    }
    // At this point, mox == 1 and CAT_cw_active == 1
    if (cw_key_hit || cw_not_ready) {
      //
      // CW transmission has been aborted, either due to manually
      // removing MOX, changing the mode to non-CW, or because a CW key has been hit.
      // Do not remove PTT in the latter case
      cw_engine_buffered_speed = 0;
      CAT_cw_is_active = 0;
      schedule_transmit_specific();
      // If a CW key has been hit, we continue in TX mode.
      // This also applies if we have an active foot-switch
      // Otherwise, switch PTT off.
      if (!cw_key_hit && mox && !radio_ptt) {
        g_idle_add(ext_mox_update, GINT_TO_POINTER(0));
      }
      //
      // keep draining ring buffer until it stays empty for 0.5 seconds
      // This is necessary: after aborting a very long CW
      // text such as a CQ call by hitting a Morse key,
      // CW characters may flow in for quite a while.
      //
      do {
        cw_buf_out = cw_buf_in;
        usleep(500000L);
      } while (cw_buf_out != cw_buf_in);
    } else {
      if (cwchar) { cw_engine_send_cw_char(cwchar); }
      if (cw_key_hit || cw_not_ready) {
        cw_engine_buffered_speed = 0;
        join_cw_characters = 0;
        cw_engine_clear();
        CAT_cw_is_active = 0;
        schedule_transmit_specific();
        if (!cw_key_hit && mox && !radio_ptt) {
          g_idle_add(ext_mox_update, GINT_TO_POINTER(0));
        }
        continue;
      }
      //
      // Character has been sent, so continue.
      // Since the second character possibly comes 250 msec after
      // the first one, we have to wait if the buffer stays
      // empty. Only then, stop CAT CW.
      //
      for (i = 0; i < 5; i++) {
        if (cw_buf_in != cw_buf_out) { break; }
        usleep(50000);
      }
      if (cw_buf_in != cw_buf_out) { continue; }
      CAT_cw_is_active = 0;
      schedule_transmit_specific();
      if (!cw_key_hit && !radio_ptt) {
        g_idle_add(ext_mox_update, GINT_TO_POINTER(0));
        // wait up to 500 msec for MOX having gone
        // otherwise there might be a race condition when sending
        // the next character really soon
        i = 10;
        while (mox && (i--) > 0) { usleep(50000L); }
      }
    }
  }
  // NOTREACHED (now this thread is started once-and-for-all)
  // We arrive here if the rigctl server shuts down.
  // This very rarely happens. But we should shut down the
  // local CW system gracefully, in case we were in the mid
  // of a transmission
  if (CAT_cw_is_active) {
    CAT_cw_is_active = 0;
    schedule_transmit_specific();
    g_idle_add(ext_mox_update, GINT_TO_POINTER(0));
  }
  cw_engine_thread_id = NULL;
  return NULL;
}


void cw_engine_start_thread(void) {
  if (!cw_engine_thread_id) {
    cw_engine_thread_id = g_thread_new("CW engine", cw_engine_thread, NULL);
  }
}

void cw_engine_clear(void) {
  cw_buf_in = 0;
  cw_buf_out = 0;
  cw_engine_buffered_speed = 0;
  cw_engine_bracket_command = 0;
  join_cw_characters = 0;
}

int cw_engine_buffer_used(void) {
  int used = cw_buf_in - cw_buf_out;
  if (used < 0) { used += CW_ENGINE_BUF_SIZE; }
  return used;
}

int cw_engine_queue_char(char c) {
  int new = cw_buf_in + 1;
  if (new >= CW_ENGINE_BUF_SIZE) { new = 0; }
  if (new == cw_buf_out) {
    return 0;
  }
  cw_buf[cw_buf_in] = c;
  cw_buf_in = new;
  return 1;
}

int cw_engine_queue_text(const char *text) {
  int queued = 0;
  if (text == NULL) {
    return 0;
  }
  while (*text != '\0') {
    if (!cw_engine_queue_char(*text++)) {
      break;
    }
    queued++;
  }
  return queued;
}

