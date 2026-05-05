/* Copyright (C)
* 2024 - Christoph van W"ullen, DL1YCF
* 2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
*   This source code has been forked and was adapted from piHPSDR by DL1YCF to deskHPSDR in October 2024
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
// Minimal stripped-down TCI server for use with logbook programs
// and possibly PAs. This is built upon  a "light-weight" websocket server.
//

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <ctype.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <stdbool.h>

#ifdef __APPLE__
  #include <time.h>
#endif

#include <openssl/sha.h>
#include <openssl/evp.h>

#include "radio.h"
#include "vfo.h"
#include "rigctl.h"
#include "ext.h"
#include "message.h"
#include "toolset.h"
#include "main.h"

#define MAX_TCI_CLIENTS 5
#define MAXDATASIZE     1024
#define MAXMSGSIZE      512
#define TCI_MAX_ARGS 16

int tci_enable = 0;
int tci_port   = 50001;
int tci_txonly = 0;
long tci_timer = 0;

//
// OpCodes for WebSocket frames
//
enum OpCode {
  opCONT  = 0,
  opTEXT  = 1,
  opBIN   = 2,
  opCLOSE = 8,
  opPING  = 9,
  opPONG  = 10
};

static GThread *tci_server_thread_id = NULL;
static int tci_running = 0;

static int server_socket = -1;
static struct sockaddr_in server_address;

typedef struct _client {
  int seq;                      // Seq. number of the client
  int fd;                       // socket
  int running;                  // set this to zero to close client connection
  guint tci_timer;              // GTK id  of the periodic task
  socklen_t address_length;     // unused
  struct sockaddr_in address;   // unused
  GThread *thread_id;           // thread id of receiving thread
  long long last_fa;            // last VFO-A  freq reported
  long long last_fb;            // last VFO-B  freq reported
  long long last_fx;            // last TX     freq reported
  int last_ma;                  // last VFO-A  mode reported
  int last_mb;                  // last VFO-B  mode reported
  int last_split;               // last split state reported
  int last_mox;                 // last mox   state reported
  int count;                    // ping counter
  int rxsensor;                 // enable transmit of S meter data
  int txsensor;                 // enable transmit of drive data
  int idle_queued;              // counter
} CLIENT;

typedef struct _response {
  CLIENT *client;
  int     type;
  char    msg[MAXMSGSIZE];
} RESPONSE;

static CLIENT tci_client[MAX_TCI_CLIENTS];

static GMutex tci_mutex;

static gpointer tci_server(gpointer data);
static gpointer tci_listener(gpointer data);

typedef struct {
  char *cmd;
  int argc;
  char *argv[TCI_MAX_ARGS];
} TCI_CMD;

typedef void (*TCI_HANDLER)(CLIENT *client, const TCI_CMD *cmd);

typedef struct {
  const char *name;
  int min_args;
  int max_args;   // -1 = unlimited
  TCI_HANDLER handler;
} TCI_DISPATCH;

static void tci_handle_text(CLIENT *client, char *msg);
static const TCI_DISPATCH tci_dispatch[];

static void tci_send_smeter(CLIENT *client, int v);

//
// Launch TCI system. Called upon program start if TCI is
// enabled in the props file, and from the CAT/TCI menu
// if TCI is enabled there.
//
void launch_tci(void) {
  t_print( "---- LAUNCHING TCI SERVER ----\n");
  tci_running = 1;
  //
  // Start TCI server
  //
  tci_server_thread_id = g_thread_new( "tci server", tci_server, GINT_TO_POINTER(tci_port));
}

//
// This enforces closing a "listener" connection even if it "hangs"
// and removes the autoreporting task. Do not  join with the listener
// so this may be called from  within the listener.
//
static void force_close(CLIENT *client) {
  struct linger linger = { 0 };
  linger.l_onoff = 1;
  linger.l_linger = 0;
  g_mutex_lock(&tci_mutex);
  client->running = 0;

  if (client->fd  != -1) {
    // No error checking since the socket may have been close in a race condition
    // in the listener
    setsockopt(client->fd, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger));
    close(client->fd);
    client->fd = -1;
  }

  if (client->tci_timer != 0) {
    g_source_remove(client->tci_timer);
    client->tci_timer = 0;
  }

  g_mutex_unlock(&tci_mutex);
}

//
// Shut down TCI system. Called from CAT/TCI menu
// if TCI is disabled there.
//
void shutdown_tci(void) {
  t_print("%s: server_socket=%d\n", __func__, server_socket);
  tci_running = 0;

  //
  // Terminate all active TCI connections and join with listener threads
  // Joining is temporarily disabled until we know why it sometimes hangs
  //
  for (int id = 0; id < MAX_TCI_CLIENTS; id++) {
    force_close(&tci_client[id]);
    usleep(100000); // let the client terminate if it can

    if (tci_client[id].thread_id) {
      //g_thread_join(tci_client[id].thread_id);
      tci_client[id].thread_id = NULL;
    }
  }

  usleep(100000);  // Let the TCI thread terminate, if it can

  //
  // Forced close of server socket, and join with TCI thread
  //
  if (server_socket >= 0) {
    struct linger linger = { 0 };
    linger.l_onoff = 1;
    linger.l_linger = 0;
    // No error checking since the socket may be closed
    // in a race condition by the server thread
    setsockopt(server_socket, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger));
    close(server_socket);
    server_socket = -1;
  }

  if (tci_server_thread_id) {
    //g_thread_join(tci_server_thread_id);
    tci_server_thread_id = NULL;
  }
}

//
// tci_send_frame is intended to  be called  through the GTK idle queue
//
static int tci_transport_write(CLIENT *client, const void *buf, size_t len) {
  if (client == NULL || buf == NULL || len == 0) { return 0; }

  return write(client->fd, buf, len);
}

static int tci_transport_read(CLIENT *client, void *buf, size_t len) {
  if (client == NULL || buf == NULL || len == 0) { return 0; }

  return recv(client->fd, buf, len, 0);
}

static int tci_encode_ws_frame(unsigned char *frame, size_t frame_size, int type, const char *msg, size_t *frame_len) {
  size_t payload_len = msg ? strlen(msg) : 0;
  size_t start;

  if (frame == NULL || frame_len == NULL) { return 0; }

  if (payload_len <= 125) {
    start = 2;
  } else {
    start = 4;
  }

  if (payload_len + start > frame_size) { return 0; }

  frame[0] = 128 | type;

  if (payload_len <= 125) {
    frame[1] = payload_len;
  } else {
    frame[1] = 126;
    frame[2] = (payload_len >> 8) & 255;
    frame[3] = payload_len & 255;
  }

  memcpy(frame + start, msg ? msg : "", payload_len);
  *frame_len = payload_len + start;
  return 1;
}

static int tci_write_ws_frame(CLIENT *client, int type, const char *msg) {
  unsigned char frame[1024];
  unsigned char *p;
  size_t length;

  if (client == NULL) { return 0; }

  g_mutex_lock(&tci_mutex);

  if (client->fd < 0) {
    g_mutex_unlock(&tci_mutex);
    return 0;
  }

  g_mutex_unlock(&tci_mutex);

  if (!tci_encode_ws_frame(frame, sizeof(frame), type, msg, &length)) { return 0; }

  int count = 0;
  p = frame;

  while (length > 0) {
    int rc = tci_transport_write(client, p, length);

    if (rc < 0) {
      if (errno == EINTR) { continue; }

      g_mutex_lock(&tci_mutex);
      client->running = 0;
      g_mutex_unlock(&tci_mutex);
      return 0;
    }

    if (rc == 0) {
      count++;

      if (count > 10) {
        g_mutex_lock(&tci_mutex);
        client->running = 0;
        g_mutex_unlock(&tci_mutex);
        return 0;
      }
    }

    length -= rc;
    p += rc;
  }

  return 1;
}

static int tci_send_frame(void *data) {
  RESPONSE *response = (RESPONSE *) data;

  if (!response || !response->client) {
    g_free(response);
    return G_SOURCE_REMOVE;
  }

  CLIENT *client = response->client;
  int type = response->type;
  const char *msg = response->msg;
  (void)tci_write_ws_frame(client, type, msg);
  g_mutex_lock(&tci_mutex);
  client->idle_queued--;
  g_mutex_unlock(&tci_mutex);
  g_free(response);
  return G_SOURCE_REMOVE;
}

static int tci_queue_frame(CLIENT *client, int type, const char *msg, int check_running) {
  RESPONSE *resp;

  if (client == NULL) { return 0; }

  if (check_running && !client->running) { return 0; }

  resp = g_new(RESPONSE, 1);
  resp->client = client;
  resp->type = type;

  if (msg != NULL) {
    g_strlcpy(resp->msg, msg, MAXMSGSIZE);
  } else {
    resp->msg[0] = 0;
  }

  g_mutex_lock(&tci_mutex);

  if (type == opTEXT && client->idle_queued >= 100) {
    g_mutex_unlock(&tci_mutex);
    g_free(resp);
    return 0;
  }

  client->idle_queued++;
  g_mutex_unlock(&tci_mutex);
  g_idle_add(tci_send_frame, resp);
  return 1;
}

static void tci_send_text(CLIENT *client, const char *msg) {
  if (rigctl_debug && client != NULL) { t_print("TCI%d response: %s\n", client->seq, msg ? msg : "(null)"); }

  (void)tci_queue_frame(client, opTEXT, msg, 1);
}

//
// To keep things  simple, tci_send_dds does not report
// the center frequency but the "real" RX frequency
//
static void tci_send_dds(CLIENT *client, int v) {
  long long f;
  char msg[MAXMSGSIZE];

  if (v < 0 || v > 1) { return; }

  f = vfo[v].ctun ? vfo[v].ctun_frequency : vfo[v].frequency;
  snprintf(msg, MAXMSGSIZE, "dds:%d,%lld;", v, f);
  tci_send_text(client, msg);
}

static void tci_send_mox(CLIENT *client) {
  if (radio_is_transmitting()) {
    tci_send_text(client, "trx:0,true;");
    client->last_mox = 1;
  } else {
    tci_send_text(client, "trx:0,false;");
    client->last_mox = 0;
  }
}

//
// There are four (!) frequencies to report, namely for RX0/1 channel0/1.
// RX=0 channel=0: reports VFO-A frequency, all other combination report VFO-B
//
// Thus logbook programs correctly display both frequencies no matter whether
// they  use RX0/channel0:RX0/channel1 or RX0/channel0:RX1/channel0
//
static void tci_send_vfo(CLIENT *client, int v, int c) {
  long long f;
  char msg[MAXMSGSIZE];

  if (v < 0 || v > 1) { return; }

  if (c < 0 || c > 1) { return; }

  if (v  == VFO_A && c == 0) {
    f = vfo[VFO_A].ctun ? vfo[VFO_A].ctun_frequency : vfo[VFO_A].frequency;
    client->last_fa = f;
  } else {
    f = vfo[VFO_B].ctun ? vfo[VFO_B].ctun_frequency : vfo[VFO_B].frequency;
    client->last_fb = f;
  }

  snprintf(msg, MAXMSGSIZE, "vfo:%d,%d,%lld;", v, c, f);
  tci_send_text(client, msg);
}

static void tci_set_vfo(CLIENT *client, int VfoNr, int Ch, long long SetFreq) {
  if (VfoNr < 0 || VfoNr > 1) { return; }

  if (Ch < 0 || Ch > 1) { return; }

  if (VfoNr  == VFO_A) {
    vfo_set_frequency(VFO_A, SetFreq);
    client->last_fa = SetFreq;
    g_idle_add(ext_vfo_update, NULL);
  } else {
    vfo_set_frequency(VFO_B, SetFreq);
    client->last_fb = SetFreq;
    g_idle_add(ext_vfo_update, NULL);
  }

  tci_send_vfo(client, VfoNr, Ch);
}

static void tci_send_limits(CLIENT *client, int v) {
  char msg[MAXMSGSIZE];
  char* maxQRG;
  int Dec, sign;
  int ndig = 1;
  maxQRG = fcvt(discovered[0].frequency_max / 10, ndig, &Dec, &sign);
  snprintf(msg, MAXMSGSIZE, "vfo_limits:%d,%s;", v, maxQRG);
  tci_send_text(client, msg);
  snprintf(msg, MAXMSGSIZE, "if_limits:-%lld,%lld;", (long long)receiver[v]->sample_rate / 2,
           (long long)receiver[v]->sample_rate / 2);
  tci_send_text(client, msg);
}

static void tci_send_drive(CLIENT *client, int v) {
  char msg[MAXMSGSIZE];
  int tx_drive;
  tx_drive = radio_get_drive_as_int();

  if (v < 0 || v > 1) { return; }

  snprintf(msg, MAXMSGSIZE, "drive:%d,%d;", v, tx_drive);
  tci_send_text(client, msg);
}

static void tci_send_split(CLIENT *client) {
  //
  // send "true" if tx is on VFO-B frequency
  //
  if (vfo_get_tx_vfo() == VFO_A) {
    tci_send_text(client, "split_enable:0,false;");
    client->last_split = 0;
  } else {
    tci_send_text(client, "split_enable:0,true;");
    client->last_split = 1;
  }
}

static void tci_send_txfreq(CLIENT *client) {
  char msg[MAXMSGSIZE];
  long long f = vfo_get_tx_freq();
  snprintf(msg, MAXMSGSIZE, "tx_frequency:%lld;", f);
  tci_send_text(client, msg);
  client->last_fx = f;
}

static void tci_send_mode(CLIENT *client, int v) {
  int m;
  const char *mode;
  char msg[MAXMSGSIZE];

  if (v < 0 || v > 1) { return; }

  m = vfo[v].mode;

  switch (m) {
  case modeLSB:
    mode = "LSB";
    break;

  case modeUSB:
    mode = "USB";
    break;

  case modeDSB:
    mode = "DSB";
    break;

  case modeCWL:
  case modeCWU:
    mode = "CW";
    break;

  case modeFMN:
    mode = "FM";
    break;

  case modeAM:
    mode = "AM";
    break;

  case modeDIGU:
    mode = "DIGU";
    break;

  case modeSPEC:
    mode = "SPEC";
    break;

  case modeDIGL:
    mode = "DIGL";
    break;

  case modeSAM:
    mode = "SAM";
    break;

  case modeDRM:
    mode = "DRM";
    break;

  default:   // should not happen
    mode = "USB";
    break;
  }

  snprintf(msg, MAXMSGSIZE, "modulation:%d,%s;", v, mode);
  tci_send_text(client, msg);

  if (v == 0) {
    client->last_ma = m;
  } else {
    client->last_mb = m;
  }
}

static int tci_parse_mode(const char *mode_str) {
  if (mode_str == NULL) { return -1; }

  if (!g_ascii_strcasecmp(mode_str, "lsb"))  { return modeLSB; }

  if (!g_ascii_strcasecmp(mode_str, "usb"))  { return modeUSB; }

  if (!g_ascii_strcasecmp(mode_str, "dsb"))  { return modeDSB; }

  if (!g_ascii_strcasecmp(mode_str, "cw"))   { return modeCWU; }

  if (!g_ascii_strcasecmp(mode_str, "cwl"))  { return modeCWL; }

  if (!g_ascii_strcasecmp(mode_str, "cwu"))  { return modeCWU; }

  if (!g_ascii_strcasecmp(mode_str, "fmn"))  { return modeFMN; }

  if (!g_ascii_strcasecmp(mode_str, "fm"))   { return modeFMN; }

  if (!g_ascii_strcasecmp(mode_str, "am"))   { return modeAM; }

  if (!g_ascii_strcasecmp(mode_str, "digu")) { return modeDIGU; }

  if (!g_ascii_strcasecmp(mode_str, "spec")) { return modeSPEC; }

  if (!g_ascii_strcasecmp(mode_str, "digl")) { return modeDIGL; }

  if (!g_ascii_strcasecmp(mode_str, "sam"))  { return modeSAM; }

  if (!g_ascii_strcasecmp(mode_str, "drm"))  { return modeDRM; }

  return -1;
}

typedef struct {
  int vfo_id;
  int mode;
} TCI_MODE_CHANGE;

static int tci_mode_change_cb(void *data) {
  TCI_MODE_CHANGE *mc = (TCI_MODE_CHANGE *)data;
  vfo_id_mode_changed(mc->vfo_id, mc->mode);
  g_free(mc);
  return G_SOURCE_REMOVE;
}

static void tci_set_mode(CLIENT *client, int VfoNr, const char *mode_str) {
  if (VfoNr < 0 || VfoNr > 1) { return; }

  int m = tci_parse_mode(mode_str);

  if (m < 0) {
    t_print("TCI%d unknown mode: %s\n", client->seq, mode_str);
    tci_send_mode(client, VfoNr);
    return;
  }

  TCI_MODE_CHANGE *mc = g_new(TCI_MODE_CHANGE, 1);
  mc->vfo_id = VfoNr;
  mc->mode = m;
  g_idle_add(tci_mode_change_cb, mc);
  tci_send_mode(client, VfoNr);
}

static void tci_send_trx_count(CLIENT *client) {
  tci_send_text(client, "trx_count:2;");
}

static void tci_send_macros_cwspeed(CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf(msg, MAXMSGSIZE, "cw_macros_speed:%d;", cw_keyer_speed);
  tci_send_text(client, msg);
}

static void tci_send_keyer_cwspeed(CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf(msg, MAXMSGSIZE, "cw_keyer_speed:%d;", cw_keyer_speed);
  tci_send_text(client, msg);
}


static int tci_parse_text(char *s, TCI_CMD *c) {
  int argc = 0;

  if (s == NULL || c == NULL) { return -1; }

  memset(c, 0, sizeof(*c));
  char *end = strchr(s, ';');

  if (end != NULL) { *end = 0; }

  c->cmd = s;
  char *p = strchr(s, ':');

  if (p == NULL) { return 0; }

  *p++ = 0;

  while (argc < TCI_MAX_ARGS) {
    c->argv[argc++] = p;
    p = strchr(p, ',');

    if (p == NULL) { break; }

    *p++ = 0;
  }

  c->argc = argc;
  return 0;
}

static int tci_bool(const char *s) {
  return s != NULL && (*s == '1' || !g_ascii_strcasecmp(s, "true"));
}

static int tci_int(const char *s, int def) {
  return s != NULL ? atoi(s) : def;
}

static long long tci_ll(const char *s, long long def) {
  return s != NULL ? atoll(s) : def;
}

static void tci_cmd_trx_count(CLIENT *client, const TCI_CMD *cmd) {
  (void)cmd;
  tci_send_trx_count(client);
}

static void tci_cmd_trx(CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 2) {
    if (tci_bool(cmd->argv[1])) {
#if defined (__HAVEATU__)

      if (transmitter->is_tuned) {
        g_idle_add(ext_mox_update, GINT_TO_POINTER(1));
        t_print("TCI%d TX request valid - TX is tuned\n", client->seq);
      } else {
        tci_send_mox(client);
        t_print("TCI%d TX request invalid - TX not tuned\n", client->seq);
        show_NOTUNE_dialog(GTK_WINDOW(top_window));
      }

#else
      g_idle_add(ext_mox_update, GINT_TO_POINTER(1));
      t_print("TCI%d TX request\n", client->seq);
#endif
    } else {
      g_timeout_add(50, ext_mox_update, GINT_TO_POINTER(0));
      t_print("TCI%d RX request\n", client->seq);
    }
  } else {
    tci_send_mox(client);
  }
}

static void tci_cmd_rx_sensors_enable(CLIENT *client, const TCI_CMD *cmd) {
  g_mutex_lock(&tci_mutex);
  client->rxsensor = tci_bool(cmd->argv[0]);
  g_mutex_unlock(&tci_mutex);
}

static void tci_cmd_tx_sensors_enable(CLIENT *client, const TCI_CMD *cmd) {
  g_mutex_lock(&tci_mutex);
  client->txsensor = tci_bool(cmd->argv[0]);
  g_mutex_unlock(&tci_mutex);
}

static void tci_cmd_modulation(CLIENT *client, const TCI_CMD *cmd) {
  int VfoNr = tci_int(cmd->argv[0], 0);

  if (cmd->argc >= 2) {
    tci_set_mode(client, VfoNr, cmd->argv[1]);
  } else {
    tci_send_mode(client, VfoNr);
  }
}

static void tci_cmd_vfo(CLIENT *client, const TCI_CMD *cmd) {
  int VfoNr = tci_int(cmd->argv[0], 0);
  int Ch = tci_int(cmd->argv[1], 0);

  if (cmd->argc >= 3) {
    tci_set_vfo(client, VfoNr, Ch, tci_ll(cmd->argv[2], 0));
  } else {
    tci_send_vfo(client, VfoNr, Ch);
  }
}

static void tci_cmd_rx_smeter(CLIENT *client, const TCI_CMD *cmd) {
  tci_send_smeter(client, tci_int(cmd->argv[0], 0));
}

static void tci_cmd_drive(CLIENT *client, const TCI_CMD *cmd) {
  tci_send_drive(client, tci_int(cmd->argv[0], 0));
}

static void tci_cmd_cw_macros_speed(CLIENT *client, const TCI_CMD *cmd) {
  (void)cmd;
  tci_send_macros_cwspeed(client);
}

static void tci_cmd_cw_keyer_speed(CLIENT *client, const TCI_CMD *cmd) {
  (void)cmd;
  tci_send_keyer_cwspeed(client);
}

static void tci_cmd_cw_macros_delay(CLIENT *client, const TCI_CMD *cmd) {
  (void)cmd;
  tci_send_text(client, "cw_macros_delay:10;");
}

static void tci_cmd_stop(CLIENT *client, const TCI_CMD *cmd) {
  (void)cmd;
  client->rxsensor = 0;
  client->txsensor = 0;
  tci_send_text(client, "stop;");
  g_mutex_lock(&tci_mutex);
  client->running = 0;
  g_mutex_unlock(&tci_mutex);
}

static const TCI_DISPATCH tci_dispatch[] = {
  { "trx_count",         0,  0, tci_cmd_trx_count },
  { "trx",               0, -1, tci_cmd_trx },
  { "rx_sensors_enable", 1,  2, tci_cmd_rx_sensors_enable },
  { "tx_sensors_enable", 1,  2, tci_cmd_tx_sensors_enable },
  { "modulation",        1,  2, tci_cmd_modulation },
  { "vfo",               2,  3, tci_cmd_vfo },
  { "rx_smeter",         1,  3, tci_cmd_rx_smeter },
  { "drive",             1,  1, tci_cmd_drive },
  { "cw_macros_speed",   0,  0, tci_cmd_cw_macros_speed },
  { "cw_keyer_speed",    0,  0, tci_cmd_cw_keyer_speed },
  { "cw_macros_delay",   0,  0, tci_cmd_cw_macros_delay },
  { "stop",              0,  0, tci_cmd_stop },
  { NULL,                0,  0, NULL }
};

static void tci_handle_text(CLIENT *client, char *msg) {
  TCI_CMD cmd;

  if (tci_parse_text(msg, &cmd) < 0 || cmd.cmd == NULL) { return; }

  for (char *p = cmd.cmd; *p != 0; p++) {
    *p = g_ascii_tolower(*p);
  }

  if (rigctl_debug) {
    t_print("TCI%d command=%s argc=%d\n", client->seq, cmd.cmd, cmd.argc);

    for (int i = 0; i < cmd.argc; i++) {
      t_print("  arg[%d]=%s\n", i, cmd.argv[i] ? cmd.argv[i] : "(null)");
    }
  }

  bool handled = false;

  for (int i = 0; tci_dispatch[i].name != NULL; i++) {
    const TCI_DISPATCH *d = &tci_dispatch[i];

    if (cmd.cmd[0] != d->name[0] || strcmp(cmd.cmd, d->name) != 0) { continue; }

    handled = true;

    if (cmd.argc < d->min_args) {
      t_print("TCI%d %s: too few args (%d < %d)\n", client->seq, d->name, cmd.argc, d->min_args);
      return;
    }

    if (d->max_args >= 0 && cmd.argc > d->max_args) {
      t_print("TCI%d %s: too many args (%d > %d)\n", client->seq, d->name, cmd.argc, d->max_args);
      return;
    }

    d->handler(client, &cmd);
    return;
  }

  if (!handled && rigctl_debug) {
    t_print("TCI%d unknown command: %s\n",
            client->seq, cmd.cmd ? cmd.cmd : "(null)");
  }
}

static void tci_send_smeter(CLIENT *client, int v) {
  //
  // UNDOCUMENTED in the TCI protocol, but MLDX sends this
  // ATTENTION: in some countries, %f sends a comma instead of a decimal
  //            point and this is a desaster. Therefore we fake a
  //            floating point number.
  //
  char msg[MAXMSGSIZE];
  int lvl;

  if (v < 0 || v > 1) { return; }

  if (v == 1 && receivers == 1) { return; }

  lvl = (int) (receiver[v]->meter - 0.5);
  // snprintf(msg, MAXMSGSIZE, "rx_smeter:%d,0,%d.0;",v,lvl);
  // tci_send_text(client, msg);
  // snprintf(msg, MAXMSGSIZE, "rx_smeter:%d,1,%d.0;",v,lvl);
  // tci_send_text(client, msg);
  snprintf(msg, MAXMSGSIZE, "rx_sensors:%d,%d.0;", v, lvl);
  tci_send_text(client, msg);
}

static void tci_send_rx(CLIENT *client, int v) {
  //
  // Send S-meter reading.
  // ATTENTION: in some countries, %f sends a comma instead of a decimal
  //            point and this is a desaster. Therefore we fake a
  //            floating point number.
  //
  char msg[MAXMSGSIZE];
  int lvl;

  if (v < 0 || v > 1) { return; }

  if (v == 1 && receivers == 1) { return; }

  lvl = (int) (receiver[v]->meter - 0.5);
  snprintf(msg, MAXMSGSIZE, "rx_channel_sensors:%d,0,%d.0;", v, lvl);
  tci_send_text(client, msg);
  snprintf(msg, MAXMSGSIZE, "rx_channel_sensors:%d,1,%d.0;", v, lvl);
  tci_send_text(client, msg);
}

static void tci_send_close(CLIENT *client) {
  if (rigctl_debug && client != NULL) { t_print("TCI%d CLOSE\n", client->seq); }

  (void)tci_queue_frame(client, opCLOSE, NULL, 0);
}

__attribute__((unused)) static void tci_send_ping(CLIENT *client) {
  if (rigctl_debug && client != NULL) { t_print("TCI%d PING\n", client->seq); }

  (void)tci_queue_frame(client, opPING, NULL, 0);
}

static void tci_send_pong(CLIENT *client) {
  if (rigctl_debug && client != NULL) { t_print("TCI%d PONG\n", client->seq); }

  (void)tci_queue_frame(client, opPONG, NULL, 0);
}

static gboolean tci_reporter(gpointer data) {
  //
  // This function is called repeatedly as long as the client  runs
  //
  CLIENT *client = (CLIENT *) data;
  g_mutex_lock(&tci_mutex);

  if (!client->running) {
    g_mutex_unlock(&tci_mutex);
    return FALSE;
  }

  g_mutex_unlock(&tci_mutex);
#ifdef __APPLE__
  struct timespec ts;
  // clock_gettime(CLOCK_REALTIME, &ts);
  clock_gettime(CLOCK_MONOTONIC, &ts);
  tci_timer += ts.tv_sec;
#endif

  if (++(client->count) >= 30) {
    client->count = 0;
    // tci_send_ping(client);
  }

  //
  // Determine TX frequency  and  report  if changed
  //
  long long fx = vfo_get_tx_freq();

  if (fx != client->last_fx) {
    tci_send_txfreq(client);
  }

  if (!tci_txonly) {
    //
    // If S-meter reading is requested, send info each time
    //
    if (client->rxsensor && (client->count & 1)) {
      tci_send_rx(client, 0);
      tci_send_rx(client, 1);
    }

    if (client->txsensor && (client->count & 1)) {
      tci_send_drive(client, 0);
    }

    if (receivers > 0 && client->rxsensor && (client->count & 1)) {
      if (receivers == 1) {
        tci_send_smeter(client, 0);
      } else {
        tci_send_smeter(client, 0);
        tci_send_smeter(client, 1);
      }
    }

    //
    // Determine VFO-A/B frequency/mode, report if changed
    //
    long long fa = vfo[VFO_A].ctun ? vfo[VFO_A].ctun_frequency : vfo[VFO_A].frequency;
    long long fb = vfo[VFO_B].ctun ? vfo[VFO_B].ctun_frequency : vfo[VFO_B].frequency;
    int       ma = vfo[VFO_A].mode;
    int       mb = vfo[VFO_B].mode;
    int       sp = (vfo_get_tx_vfo() == VFO_B);
    int       mx = radio_is_transmitting();

    if (fa != client->last_fa) {
      tci_send_vfo(client, 0, 0);
    }

    if (fb != client->last_fb) {
      tci_send_vfo(client, 0, 1);
      tci_send_vfo(client, 1, 0);
      tci_send_vfo(client, 1, 1);
    }

    if (ma  != client->last_ma) {
      tci_send_mode(client, 0);
    }

    if (mb  != client->last_mb) {
      tci_send_mode(client, 1);
    }

    if (sp != client->last_split) {
      tci_send_split(client);
    }

    if (mx != client->last_mox) {
      tci_send_mox(client);
    }
  }

  return TRUE;
}

//
// This is the TCI server, which listens for (and accepts) connections
//
static int tci_perform_ws_handshake(int fd) {
  char buf[MAXDATASIZE + 40];
  char key[MAXDATASIZE + 1];
  unsigned char sha[SHA_DIGEST_LENGTH];
  ssize_t nbytes;
  char *p;
  char *q;
  size_t left;
  char *out;
  nbytes = recv(fd, buf, sizeof(buf) - 1, 0);

  if (nbytes <= 0) {
    perror("recv");
    return -1;
  }

  buf[nbytes] = '\0';

  if (strncmp(buf, "GET", 3) != 0) { return -1; }

  p = strstr(buf, "Sec-WebSocket-Key: ");

  if (p == NULL) { return -1; }

  p += strlen("Sec-WebSocket-Key: ");
  q = key;

  while (*p != '\0' && *p != '\n' && *p != '\r') {
    if ((size_t)(q - key) >= sizeof(key) - 1) { return -1; }

    *q++ = *p++;
  }

  *q = '\0';
  snprintf(buf, sizeof(buf), "%s%s", key, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
  nbytes = strlen(buf);
  SHA1((unsigned char *)buf, nbytes, sha);
  EVP_EncodeBlock((unsigned char *)key, sha, SHA_DIGEST_LENGTH);
  nbytes = snprintf(buf, sizeof(buf),
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Connection: Upgrade\r\n"
                    "Upgrade: websocket\r\n"
                    "Sec-WebSocket-Accept: %s\r\n\r\n", key);

  if (nbytes <= 0 || (size_t)nbytes >= sizeof(buf)) { return -1; }

  out = buf;
  left = (size_t)nbytes;

  while (left > 0) {
    ssize_t rc = write(fd, out, left);

    if (rc < 0) {
      if (errno == EINTR) { continue; }

      return -1;
    }

    if (rc == 0) { return -1; }

    out += rc;
    left -= (size_t)rc;
  }

  return 0;
}

static void tci_init_client(CLIENT *client, int fd, int seq) {
  if (client == NULL) { return; }

  client->fd              = fd;
  client->running         = 1;
  client->seq             = seq;
  client->last_fa         = -1;
  client->last_fb         = -1;
  client->last_fx         = -1;
  client->last_ma         = -1;
  client->last_mb         = -1;
  client->last_split      = -1;
  client->last_mox        = -1;
  client->count           =  0;
  client->rxsensor        =  0;
  client->txsensor        =  0;
  client->idle_queued     =  0;
  client->tci_timer       =  0;
  client->thread_id       = NULL;
}

static gpointer tci_server(gpointer data) {
  signal(SIGPIPE, SIG_IGN);
  int port = GPOINTER_TO_INT(data);
  int on = 1;
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  t_print("%s: starting TCI server on port %d\n", __func__, port);
  server_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (server_socket < 0) {
    t_perror("TCI: listen socket failed");
    return NULL;
  }

  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    t_perror("TCISrvReuseAddr");
  }

  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) < 0) {
    t_perror("TCISrvReUsePort");
  }

  if (setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO,  &tv, sizeof(tv)) < 0) {
    t_perror("TCISrvTimeOut");
  }

  // bind to listening port
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(port);

  if (bind(server_socket, (struct sockaddr * )&server_address, sizeof(server_address)) < 0) {
    t_perror("TCI: listen socket bind failed");
    close(server_socket);
    return NULL;
  }

  for (int id = 0; id < MAX_TCI_CLIENTS; id++) {
    tci_client[id].fd = -1;
  }

  // listen with a max queue of 3
  if (listen(server_socket, 3) < 0) {
    t_perror("TCI: listen failed");
    close(server_socket);
    return NULL;
  }

  while (tci_running) {
    int spare;
    int  fd;
    //
    // find a spare slot
    //
    spare = -1;
    g_mutex_lock(&tci_mutex);

    for (int id = 0; id < MAX_TCI_CLIENTS; id++) {
      if (tci_client[id].fd == -1) {
        spare = id;
        break;
      }
    }

    g_mutex_unlock(&tci_mutex);

    // if all slots are in use, wait and continue
    if (spare < 0) {
      usleep(100000L);
      continue;
    }

    //
    // A slot is available, try to get connection via accept()
    // (this initializes fd, address, address_length)
    //
    tci_client[spare].address_length = sizeof(struct sockaddr_in);
    fd = accept(server_socket, (struct sockaddr*)&tci_client[spare].address,
                &tci_client[spare].address_length);

    if (fd < 0) {
      // Since we have a 0.1 sec time-out, this is normal
      continue;
    }

    t_print("%s: slot= %d connected with fd=%d\n", __func__, spare, fd);

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,  &tv, sizeof(tv)) < 0) {
      t_perror("TCIClntSetTimeOut");
    }

    //
    // Setting TCP_NODELAY may (or may not) improve responsiveness
    // by *disabling* Nagle's algorithm for clustering small packets
    //
#ifdef __APPLE__

    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on)) < 0) {
#else

    if (setsockopt(fd, SOL_TCP, TCP_NODELAY, (void *)&on, sizeof(on)) < 0) {
#endif
      t_perror("TCP_NODELAY");
    }

    if (tci_perform_ws_handshake(fd) < 0) {
      close(fd);
      continue;
    }

    //
    // If everything worked as expected:
    // Initialize client data structure,
    // spawn off thread that "listens" to the connection,
    // start periodic job that reports frequency/mode changes
    //
    tci_init_client(&tci_client[spare], fd, spare);
    tci_client[spare].thread_id       = g_thread_new("TCI listener", tci_listener, (gpointer)&tci_client[spare]);
    tci_client[spare].tci_timer       = g_timeout_add(500, tci_reporter, &tci_client[spare]);
  }

  close(server_socket);
  return NULL;
}

static int digest_frame(const unsigned char *buff, char *msg,  int offset, int *type) {
  //
  // If the buffer contains enough data for a complete frame,
  // produce the payload in "msg" and return the number of
  // frame bytes consumed.
  // If there is not enough data, leave input data untouched
  // and return zero.
  // For a valid frame, return frame type in "type".
  //
  int head = 2;   // number  of bytes preceeding the payload
  int mask = (buff[1] & 0x80);
  int len = (buff[1] & 0x7F);
  int mstrt;

  if (len == 127) {
    // Do not even try
    t_print("%s: excessive length\n", __func__);
    return 0;
  }

  if (len == 126) {
    len = buff[2] + (buff[3] << 8);
    head = 4;
  }

  if (mask) {
    mstrt = head;  // position where mask starts
    head += 4;
  }

  if (head + len > offset) {
    return 0;
  }

  //
  // There is enough data. Copy/DeMask  it.
  //
  *type = buff[0] & 0x0F;

  for (int i = 0; i < len;  i++) {
    if (mask) {
      msg[i] = buff[head + i] ^ buff[mstrt + (i & 3)];
    } else {
      msg[i] = buff[head + i];
    }
  }

  msg[len] = 0;   // form null-terminated  string
  //
  // Return the number of bytes *digested*, not the number of  bytes produced.
  //
  return (head + len);
}

static void tci_process_ws_payload(CLIENT *client, int type, char *msg) {
  if (client == NULL) { return; }

  switch (type) {
  case opTEXT:
    if (rigctl_debug) {
      t_print("TCI%d command rcvd=%s\n", client->seq, msg);
    }

    tci_handle_text(client, msg);
    break;

  case opPING:
    if (rigctl_debug) { t_print("TCI%d PING rcvd\n", client->seq); }

    tci_send_pong(client);
    break;

  case opCLOSE:
    if (rigctl_debug) { t_print("TCI%d CLOSE rcvd\n", client->seq); }

    g_mutex_lock(&tci_mutex);
    client->running = 0;
    g_mutex_unlock(&tci_mutex);
    break;

  default:
    if (rigctl_debug) {
      t_print("TCI%d unknown frame type=%d ignored\n", client->seq, type);
    }

    break;
  }
}

//
// TCI "Listener". It starts with sending  initialisation data, and then
// listens for incoming commands (and ignores all of them!).
// It only responds to incoming PING and CLOSE packets.
//
static gpointer tci_listener(gpointer data) {
  CLIENT *client = (CLIENT *)data;
  t_print("%s: starting client: socket=%d\n", __func__, client->fd);
  // update CAT status onscreen
  cat_control++;
  g_idle_add(ext_vfo_update, NULL);
  int offset = 0;
  unsigned char buff [MAXDATASIZE];
  char msg [MAXDATASIZE];
  //
  // Send initial state info to client
  // using emulatation Expert SunSDR2Pro
  //
  // tci_send_text(client, "protocol:ExpertSDR3,1.8;");
  // tci_send_text(client, "device:SunSDR2PRO;");
  tci_send_text(client, "protocol:ExpertSDR3,2.0;");
  tci_send_text(client, "device:SunSDR2QRP;");
  tci_send_text(client, "receive_only:false;");
  tci_send_trx_count(client);
  tci_send_text(client, "channels_count:2;");
  //
  // With transverters etc. the upper frequency can be
  // very large. For the time being we go up to the 70cm band
  // No need to send vfo and modulation  commands, since this is
  // automatically  done in the tci_reporter task.
  //
  // tci_send_text(client, "vfo_limits:0,450000000;");
  // tci_send_text(client, "if_limits:-96000,96000;");
  tci_send_limits(client, VFO_A);
  tci_send_text(client, "modulations_list:LSB,USB,DSB,CW,FMN,AM,DIGU,SPEC,DIGL,SAM,DRM;");
  tci_send_dds(client, VFO_A);
  tci_send_dds(client, VFO_B);
  tci_send_text(client, "if:0,0,0;");
  tci_send_text(client, "if:0,1,0;");
  tci_send_text(client, "if:1,0,0;");
  tci_send_text(client, "if:1,1,0;");
  tci_send_vfo(client, VFO_A, 0);
  tci_send_vfo(client, VFO_A, 1);
  tci_send_vfo(client, VFO_B, 0);
  tci_send_vfo(client, VFO_B, 1);
  tci_send_mode(client, VFO_A);
  tci_send_mode(client, VFO_B);
  tci_send_text(client, "rx_enable:0,true;");

  if (receivers == 1) {
    tci_send_text(client, "rx_enable:1,false;");
  } else {
    tci_send_text(client, "rx_enable:1,true;");
  }

  tci_send_text(client, "tx_enable:0,true;");
  tci_send_text(client, "tx_enable:1,false;");
  tci_send_text(client, "split_enable:0,false;");
  tci_send_text(client, "split_enable:1,false;");
  tci_send_mox(client);
  tci_send_text(client, "trx:1,false;");
  tci_send_text(client, "tune:0,false;");
  tci_send_text(client, "tune:1,false;");
  tci_send_text(client, "mute:false;");
  tci_send_macros_cwspeed(client);
  tci_send_text(client, "cw_macros_delay:10;");
  tci_send_keyer_cwspeed(client);
  tci_send_text(client, "start;");
  tci_send_text(client, "ready;");

  while (client->running) {
    int numbytes;
    int type;

    //
    // This can happen when a very long command has arrived...
    // ...just give up
    //
    if (offset >= MAXDATASIZE) {
      g_mutex_lock(&tci_mutex);
      client->running = 0;
      g_mutex_unlock(&tci_mutex);
      break;
    }

    g_mutex_lock(&tci_mutex);
    int fd = client->fd;
    g_mutex_unlock(&tci_mutex);

    if (fd < 0) { break; }

    numbytes = tci_transport_read(client, buff + offset, MAXDATASIZE - offset);

    if (numbytes <= 0) {
      usleep(100000);
      continue;
    }

    offset += numbytes;

    //
    // The chunk just read may contain more than one frame
    //
    while ((numbytes =  digest_frame(buff, msg, offset, &type)) > 0) {
      tci_process_ws_payload(client, type, msg);
      //
      // Remove the just-processed frame from the input buffer
      // In normal operation, offset will be set to zero here.
      //
      offset  -= numbytes;

      if (offset > 0) {
        for (int i = 0; i < offset; i++) {
          buff[i] = buff[i + numbytes];
        }
      }
    }
  }

  tci_send_text(client, "stop;");
  tci_send_close(client);
  force_close(client);
  t_print("%s: leaving thread\n", __func__);
  // update CAT status onscreen
  cat_control--;
  g_idle_add(ext_vfo_update, NULL);
  return NULL;
}
