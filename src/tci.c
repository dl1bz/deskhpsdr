/* Copyright (C)
* 2024,2025, 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
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
// TCI server based on libwebsockets
// complete rebuild for deskHPSDR by DL1BZ
//

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include <ctype.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>

#ifdef __APPLE__
  #include <time.h>
#endif

#include <libwebsockets.h>

#include "radio.h"
#include "vfo.h"
#include "rigctl.h"
#include "ext.h"
#include "message.h"
#include "toolset.h"
#include "main.h"
#include "discovery.h"
#include "tci_audio.h"
#include "audio.h"
#include "band.h"
#include "sliders.h"

#define MAXDATASIZE     1024
#define MAXMSGSIZE      512
#define TCI_MAX_ARGS 16
#define TCI_BINARY_REASSEMBLY_MAX 65536

#ifndef LWS_PROTOCOL_LIST_TERM
  #define LWS_PROTOCOL_LIST_TERM { NULL, NULL, 0, 0, 0, NULL, 0 }
#endif

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
static guint tci_tx_chrono_timer_id = 0;

static struct lws_context *tci_lws_context = NULL;
static int tci_lws_seq = 0;
static int tci_lws_pending_writable = 0;
static int tci_apply_in_progress = 0;
static int tci_tune_transition = 0;

typedef struct _client {
  int seq;                      // Seq. number of the client
  int fd;                       // socket
  int running;                  // set this to zero to close client connection
  guint tci_timer;              // GTK id  of the periodic task
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
  struct lws *wsi;              // libwebsockets connection
  GQueue *lws_tx_queue;         // queued RESPONSE objects for LWS writable callback
  int initial_sent;             // initial state already sent via LWS
  DISCOVERED *device;           // device bound to this TCI client
  int device_index;             // discovery index bound to this TCI client
  int rx_audio_enabled[TCI_RX_AUDIO_MAX_RECEIVERS];
  guint64 rx_audio_read_count[TCI_RX_AUDIO_MAX_RECEIVERS];
  int tx_audio_enabled;
  gint64 tx_chrono_next_us;
  guint64 tx_chrono_queue_count;
  guint64 tx_audio_rx_count;
  unsigned char *binary_rx_buf;
  size_t binary_rx_len;
  size_t binary_rx_size;
} CLIENT;

typedef struct _response {
  CLIENT *client;
  int     type;
  char    msg[MAXMSGSIZE];
  unsigned char *bin;
  size_t  len;
} RESPONSE;

static GMutex tci_mutex;
static GList *tci_clients = NULL;

static gpointer tci_lws_server (gpointer data);
static void tci_lws_free_queue (CLIENT *client);
static void tci_update_rx_audio_global (void);
static void tci_audio_wakeup (void);
static gboolean tci_tx_chrono_timer_cb (gpointer data);
static void tci_service_tx_chrono (void);
static void tci_lws_binary_reset (CLIENT *client);
static void tci_handle_binary_lws (CLIENT *client, const unsigned char* data, size_t len, struct lws *wsi);
static void tci_handle_binary (CLIENT *client, const unsigned char* data, size_t len);

typedef struct {
  char *cmd;
  int argc;
  char *argv[TCI_MAX_ARGS];
} TCI_CMD;

typedef void (*TCI_HANDLER) (CLIENT *client, const TCI_CMD *cmd);

typedef struct {
  const char *name;
  int min_args;
  int max_args;   // -1 = unlimited
  TCI_HANDLER handler;
} TCI_DISPATCH;

static void tci_handle_text (CLIENT *client, char* msg);
static const TCI_DISPATCH tci_dispatch[];

static void tci_send_smeter (CLIENT *client, int v);

static void tci_begin_apply (void) {
  tci_apply_in_progress = 1;
}

static void tci_end_apply (void) {
  tci_apply_in_progress = 0;
}

int tci_is_applying (void) {
  return tci_apply_in_progress;
}

void tci_begin_tune_transition (void) {
  tci_tune_transition = 1;
}

void tci_end_tune_transition (void) {
  tci_tune_transition = 0;
}

int tci_is_tune_transition (void) {
  return tci_tune_transition;
}


static gboolean tci_tx_chrono_timer_cb (gpointer data) {
  (void) data;
  if (!tci_running) {
    tci_tx_chrono_timer_id = 0;
    return G_SOURCE_REMOVE;
  }
  tci_service_tx_chrono();
  return G_SOURCE_CONTINUE;
}


//
// Launch TCI system. Called upon program start if TCI is
// enabled in the props file, and from the CAT/TCI menu
// if TCI is enabled there.
//
void launch_tci (void) {
  t_print ("---- LAUNCHING TCI LWS SERVER ----\n");
  tci_audio_set_wakeup_callback (tci_audio_wakeup);
  tci_running = 1;
  if (tci_tx_chrono_timer_id == 0) {
    tci_tx_chrono_timer_id = g_timeout_add (1, tci_tx_chrono_timer_cb, NULL);
  }
  tci_server_thread_id = g_thread_new ("tci lws server", tci_lws_server, GINT_TO_POINTER (tci_port));
}

//
// Shut down TCI system. Called from CAT/TCI menu
// if TCI is disabled there.
//
void shutdown_tci (void) {
  t_print ("%s\n", __func__);
  tci_running = 0;
  if (tci_tx_chrono_timer_id != 0) {
    g_source_remove (tci_tx_chrono_timer_id);
    tci_tx_chrono_timer_id = 0;
  }
  tci_audio_set_active (0);
  tci_audio_set_wakeup_callback (NULL);
  if (tci_lws_context != NULL) {
    lws_cancel_service (tci_lws_context);
  }
  if (tci_server_thread_id != NULL) {
    if (g_thread_self() != tci_server_thread_id) {
      g_thread_join (tci_server_thread_id);
    }
    tci_server_thread_id = NULL;
  }
}

static int tci_queue_frame (CLIENT *client, int type, const char* msg, int check_running) {
  RESPONSE *resp;
  if (client == NULL) { return 0; }
  if (check_running && !client->running) { return 0; }
  resp = g_new (RESPONSE, 1);
  resp->client = client;
  resp->type = type;
  resp->bin = NULL;
  resp->len = 0;
  if (msg != NULL) {
    g_strlcpy (resp->msg, msg, MAXMSGSIZE);
  } else {
    resp->msg[0] = 0;
  }
  g_mutex_lock (&tci_mutex);
  if (type == opTEXT && client->idle_queued >= 100) {
    g_mutex_unlock (&tci_mutex);
    g_free (resp);
    return 0;
  }
  client->idle_queued++;
  if (client->wsi != NULL) {
    if (client->lws_tx_queue == NULL) {
      client->lws_tx_queue = g_queue_new();
    }
    g_queue_push_tail (client->lws_tx_queue, resp);
    tci_lws_pending_writable = 1;
    g_mutex_unlock (&tci_mutex);
    if (tci_lws_context != NULL) {
      lws_cancel_service (tci_lws_context);
    }
    return 1;
  }
  g_mutex_unlock (&tci_mutex);
  g_free (resp);
  g_mutex_lock (&tci_mutex);
  if (client->idle_queued > 0) { client->idle_queued--; }
  g_mutex_unlock (&tci_mutex);
  return 0;
}

static int tci_queue_binary_frame (CLIENT *client, const unsigned char* data, size_t len) {
  RESPONSE *resp;
  if (client == NULL || data == NULL || len == 0 || !client->running) { return 0; }
  resp = g_new (RESPONSE, 1);
  resp->client = client;
  resp->type = opBIN;
  resp->msg[0] = 0;
  resp->bin = g_memdup2 (data, len);
  resp->len = len;
  g_mutex_lock (&tci_mutex);
  if (client->idle_queued >= 100 || client->wsi == NULL) {
    g_mutex_unlock (&tci_mutex);
    g_free (resp->bin);
    g_free (resp);
    return 0;
  }
  if (client->lws_tx_queue == NULL) {
    client->lws_tx_queue = g_queue_new();
  }
  client->idle_queued++;
  g_queue_push_tail (client->lws_tx_queue, resp);
  tci_lws_pending_writable = 1;
  g_mutex_unlock (&tci_mutex);
  if (tci_lws_context != NULL) {
    lws_cancel_service (tci_lws_context);
  }
  return 1;
}

static void tci_send_text (CLIENT *client, const char* msg) {
  if (rigctl_debug && client != NULL) { t_print ("TCI%d response: %s\n", client->seq, msg ? msg : "(null)"); }
  (void) tci_queue_frame (client, opTEXT, msg, 1);
}

static GList *tci_clients_snapshot (void) {
  GList *clients;
  g_mutex_lock (&tci_mutex);
  clients = g_list_copy (tci_clients);
  g_mutex_unlock (&tci_mutex);
  return clients;
}

static void tci_update_rx_audio_global (void) {
  int enabled = 0;
  g_mutex_lock (&tci_mutex);
  for (GList *l = tci_clients; l != NULL && !enabled; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
        if (client->rx_audio_enabled[i]) {
          enabled = 1;
          break;
        }
      }
    }
  }
  g_mutex_unlock (&tci_mutex);
  tci_audio_set_active (enabled);
}

static void tci_audio_wakeup (void) {
  tci_lws_pending_writable = 1;
  if (tci_lws_context != NULL) {
    lws_cancel_service (tci_lws_context);
  }
}

static void tci_queue_rx_audio_frame (CLIENT *client, int receiver_id) {
  unsigned char frame[TCI_AUDIO_RX_FRAME_MAX_BYTES];
  size_t frame_len;
  if (client == NULL || !client->running || !client->rx_audio_enabled[receiver_id]) { return; }
  if (tci_audio_get_frame (receiver_id, &client->rx_audio_read_count[receiver_id], frame, sizeof (frame),
                           &frame_len) == 0) {
    return;
  }
  (void) tci_queue_binary_frame (client, frame, frame_len);
}


static int tci_queue_tx_chrono_frame (CLIENT *client) {
  TCI_STREAM_HEADER header;
  int queued;
  if (client == NULL || !client->running || !client->tx_audio_enabled) { return 0; }
  memset (&header, 0, sizeof (header));
  header.receiver = 0;
  header.sample_rate = TCI_AUDIO_SAMPLE_RATE;
  header.format = TCI_AUDIO_FORMAT_FLOAT32;
  header.length = TCI_TX_AUDIO_CHRONO_LENGTH;
  header.type = TCI_STREAM_TX_CHRONO;
  header.channels = TCI_AUDIO_CHANNELS;
  queued = tci_queue_binary_frame (client, (const unsigned char*) &header, sizeof (header));
  if (queued) {
    client->tx_chrono_queue_count++;
  } else if (rigctl_debug) {
    t_print ("TCI%d TX chrono queue FAILED enabled=%d running=%d\n",
             client->seq,
             client->tx_audio_enabled,
             client->running);
  }
  return queued;
}

static void tci_service_tx_chrono (void) {
  GList *clients;
  gint64 now_us;
  const gint64 frame_us = ((gint64) TCI_TX_AUDIO_FRAME_FRAMES * G_USEC_PER_SEC) / TCI_AUDIO_SAMPLE_RATE;
  clients = tci_clients_snapshot();
  now_us = g_get_monotonic_time();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    int send_chrono = 0;
    if (client == NULL || !client->running) { continue; }
    g_mutex_lock (&tci_mutex);
    if (client->tx_audio_enabled) {
      if (client->tx_chrono_next_us == 0 || now_us >= client->tx_chrono_next_us) {
        send_chrono = 1;
        if (client->tx_chrono_next_us == 0 || now_us > (client->tx_chrono_next_us + frame_us)) {
          client->tx_chrono_next_us = now_us + frame_us;
        } else {
          client->tx_chrono_next_us += frame_us;
        }
      }
    } else {
      client->tx_chrono_next_us = 0;
    }
    g_mutex_unlock (&tci_mutex);
    if (send_chrono) {
      tci_queue_tx_chrono_frame (client);
    }
  }
  g_list_free (clients);
}

static void tci_lws_binary_reset (CLIENT *client) {
  if (client == NULL) { return; }
  client->binary_rx_len = 0;
}

static void tci_handle_binary (CLIENT *client, const unsigned char* data, size_t len) {
  TCI_STREAM_HEADER header;
  if (client == NULL || data == NULL || len < sizeof (TCI_STREAM_HEADER)) { return; }
  memcpy (&header, data, sizeof (header));
  switch (header.type) {
  case TCI_STREAM_TX_AUDIO:
    if (client->tx_audio_enabled) {
      client->tx_audio_rx_count++;
      tci_audio_handle_tx_frame (data, len);
    } else if (rigctl_debug) {
      t_print ("TCI%d TX audio ignored: tx_audio_enabled=0 len=%zu\n", client->seq, len);
    }
    break;
  default:
    if (rigctl_debug) {
      t_print ("TCI%d binary ignored: type=%u len=%zu\n", client->seq, header.type, len);
    }
    break;
  }
}

static void tci_handle_binary_lws (CLIENT *client, const unsigned char* data, size_t len, struct lws *wsi) {
  size_t remaining;
  int final;
  size_t needed;
  if (client == NULL || data == NULL || wsi == NULL || len == 0) { return; }
  remaining = lws_remaining_packet_payload (wsi);
  final = lws_is_final_fragment (wsi);
  if (client->binary_rx_len == 0 && remaining == 0 && final) {
    tci_handle_binary (client, data, len);
    return;
  }
  needed = client->binary_rx_len + len;
  if (needed > TCI_BINARY_REASSEMBLY_MAX) {
    if (rigctl_debug) {
      t_print ("TCI%d binary fragment overflow: accumulated=%zu incoming=%zu max=%zu\n",
               client->seq,
               client->binary_rx_len,
               len,
               (size_t) TCI_BINARY_REASSEMBLY_MAX);
    }
    tci_lws_binary_reset (client);
    return;
  }
  if (needed > client->binary_rx_size) {
    size_t new_size = client->binary_rx_size ? client->binary_rx_size : 8192;
    while (new_size < needed) {
      new_size *= 2;
    }
    client->binary_rx_buf = g_realloc (client->binary_rx_buf, new_size);
    client->binary_rx_size = new_size;
  }
  memcpy (client->binary_rx_buf + client->binary_rx_len, data, len);
  client->binary_rx_len = needed;
  if (!final || remaining != 0) {
    return;
  }
  tci_handle_binary (client, client->binary_rx_buf, client->binary_rx_len);
  tci_lws_binary_reset (client);
}

static void tci_service_rx_audio (void) {
  GList *clients;
  if (!tci_audio_is_active()) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client == NULL || !client->running) { continue; }
    for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
      if (client->rx_audio_enabled[i]) {
        tci_queue_rx_audio_frame (client, i);
      }
    }
  }
  g_list_free (clients);
}

//
// To keep things  simple, tci_send_dds does not report
// the center frequency but the "real" RX frequency
//
static void tci_send_dds (CLIENT *client, int v) {
  long long f;
  char msg[MAXMSGSIZE];
  if (v < 0 || v > 1) { return; }
  f = vfo[v].ctun ? vfo[v].ctun_frequency : vfo[v].frequency;
  snprintf (msg, MAXMSGSIZE, "dds:%d,%lld;", v, f);
  tci_send_text (client, msg);
}

static void tci_send_mox (CLIENT *client) {
  if (radio_is_transmitting()) {
    tci_send_text (client, "trx:0,true;");
    client->last_mox = 1;
  } else {
    tci_send_text (client, "trx:0,false;");
    client->last_mox = 0;
  }
}

static void tci_send_mox_state (CLIENT *client, int state) {
  if (client == NULL) { return; }
  if (client->last_mox == state) { return; }
  if (state) {
    tci_send_text (client, "trx:0,true;");
    client->last_mox = 1;
  } else {
    tci_send_text (client, "trx:0,false;");
    client->last_mox = 0;
  }
}

static void tci_broadcast_mox_state (int state) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_mox_state (client, state);
    }
  }
  g_list_free (clients);
}

void tci_mox_changed (int state) {
  if (!tci_running) { return; }
  tci_broadcast_mox_state (state);
}

static void tci_broadcast_tune_state (int state) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      if (state) {
        tci_send_text (client, "tune:0,true;");
      } else {
        tci_send_text (client, "tune:0,false;");
      }
    }
  }
  g_list_free (clients);
}

void tci_tune_changed (int state) {
  if (!tci_running) { return; }
  tci_broadcast_tune_state (state);
}

//
// There are four (!) frequencies to report, namely for RX0/1 channel0/1.
// RX=0 channel=0: reports VFO-A frequency, all other combination report VFO-B
//
// Thus logbook programs correctly display both frequencies no matter whether
// they  use RX0/channel0:RX0/channel1 or RX0/channel0:RX1/channel0
//
static void tci_send_vfo (CLIENT *client, int v, int c) {
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
  snprintf (msg, MAXMSGSIZE, "vfo:%d,%d,%lld;", v, c, f);
  tci_send_text (client, msg);
}

static void tci_broadcast_vfo (int v, int c) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_vfo (client, v, c);
    }
  }
  g_list_free (clients);
}

static void tci_set_vfo (CLIENT *client, int VfoNr, int Ch, long long SetFreq) {
  if (VfoNr < 0 || VfoNr > 1) { return; }
  if (Ch < 0 || Ch > 1) { return; }
  tci_begin_apply();
  if (VfoNr  == VFO_A) {
    vfo_set_frequency (VFO_A, SetFreq);
    client->last_fa = SetFreq;
    g_idle_add (ext_vfo_update, NULL);
  } else {
    vfo_set_frequency (VFO_B, SetFreq);
    client->last_fb = SetFreq;
    g_idle_add (ext_vfo_update, NULL);
  }
  tci_end_apply();
  tci_broadcast_vfo (VfoNr, Ch);
}

static void tci_send_limits (CLIENT *client) {
  char msg[MAXMSGSIZE];
  if (client == NULL || client->device == NULL || receiver[0] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "vfo_limits:%lld,%lld;",
            (long long) client->device->frequency_min,
            (long long) client->device->frequency_max);
  tci_send_text (client, msg);
  snprintf (msg, MAXMSGSIZE, "if_limits:%lld,%lld;",
            - (long long) (receiver[0]->sample_rate / 2),
            (long long) (receiver[0]->sample_rate / 2));
  tci_send_text (client, msg);
}

static void tci_send_drive (CLIENT *client, int v) {
  char msg[MAXMSGSIZE];
  int tx_drive;
  tx_drive = radio_get_drive_as_int();
  if (v < 0 || v > 1) { return; }
  snprintf (msg, MAXMSGSIZE, "drive:%d,%d;", v, tx_drive);
  tci_send_text (client, msg);
}

static void tci_send_split (CLIENT *client) {
  //
  // send "true" if tx is on VFO-B frequency
  //
  if (vfo_get_tx_vfo() == VFO_A) {
    tci_send_text (client, "split_enable:0,false;");
    client->last_split = 0;
  } else {
    tci_send_text (client, "split_enable:0,true;");
    client->last_split = 1;
  }
}

static void tci_send_tx_enable (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "tx_enable:0,%s;",
            can_transmit ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_tune (CLIENT *client) {
  if (tune) {
    tci_send_text (client, "tune:0,true;");
  } else {
    tci_send_text (client, "tune:0,false;");
  }
}

static void tci_send_txfreq (CLIENT *client) {
  char msg[MAXMSGSIZE];
  long long f = vfo_get_tx_freq();
  snprintf (msg, MAXMSGSIZE, "tx_frequency:%lld;", f);
  tci_send_text (client, msg);
  client->last_fx = f;
}

static void tci_broadcast_txfreq (void) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_txfreq (client);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_split (void) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_split (client);
    }
  }
  g_list_free (clients);
}

void tci_split_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_split();
}

static const char *tci_mode_name (int m) {
  switch (m) {
  case modeLSB:
    return "LSB";
  case modeUSB:
    return "USB";
  case modeDSB:
    return "DSB";
  case modeCWL:
  case modeCWU:
    return "CW";
  case modeFMN:
    return "FM";
  case modeAM:
    return "AM";
  case modeDIGU:
    return "DIGU";
  case modeSPEC:
    return "SPEC";
  case modeDIGL:
    return "DIGL";
  case modeSAM:
    return "SAM";
  case modeDRM:
    return "DRM";
  default:
    return "USB";
  }
}

static void tci_send_mode_value (CLIENT *client, int v, int m) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  if (v < 0 || v > 1) { return; }
  snprintf (msg, MAXMSGSIZE, "modulation:%d,%s;", v, tci_mode_name (m));
  tci_send_text (client, msg);
  if (v == 0) {
    client->last_ma = m;
  } else {
    client->last_mb = m;
  }
}

static void tci_send_mode (CLIENT *client, int v) {
  if (v < 0 || v > 1) { return; }
  tci_send_mode_value (client, v, vfo[v].mode);
}

static void tci_broadcast_mode_value (int v, int m) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_mode_value (client, v, m);
    }
  }
  g_list_free (clients);
}

void tci_vfo_changed (int id) {
  if (!tci_running) { return; }
  if (id == VFO_A) {
    tci_broadcast_vfo (VFO_A, 0);
  } else if (id == VFO_B) {
    tci_broadcast_vfo (VFO_A, 1);
    tci_broadcast_vfo (VFO_B, 0);
    tci_broadcast_vfo (VFO_B, 1);
  }
}

void tci_vfos_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_vfo (VFO_A, 0);
  tci_broadcast_vfo (VFO_A, 1);
  tci_broadcast_vfo (VFO_B, 0);
  tci_broadcast_vfo (VFO_B, 1);
  tci_broadcast_mode_value (VFO_A, vfo[VFO_A].mode);
  tci_broadcast_mode_value (VFO_B, vfo[VFO_B].mode);
  tci_broadcast_txfreq();
  tci_broadcast_split();
}

void tci_mode_changed (int id) {
  if (!tci_running) { return; }
  if (id < VFO_A || id > VFO_B) { return; }
  tci_broadcast_mode_value (id, vfo[id].mode);
}

void tci_tx_frequency_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_txfreq();
}

static int tci_parse_mode (const char* mode_str) {
  if (mode_str == NULL) { return -1; }
  if (!g_ascii_strcasecmp (mode_str, "lsb"))  { return modeLSB; }
  if (!g_ascii_strcasecmp (mode_str, "usb"))  { return modeUSB; }
  if (!g_ascii_strcasecmp (mode_str, "dsb"))  { return modeDSB; }
  if (!g_ascii_strcasecmp (mode_str, "cw"))   { return modeCWU; }
  if (!g_ascii_strcasecmp (mode_str, "cwl"))  { return modeCWL; }
  if (!g_ascii_strcasecmp (mode_str, "cwu"))  { return modeCWU; }
  if (!g_ascii_strcasecmp (mode_str, "fmn"))  { return modeFMN; }
  if (!g_ascii_strcasecmp (mode_str, "fm"))   { return modeFMN; }
  if (!g_ascii_strcasecmp (mode_str, "am"))   { return modeAM; }
  if (!g_ascii_strcasecmp (mode_str, "digu")) { return modeDIGU; }
  if (!g_ascii_strcasecmp (mode_str, "spec")) { return modeSPEC; }
  if (!g_ascii_strcasecmp (mode_str, "digl")) { return modeDIGL; }
  if (!g_ascii_strcasecmp (mode_str, "sam"))  { return modeSAM; }
  if (!g_ascii_strcasecmp (mode_str, "drm"))  { return modeDRM; }
  return -1;
}

typedef struct {
  int vfo_id;
  int mode;
} TCI_MODE_CHANGE;

static int tci_mode_change_cb (void* data) {
  TCI_MODE_CHANGE *mc = (TCI_MODE_CHANGE*) data;
  tci_begin_apply();
  vfo_id_mode_changed (mc->vfo_id, mc->mode);
  tci_end_apply();
  g_free (mc);
  return G_SOURCE_REMOVE;
}

static void tci_set_mode (CLIENT *client, int VfoNr, const char* mode_str) {
  if (VfoNr < 0 || VfoNr > 1) { return; }
  int m = tci_parse_mode (mode_str);
  if (m < 0) {
    t_print ("TCI%d unknown mode: %s\n", client->seq, mode_str);
    tci_send_mode (client, VfoNr);
    return;
  }
  TCI_MODE_CHANGE *mc = g_new (TCI_MODE_CHANGE, 1);
  mc->vfo_id = VfoNr;
  mc->mode = m;
  g_idle_add (tci_mode_change_cb, mc);
  tci_broadcast_mode_value (VfoNr, m);
}

static void tci_send_trx_count (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "trx_count:%d;", receivers);
  tci_send_text (client, msg);
}

static void tci_send_macros_cwspeed (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "cw_macros_speed:%d;", cw_keyer_speed);
  tci_send_text (client, msg);
}

static void tci_send_keyer_cwspeed (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "cw_keyer_speed:%d;", cw_keyer_speed);
  tci_send_text (client, msg);
}


static int tci_parse_text (char* s, TCI_CMD *c) {
  int argc = 0;
  if (s == NULL || c == NULL) { return -1; }
  memset (c, 0, sizeof (*c));
  char *end = strchr (s, ';');
  if (end != NULL) { *end = 0; }
  c->cmd = s;
  char *p = strchr (s, ':');
  if (p == NULL) { return 0; }
  *p++ = 0;
  while (argc < TCI_MAX_ARGS) {
    c->argv[argc++] = p;
    p = strchr (p, ',');
    if (p == NULL) { break; }
    *p++ = 0;
  }
  c->argc = argc;
  return 0;
}

static int tci_bool (const char* s) {
  return s != NULL && (*s == '1' || !g_ascii_strcasecmp (s, "true"));
}

static int tci_int (const char* s, int def) {
  return s != NULL ? atoi (s) : def;
}

static long long tci_ll (const char* s, long long def) {
  return s != NULL ? atoll (s) : def;
}

static void tci_cmd_trx_count (CLIENT *client, const TCI_CMD *cmd) {
  (void) cmd;
  tci_send_trx_count (client);
}

static void tci_cmd_trx (CLIENT *client, const TCI_CMD *cmd) {
  int source_tci = (cmd->argc >= 3 && cmd->argv[2] != NULL && !g_ascii_strcasecmp (cmd->argv[2], "tci"));
  if (cmd->argc >= 2) {
    if (tci_bool (cmd->argv[1])) {
      if (source_tci) {
        tci_audio_tx_reset();
        tci_audio_tx_set_active (1);
        g_mutex_lock (&tci_mutex);
        client->tx_audio_enabled = 1;
        client->tx_chrono_next_us = 0;
        client->tx_chrono_queue_count = 0;
        client->tx_audio_rx_count = 0;
        g_mutex_unlock (&tci_mutex);
        tci_lws_binary_reset (client);
#ifdef PORTAUDIO
        if (tci_audio_monitor) {
          // audio_open_tci_monitor("Externe Kopfhörer");
          audio_open_tci_monitor (active_receiver->audio_name);
        }
#endif
        tci_send_text (client, "audio_samplerate:48000;");
        tci_send_text (client, "audio_stream_sample_type:float32;");
        tci_send_text (client, "audio_stream_channels:1;");
        tci_send_text (client, "audio_stream_samples:512;");
        tci_send_text (client, "tx_stream_audio_buffering:0;");
        tci_send_text (client, "audio_start:0;");
        t_print ("TCI%d TX request (tci audio)\n", client->seq);
      } else {
        t_print ("TCI%d TX request\n", client->seq);
      }
      g_idle_add (ext_mox_update, GINT_TO_POINTER (1));
    } else {
      g_mutex_lock (&tci_mutex);
      client->tx_audio_enabled = 0;
      client->tx_chrono_next_us = 0;
      g_mutex_unlock (&tci_mutex);
      tci_audio_tx_set_active (0);
      tci_audio_tx_reset();
      tci_lws_binary_reset (client);
      g_timeout_add (50, ext_mox_update, GINT_TO_POINTER (0));
      t_print ("TCI%d RX request\n", client->seq);
    }
  } else {
    tci_send_mox (client);
  }
}

static void tci_cmd_tune (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 2) {
    int state = tci_bool (cmd->argv[1]);
    g_idle_add (ext_tune_update, GINT_TO_POINTER (state));
    t_print ("TCI%d TUNE request=%d\n", client->seq, state);
  } else {
    tci_send_tune (client);
  }
}

static void tci_cmd_rx_sensors_enable (CLIENT *client, const TCI_CMD *cmd) {
  g_mutex_lock (&tci_mutex);
  client->rxsensor = tci_bool (cmd->argv[0]);
  g_mutex_unlock (&tci_mutex);
}

static void tci_cmd_tx_sensors_enable (CLIENT *client, const TCI_CMD *cmd) {
  g_mutex_lock (&tci_mutex);
  client->txsensor = tci_bool (cmd->argv[0]);
  g_mutex_unlock (&tci_mutex);
}

static void tci_cmd_audio_start (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return; }
  g_mutex_lock (&tci_mutex);
  client->rx_audio_enabled[receiver_id] = 1;
  client->rx_audio_read_count[receiver_id] = tci_audio_get_write_count (receiver_id);
  g_mutex_unlock (&tci_mutex);
  tci_update_rx_audio_global();
#ifdef PORTAUDIO
  if (tci_audio_monitor) {
    // audio_open_tci_monitor("Mac mini-Lautsprecher");
    // audio_open_tci_monitor("Externe Kopfhörer");
  }
#endif
  snprintf (msg, MAXMSGSIZE, "audio_start:%d;", receiver_id);
  tci_send_text (client, msg);
}

static void tci_send_audio_samplerate (CLIENT *client) {
  tci_send_text (client, "audio_samplerate:48000;");
}

static void tci_send_audio_stream_sample_type (CLIENT *client) {
  tci_send_text (client, "audio_stream_sample_type:3;");
}

static void tci_send_audio_stream_channels (CLIENT *client) {
  tci_send_text (client, "audio_stream_channels:2;");
}

static void tci_send_audio_stream_samples (CLIENT *client) {
  tci_send_text (client, "audio_stream_samples:512;");
}

static void tci_cmd_audio_samplerate (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_audio_samplerate (client);
}

static void tci_cmd_audio_stream_sample_type (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_audio_stream_sample_type (client);
}

static void tci_cmd_audio_stream_channels (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_audio_stream_channels (client);
}

static void tci_cmd_audio_stream_samples (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_audio_stream_samples (client);
}

static void tci_cmd_audio_stop (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return; }
  g_mutex_lock (&tci_mutex);
  client->rx_audio_enabled[receiver_id] = 0;
  g_mutex_unlock (&tci_mutex);
  tci_update_rx_audio_global();
#ifdef PORTAUDIO
  audio_close_tci_monitor();
#endif
  snprintf (msg, MAXMSGSIZE, "audio_stop:%d;", receiver_id);
  tci_send_text (client, msg);
}

static void tci_cmd_modulation (CLIENT *client, const TCI_CMD *cmd) {
  int VfoNr = tci_int (cmd->argv[0], 0);
  if (cmd->argc >= 2) {
    tci_set_mode (client, VfoNr, cmd->argv[1]);
  } else {
    tci_send_mode (client, VfoNr);
  }
}

static void tci_cmd_vfo (CLIENT *client, const TCI_CMD *cmd) {
  int VfoNr = tci_int (cmd->argv[0], 0);
  int Ch = tci_int (cmd->argv[1], 0);
  if (cmd->argc >= 3) {
    tci_set_vfo (client, VfoNr, Ch, tci_ll (cmd->argv[2], 0));
  } else {
    tci_send_vfo (client, VfoNr, Ch);
  }
}

static void tci_cmd_rx_smeter (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_smeter (client, tci_int (cmd->argv[0], 0));
}

static void tci_cmd_drive (CLIENT *client, const TCI_CMD *cmd) {
  int trx = tci_int (cmd->argv[0], 0);
  if (cmd->argc >= 2) {
    int value = tci_int (cmd->argv[1], 0);
    if (value > 0 && value <= 100) {
      radio_set_drive (value);
      if (can_transmit) {
        int v = vfo_get_tx_vfo();
        int b = vfo[v].band;
        BANDSETTINGS *bs = band_get_settings (b);
        if (bs != NULL) {
          bs->tx_drive = radio_get_drive_as_int();
          t_print ("%s: bs->tx_drive = %d\n", __func__, bs->tx_drive);
        }
      }
      update_drive_scale();
    }
  }
  tci_send_drive (client, trx);
}

static void tci_cmd_cw_macros_speed (CLIENT *client, const TCI_CMD *cmd) {
  (void) cmd;
  tci_send_macros_cwspeed (client);
}

static void tci_cmd_cw_keyer_speed (CLIENT *client, const TCI_CMD *cmd) {
  (void) cmd;
  tci_send_keyer_cwspeed (client);
}

static void tci_cmd_cw_macros_delay (CLIENT *client, const TCI_CMD *cmd) {
  (void) cmd;
  tci_send_text (client, "cw_macros_delay:10;");
}

static void tci_cmd_stop (CLIENT *client, const TCI_CMD *cmd) {
  (void) cmd;
  client->rxsensor = 0;
  client->txsensor = 0;
  tci_send_text (client, "stop;");
  g_mutex_lock (&tci_mutex);
  client->running = 0;
  g_mutex_unlock (&tci_mutex);
}

static const TCI_DISPATCH tci_dispatch[] = {
  { "trx_count",         0,  0, tci_cmd_trx_count },
  { "trx",               0, -1, tci_cmd_trx },
  { "tune",              0, -1, tci_cmd_tune },
  { "audio_samplerate",            0, -1, tci_cmd_audio_samplerate },
  { "audio_stream_sample_type",    0, -1, tci_cmd_audio_stream_sample_type },
  { "audio_stream_channels",       0, -1, tci_cmd_audio_stream_channels },
  { "audio_stream_samples",        0, -1, tci_cmd_audio_stream_samples },
  { "rx_sensors_enable", 1,  2, tci_cmd_rx_sensors_enable },
  { "tx_sensors_enable", 1,  2, tci_cmd_tx_sensors_enable },
  { "audio_start",       1,  1, tci_cmd_audio_start },
  { "audio_stop",        1,  1, tci_cmd_audio_stop },
  { "modulation",        1,  2, tci_cmd_modulation },
  { "vfo",               2,  3, tci_cmd_vfo },
  { "rx_smeter",         1,  3, tci_cmd_rx_smeter },
  { "drive",             1,  2, tci_cmd_drive },
  { "cw_macros_speed",   0,  0, tci_cmd_cw_macros_speed },
  { "cw_keyer_speed",    0,  0, tci_cmd_cw_keyer_speed },
  { "cw_macros_delay",   0,  0, tci_cmd_cw_macros_delay },
  { "stop",              0,  0, tci_cmd_stop },
  { NULL,                0,  0, NULL }
};

static void tci_handle_text (CLIENT *client, char* msg) {
  TCI_CMD cmd;
  if (tci_parse_text (msg, &cmd) < 0 || cmd.cmd == NULL) { return; }
  for (char * p = cmd.cmd; *p != 0; p++) {
    *p = g_ascii_tolower (*p);
  }
  if (rigctl_debug) {
    t_print ("TCI%d command=%s argc=%d\n", client->seq, cmd.cmd, cmd.argc);
    for (int i = 0; i < cmd.argc; i++) {
      t_print ("  arg[%d]=%s\n", i, cmd.argv[i] ? cmd.argv[i] : "(null)");
    }
  }
  bool handled = false;
  for (int i = 0; tci_dispatch[i].name != NULL; i++) {
    const TCI_DISPATCH *d = &tci_dispatch[i];
    if (cmd.cmd[0] != d->name[0] || strcmp (cmd.cmd, d->name) != 0) { continue; }
    handled = true;
    if (cmd.argc < d->min_args) {
      t_print ("TCI%d %s: too few args (%d < %d)\n", client->seq, d->name, cmd.argc, d->min_args);
      return;
    }
    if (d->max_args >= 0 && cmd.argc > d->max_args) {
      t_print ("TCI%d %s: too many args (%d > %d)\n", client->seq, d->name, cmd.argc, d->max_args);
      return;
    }
    d->handler (client, &cmd);
    return;
  }
  if (!handled && rigctl_debug) {
    t_print ("TCI%d unknown command: %s\n",
             client->seq, cmd.cmd ? cmd.cmd : "(null)");
  }
}

static void tci_send_smeter (CLIENT *client, int v) {
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
  snprintf (msg, MAXMSGSIZE, "rx_sensors:%d,%d.0;", v, lvl);
  tci_send_text (client, msg);
}

static void tci_send_rx (CLIENT *client, int v) {
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
  snprintf (msg, MAXMSGSIZE, "rx_channel_sensors:%d,0,%d.0;", v, lvl);
  tci_send_text (client, msg);
  snprintf (msg, MAXMSGSIZE, "rx_channel_sensors:%d,1,%d.0;", v, lvl);
  tci_send_text (client, msg);
}

__attribute__ ((unused)) static void tci_send_ping (CLIENT *client) {
  if (rigctl_debug && client != NULL) { t_print ("TCI%d PING\n", client->seq); }
  (void) tci_queue_frame (client, opPING, NULL, 0);
}

static void tci_send_pong (CLIENT *client) {
  if (rigctl_debug && client != NULL) { t_print ("TCI%d PONG\n", client->seq); }
  (void) tci_queue_frame (client, opPONG, NULL, 0);
}

static gboolean tci_reporter (gpointer data) {
  //
  // This function is called repeatedly as long as the client  runs
  //
  CLIENT *client = (CLIENT*) data;
  g_mutex_lock (&tci_mutex);
  if (!client->running) {
    g_mutex_unlock (&tci_mutex);
    return FALSE;
  }
  g_mutex_unlock (&tci_mutex);
#ifdef __APPLE__
  struct timespec ts;
  // clock_gettime(CLOCK_REALTIME, &ts);
  clock_gettime (CLOCK_MONOTONIC, &ts);
  tci_timer += ts.tv_sec;
#endif
  if (++ (client->count) >= 30) {
    client->count = 0;
    // tci_send_ping(client);
  }
  //
  // Determine TX frequency  and  report  if changed
  //
  long long fx = vfo_get_tx_freq();
  if (fx != client->last_fx) {
    tci_send_txfreq (client);
  }
  int sp = (vfo_get_tx_vfo() == VFO_B);
  int mx = radio_is_transmitting();
  if (sp != client->last_split) {
    tci_send_split (client);
  }
  if (!tci_is_tune_transition() && mx != client->last_mox) {
    tci_send_mox (client);
  }
  if (!tci_txonly) {
    //
    // If S-meter reading is requested, send info each time
    //
    if (client->rxsensor && (client->count & 1)) {
      tci_send_rx (client, 0);
      tci_send_rx (client, 1);
    }
    if (client->txsensor && (client->count & 1)) {
      tci_send_drive (client, 0);
    }
    if (receivers > 0 && client->rxsensor && (client->count & 1)) {
      if (receivers == 1) {
        tci_send_smeter (client, 0);
      } else {
        tci_send_smeter (client, 0);
        tci_send_smeter (client, 1);
      }
    }
    //
    // Determine VFO-A/B frequency/mode, report if changed
    //
    long long fa = vfo[VFO_A].ctun ? vfo[VFO_A].ctun_frequency : vfo[VFO_A].frequency;
    long long fb = vfo[VFO_B].ctun ? vfo[VFO_B].ctun_frequency : vfo[VFO_B].frequency;
    int       ma = vfo[VFO_A].mode;
    int       mb = vfo[VFO_B].mode;
    if (fa != client->last_fa) {
      tci_send_vfo (client, 0, 0);
    }
    if (fb != client->last_fb) {
      tci_send_vfo (client, 0, 1);
      tci_send_vfo (client, 1, 0);
      tci_send_vfo (client, 1, 1);
    }
    if (ma  != client->last_ma) {
      tci_send_mode (client, 0);
    }
    if (mb  != client->last_mb) {
      tci_send_mode (client, 1);
    }
  }
  return TRUE;
}

//
// Initialise TCI client state.
//
static void tci_init_client (CLIENT *client, int fd, int seq) {
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
  client->wsi             = NULL;
  client->lws_tx_queue    = NULL;
  client->initial_sent    =  0;
  client->device          = NULL;
  client->device_index    = -1;
  for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
    client->rx_audio_enabled[i] = 0;
    client->rx_audio_read_count[i] = 0;
  }
}

static void tci_process_ws_payload (CLIENT *client, int type, char* msg) {
  if (client == NULL) { return; }
  switch (type) {
  case opTEXT:
    if (rigctl_debug) {
      t_print ("TCI%d command rcvd=%s\n", client->seq, msg);
    }
    tci_handle_text (client, msg);
    break;
  case opPING:
    if (rigctl_debug) { t_print ("TCI%d PING rcvd\n", client->seq); }
    tci_send_pong (client);
    break;
  case opCLOSE:
    if (rigctl_debug) { t_print ("TCI%d CLOSE rcvd\n", client->seq); }
    g_mutex_lock (&tci_mutex);
    client->running = 0;
    g_mutex_unlock (&tci_mutex);
    break;
  default:
    if (rigctl_debug) {
      t_print ("TCI%d unknown frame type=%d ignored\n", client->seq, type);
    }
    break;
  }
}

static void tci_send_initial_state (CLIENT *client) {
  //
  // Send initial state info to client
  // using emulatation Expert SunSDR2Pro
  //
  // tci_send_text(client, "protocol:ExpertSDR3,1.8;");
  // tci_send_text(client, "device:SunSDR2PRO;");
  tci_send_text (client, "protocol:ExpertSDR3,2.0;");
  tci_send_text (client, "device:SunSDR2QRP;");
  tci_send_text (client, can_transmit ? "receive_only:false;" : "receive_only:true;");
  tci_send_trx_count (client);
  tci_send_text (client, "channels_count:2;");
  //
  // With transverters etc. the upper frequency can be
  // very large. For the time being we go up to the 70cm band
  // No need to send vfo and modulation  commands, since this is
  // automatically  done in the tci_reporter task.
  //
  // tci_send_text(client, "vfo_limits:0,450000000;");
  // tci_send_text(client, "if_limits:-96000,96000;");
  tci_send_limits (client);
  tci_send_text (client, "modulations_list:LSB,USB,DSB,CW,FMN,AM,DIGU,SPEC,DIGL,SAM,DRM;");
  tci_send_dds (client, VFO_A);
  tci_send_text (client, "if:0,0,0;");
  tci_send_text (client, "if:0,1,0;");
  tci_send_vfo (client, VFO_A, 0);
  tci_send_vfo (client, VFO_A, 1);
  tci_send_mode (client, VFO_A);
  if (receivers > 1) {
    tci_send_dds (client, VFO_B);
    tci_send_text (client, "if:1,0,0;");
    tci_send_text (client, "if:1,1,0;");
    tci_send_vfo (client, VFO_B, 0);
    tci_send_vfo (client, VFO_B, 1);
    tci_send_mode (client, VFO_B);
  }
  tci_send_text (client, "rx_enable:0,true;");
  if (receivers > 1) {
    tci_send_text (client, "rx_enable:1,true;");
  }
  tci_send_tx_enable (client);
  tci_send_split (client);
  tci_send_mox (client);
  tci_send_tune (client);
  tci_send_text (client, "mute:false;");
  tci_send_macros_cwspeed (client);
  tci_send_text (client, "cw_macros_delay:10;");
  tci_send_keyer_cwspeed (client);
  tci_send_text (client, "start;");
  tci_send_text (client, "ready;");
}

static void tci_lws_free_queue (CLIENT *client) {
  GQueue *queue;
  if (client == NULL) { return; }
  g_mutex_lock (&tci_mutex);
  queue = client->lws_tx_queue;
  client->lws_tx_queue = NULL;
  client->idle_queued = 0;
  g_mutex_unlock (&tci_mutex);
  if (queue == NULL) { return; }
  while (!g_queue_is_empty (queue)) {
    RESPONSE *resp = (RESPONSE*) g_queue_pop_head (queue);
    g_free (resp->bin);
    g_free (resp);
  }
  g_queue_free (queue);
}

static int tci_lws_write_queued (CLIENT *client) {
  RESPONSE *resp = NULL;
  struct lws *wsi;
  if (client == NULL) { return 0; }
  g_mutex_lock (&tci_mutex);
  wsi = client->wsi;
  if (client->lws_tx_queue != NULL && !g_queue_is_empty (client->lws_tx_queue)) {
    resp = (RESPONSE*) g_queue_pop_head (client->lws_tx_queue);
  }
  g_mutex_unlock (&tci_mutex);
  if (resp == NULL) { return 0; }
  if (resp->type == opCLOSE || wsi == NULL) {
    g_mutex_lock (&tci_mutex);
    if (client->idle_queued > 0) {
      client->idle_queued--;
    }
    g_mutex_unlock (&tci_mutex);
    g_free (resp);
    return -1;
  }
  size_t len = (resp->type == opBIN) ? resp->len : strlen (resp->msg);
  enum lws_write_protocol protocol = LWS_WRITE_TEXT;
  unsigned char *buf = g_malloc (LWS_PRE + len);
  int rc;
  if (resp->type == opBIN) {
    protocol = LWS_WRITE_BINARY;
    memcpy (&buf[LWS_PRE], resp->bin, len);
  } else if (resp->type == opPING) {
    protocol = LWS_WRITE_PING;
    memcpy (&buf[LWS_PRE], resp->msg, len);
  } else if (resp->type == opPONG) {
    protocol = LWS_WRITE_PONG;
    memcpy (&buf[LWS_PRE], resp->msg, len);
  } else {
    memcpy (&buf[LWS_PRE], resp->msg, len);
  }
  rc = lws_write (wsi, &buf[LWS_PRE], len, protocol);
  g_free (buf);
  g_free (resp->bin);
  g_free (resp);
  g_mutex_lock (&tci_mutex);
  if (client->idle_queued > 0) {
    client->idle_queued--;
  }
  if (rc < 0) {
    client->running = 0;
    g_mutex_unlock (&tci_mutex);
    return -1;
  }
  if (client->wsi != NULL && client->lws_tx_queue != NULL && !g_queue_is_empty (client->lws_tx_queue)) {
    lws_callback_on_writable (client->wsi);
  }
  g_mutex_unlock (&tci_mutex);
  return 0;
}

static int tci_lws_callback (struct lws *wsi, enum lws_callback_reasons reason,
                             void *user, void *in, size_t len) {
  CLIENT *client = (CLIENT*) user;
  switch (reason) {
  case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
    if (rigctl_debug) {
      char uri[256] = {0};
      char proto[256] = {0};
      char ua[256] = {0};
      lws_hdr_copy (wsi, uri,   sizeof (uri),   WSI_TOKEN_GET_URI);
      lws_hdr_copy (wsi, proto, sizeof (proto), WSI_TOKEN_PROTOCOL);
      lws_hdr_copy (wsi, ua,    sizeof (ua),    WSI_TOKEN_HTTP_USER_AGENT);
      t_print ("LWS HANDSHAKE uri=%s\n", uri);
      t_print ("LWS HANDSHAKE protocol=%s\n", proto);
      t_print ("LWS HANDSHAKE user-agent=%s\n", ua);
    }
    return 0;
  case LWS_CALLBACK_HTTP:
    if (rigctl_debug) {
      char uri[256];
      uri[0] = 0;
      lws_hdr_copy (wsi, uri, sizeof (uri), WSI_TOKEN_GET_URI);
      t_print ("LWS HTTP uri=%s\n", uri);
    }
    return 0;
  case LWS_CALLBACK_ESTABLISHED:
    if (rigctl_debug) {
      char proto[128];
      char uri[256];
      proto[0] = 0;
      uri[0] = 0;
      lws_hdr_copy (wsi, proto, sizeof (proto), WSI_TOKEN_PROTOCOL);
      lws_hdr_copy (wsi, uri, sizeof (uri), WSI_TOKEN_GET_URI);
      t_print ("LWS ESTABLISHED uri=%s protocol=%s\n", uri, proto);
    }
    tci_init_client (client, lws_get_socket_fd (wsi), ++tci_lws_seq);
    client->wsi = wsi;
    client->lws_tx_queue = g_queue_new();
    client->device_index = active_device_index;
    client->device = &discovered[client->device_index];
    g_mutex_lock (&tci_mutex);
    tci_clients = g_list_append (tci_clients, client);
    g_mutex_unlock (&tci_mutex);
    t_print ("%s: starting client: socket=%d\n", __func__, client->seq);
    cat_control++;
    g_idle_add (ext_vfo_update, NULL);
    client->initial_sent = 0;
    lws_callback_on_writable (wsi);
    client->tci_timer = g_timeout_add (500, tci_reporter, client);
    break;
  case LWS_CALLBACK_RECEIVE:
    if (lws_frame_is_binary (wsi)) {
      tci_handle_binary_lws (client, (const unsigned char*) in, len, wsi);
      break;
    } else {
      if (rigctl_debug) { t_print ("LWS RECEIVE len=%zu\n", len); }
      char msg[MAXDATASIZE];
      size_t n = (len < sizeof (msg) - 1) ? len : sizeof (msg) - 1;
      memcpy (msg, in, n);
      msg[n] = 0;
      tci_process_ws_payload (client, opTEXT, msg);
    }
    break;
  case LWS_CALLBACK_SERVER_WRITEABLE:
    if (client == NULL || !client->running || client->wsi == NULL) { return 0; }
    // if (rigctl_debug && client->lws_tx_queue != NULL && !g_queue_is_empty(client->lws_tx_queue)) {
    //  t_print("LWS WRITEABLE queued=%u\n", g_queue_get_length(client->lws_tx_queue));
    // }
    if (!client->initial_sent) {
      tci_send_initial_state (client);
      client->initial_sent = 1;
    }
    tci_service_tx_chrono();
    return tci_lws_write_queued (client);
  case LWS_CALLBACK_CLOSED:
    if (rigctl_debug) {
      t_print ("LWS CLOSED client=%d\n", client ? client->seq : -1);
    }
    if (client == NULL) { break; }
    g_mutex_lock (&tci_mutex);
    client->tx_audio_enabled = 0;
    client->tx_chrono_next_us = 0;
    client->running = 0;
    client->wsi = NULL;
    tci_clients = g_list_remove (tci_clients, client);
    g_mutex_unlock (&tci_mutex);
    tci_update_rx_audio_global();
    if (client->tci_timer != 0) {
      g_source_remove (client->tci_timer);
      client->tci_timer = 0;
    }
    tci_lws_free_queue (client);
    g_free (client->binary_rx_buf);
    client->binary_rx_buf = NULL;
    client->binary_rx_len = 0;
    client->binary_rx_size = 0;
    t_print ("%s: leaving client\n", __func__);
    if (cat_control > 0) {
      cat_control--;
    }
    g_idle_add (ext_vfo_update, NULL);
    break;
  default:
    break;
  }
  return 0;
}

static const struct lws_protocols tci_lws_protocols[] = {
  { "chat",       tci_lws_callback, sizeof (CLIENT), 8192, 0, NULL, 0 },
  { "superchat",  tci_lws_callback, sizeof (CLIENT), 8192, 0, NULL, 0 },
  { "tci",        tci_lws_callback, sizeof (CLIENT), 8192, 0, NULL, 0 },
  LWS_PROTOCOL_LIST_TERM
};

static gpointer tci_lws_server (gpointer data) {
  struct lws_context_creation_info info;
  int port = GPOINTER_TO_INT (data);
  memset (&info, 0, sizeof (info));
  signal (SIGPIPE, SIG_IGN);
  info.port = port;
  info.protocols = tci_lws_protocols;
  info.gid = -1;
  info.uid = -1;
  // WICHTIG: HTTP/WS korrekt aktivieren
  info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  t_print ("%s: starting TCI LWS server on port %d\n", __func__, port);
  tci_lws_context = lws_create_context (&info);
  if (tci_lws_context == NULL) {
    t_print ("%s: lws_create_context failed\n", __func__);
    return NULL;
  }
  while (tci_running) {
    int do_writable = 0;
    tci_service_rx_audio();
    tci_service_tx_chrono();
    g_mutex_lock (&tci_mutex);
    if (tci_lws_pending_writable) {
      tci_lws_pending_writable = 0;
      do_writable = 1;
    }
    g_mutex_unlock (&tci_mutex);
    if (do_writable) {
      GList *clients = tci_clients_snapshot();
      for (GList *l = clients; l != NULL; l = l->next) {
        CLIENT *client = (CLIENT*) l->data;
        struct lws *wsi = NULL;
        g_mutex_lock (&tci_mutex);
        if (client != NULL && client->running && client->wsi != NULL &&
            client->lws_tx_queue != NULL && !g_queue_is_empty (client->lws_tx_queue)) {
          wsi = client->wsi;
        }
        g_mutex_unlock (&tci_mutex);
        if (wsi != NULL) {
          lws_callback_on_writable (wsi);
        }
      }
      g_list_free (clients);
    }
    lws_service (tci_lws_context, 0);
    g_usleep (1000);
  }
  lws_context_destroy (tci_lws_context);
  tci_lws_context = NULL;
  return NULL;
}
