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
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <math.h>

#ifdef __APPLE__
  #include <time.h>
#endif

#include <libwebsockets.h>
#include <wdsp.h>

#include "radio.h"
#include "vfo.h"
#include "rigctl.h"
#include "ext.h"
#include "message.h"
#include "toolset.h"
#include "main.h"
#include "discovery.h"
#include "tci_audio.h"
#include "cw_engine.h"
#include "audio.h"
#include "transmitter.h"
#include "band.h"
#include "filter.h"
#include "agc.h"
#include "sliders.h"
#include "noise_menu.h"
#include "receiver.h"
#include "rx_panadapter.h"

#define MAXDATASIZE     1024
#define MAXMSGSIZE      512
#define TCI_MAX_ARGS 16
#define TCI_BINARY_REASSEMBLY_MAX 65536

#ifndef LWS_PROTOCOL_LIST_TERM
  #define LWS_PROTOCOL_LIST_TERM { NULL, NULL, 0, 0, 0, NULL, 0 }
#endif

int tci_enable = 0;
int tci_port   = 40001;
int tci_txonly = 0;
long tci_timer = 0;
extern gboolean tci_debug;

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
static struct lws_context *tci_lws_context = NULL;
static int tci_lws_seq = 0;
static int tci_lws_pending_writable = 0;
static int tci_apply_in_progress = 0;
static int tci_tune_transition = 0;
static char tci_cw_msg_pending_callsign[MAXMSGSIZE];
static char tci_cw_msg_active_callsign[MAXMSGSIZE];
static char tci_cw_msg_active_suffix[MAXMSGSIZE];
static int tci_cw_msg_active = 0;
static int tci_cw_msg_call_pos = 0;
static int tci_cw_msg_call_repeat = 1;
static int tci_cw_msg_call_repeat_index = 0;
static int tci_cw_msg_suffix_pending = 0;
static int tci_cw_macros_delay_ms = 10;
static gint tci_iq_stream_clients = 0;
static int tci_iq_stream_sample_rate = 0;

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
  int txsensor;                 // enable transmit of TX sensor data
  int rxsensor_interval_ms;     // RX sensor send interval in ms
  int txsensor_interval_ms;     // TX sensor send interval in ms
  gint64 rxsensor_last_us;      // last RX sensor send timestamp
  gint64 txsensor_last_us;      // last TX sensor send timestamp
  int iq_stream_enabled[TCI_RX_AUDIO_MAX_RECEIVERS];
  double iq_cw_phase[TCI_RX_AUDIO_MAX_RECEIVERS];
  int iq_sample_rate;
  int idle_queued;              // counter
  struct lws *wsi;              // libwebsockets connection
  GQueue *lws_tx_queue;         // queued RESPONSE objects for LWS writable callback
  int initial_sent;             // initial state already sent via LWS
  DISCOVERED *device;           // device bound to this TCI client
  int device_index;             // discovery index bound to this TCI client
  int rx_audio_enabled[TCI_RX_AUDIO_MAX_RECEIVERS];
  guint64 rx_audio_read_count[TCI_RX_AUDIO_MAX_RECEIVERS];
  guint64 rx_audio_queue_count[TCI_RX_AUDIO_MAX_RECEIVERS];
  guint64 rx_audio_empty_count[TCI_RX_AUDIO_MAX_RECEIVERS];
  void *rx_audio_resampler_l[TCI_RX_AUDIO_MAX_RECEIVERS];
  void *rx_audio_resampler_r[TCI_RX_AUDIO_MAX_RECEIVERS];
  int audio_sample_rate;
  void *tx_audio_resampler_24_to_48;
  int tx_audio_enabled;
  gint64 tx_chrono_next_us;
  guint tx_chrono_tick;
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
static CLIENT *tci_iq_stream_owner = NULL;
static CLIENT *tci_digi_offset_owner = NULL;
static CLIENT *tci_tx_owner = NULL;

typedef enum {
  TCI_TX_OWNER_NONE = 0,
  TCI_TX_OWNER_MOX,
  TCI_TX_OWNER_TUNE
} TCI_TX_OWNER_MODE;

static TCI_TX_OWNER_MODE tci_tx_owner_mode = TCI_TX_OWNER_NONE;

typedef enum {
  TCI_SET_LOCK_RIT,
  TCI_SET_LOCK_XIT,
  TCI_SET_LOCK_LOCK,
  TCI_SET_LOCK_SPLIT,
  TCI_SET_LOCK_MUTE,
  TCI_SET_LOCK_RX_MUTE,
  TCI_SET_LOCK_RX_DSP,
  TCI_SET_LOCK_VOLUME,
  TCI_SET_LOCK_AGC,
  TCI_SET_LOCK_SQL,
  TCI_SET_LOCK_MODE,
  TCI_SET_LOCK_VFO,
  TCI_SET_LOCK_DRIVE,
  TCI_SET_LOCK_FILTER,
  TCI_SET_LOCK_CW_SPEED,
  TCI_SET_LOCK_IQ_RATE,
  TCI_SET_LOCK_COUNT
} TCI_SET_LOCK_ID;

typedef struct {
  CLIENT *owner;
  gint64 until_us;
} TCI_SET_LOCK;

static TCI_SET_LOCK tci_set_locks[TCI_SET_LOCK_COUNT];

static gpointer tci_lws_server (gpointer data);
static void tci_lws_free_queue (CLIENT *client);
static int tci_has_audio_monitor_source (void);
static void tci_update_rx_audio_global (void);
static void tci_audio_wakeup (void);
static void tci_audio_tx_chrono_wakeup (void);
static void tci_service_tx_chrono (void);
static void tci_lws_binary_reset (CLIENT *client);
static void tci_handle_binary_lws (CLIENT *client, const unsigned char* data, size_t len, struct lws *wsi);
static void tci_handle_binary (CLIENT *client, const unsigned char* data, size_t len);
static void tci_send_iq_samplerate (CLIENT *client);
static void tci_send_audio_samplerate (CLIENT *client);
static void tci_send_audio_stream_samples (CLIENT *client);
static int tci_set_lock_allowed(CLIENT *client, TCI_SET_LOCK_ID lock_id);
static void tci_set_locks_clear_client(CLIENT *client);
static void tci_set_locks_clear_all(void);
static void tci_tx_client_cleanup_tx_audio(CLIENT *client);
static void tci_tx_owner_sync_local_state(void);

typedef struct {
  char *cmd;
  int argc;
  char *argv[TCI_MAX_ARGS];
} TCI_CMD;

typedef struct {
  int receiver_id;
  int state;
} TCI_RX_MUTE_UPDATE;

typedef struct {
  int receiver_id;
  int state;
} TCI_RIT_ENABLE_UPDATE;

typedef struct {
  int receiver_id;
  long long value;
} TCI_RIT_OFFSET_UPDATE;

typedef struct {
  int is_digu;
  int value;
} TCI_DIGI_OFFSET_UPDATE;

typedef struct {
  int receiver_id;
  int state;
} TCI_SQL_ENABLE_UPDATE;

typedef struct {
  int receiver_id;
  int state;
} TCI_RX_ANF_ENABLE_UPDATE;

typedef struct {
  int receiver_id;
  int state;
} TCI_RX_NF_ENABLE_UPDATE;

typedef struct {
  int receiver_id;
  int state;
} TCI_RX_NB_ENABLE_UPDATE;

typedef struct {
  int receiver_id;
  int state;
} TCI_RX_BIN_ENABLE_UPDATE;

typedef struct {
  int receiver_id;
  int state;
} TCI_RX_NR_ENABLE_UPDATE;

typedef struct {
  int receiver_id;
  int state;
} TCI_RX_APF_ENABLE_UPDATE;

typedef struct {
  int receiver_id;
  double level_db;
} TCI_SQL_LEVEL_UPDATE;

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
static void tci_send_rx_filter_band (CLIENT *client, int v);
static void tci_send_text (CLIENT *client, const char* msg);
static void tci_update_iq_stream_global (void);
static void tci_send_iq_stream_start (CLIENT *client, int receiver_id);
static void tci_send_iq_stream_stop (CLIENT *client, int receiver_id);
static int tci_queue_frame (CLIENT *client, int type, const char* msg, int check_running);
static GList *tci_clients_snapshot (void);
static void tci_cw_macros_empty (void);

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



//
// Launch TCI system. Called upon program start if TCI is
// enabled in the props file, and from the CAT/TCI menu
// if TCI is enabled there.
//
void launch_tci (void) {
  t_print ("---- LAUNCHING TCI LWS SERVER ----\n");
  tci_audio_set_wakeup_callback (tci_audio_wakeup);
  tci_audio_set_tx_chrono_callback (tci_audio_tx_chrono_wakeup);
  cw_engine_set_start_delay(tci_cw_macros_delay_ms);
  cw_engine_set_empty_callback(tci_cw_macros_empty);
  tci_running = 1;
  tci_server_thread_id = g_thread_new ("tci lws server", tci_lws_server, GINT_TO_POINTER (tci_port));
}

static int tci_has_clients(void) {
  int have_clients;
  GList *clients = tci_clients_snapshot();
  have_clients = (clients != NULL);
  g_list_free(clients);
  return have_clients;
}

void tci_send_stop_and_flush(void) {
  if (!tci_running || tci_lws_context == NULL) {
    return;
  }
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT *) l->data;
    if (client != NULL && client->wsi != NULL) {
      client->rxsensor = 0;
      client->txsensor = 0;
      for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
        client->iq_stream_enabled[i] = 0;
        client->iq_cw_phase[i] = 0.0;
      }
      (void) tci_queue_frame(client, opTEXT, "stop;", 0);
    }
  }
  g_list_free(clients);
  g_mutex_lock (&tci_mutex);
  tci_iq_stream_sample_rate = 0;
  tci_iq_stream_owner = NULL;
  tci_digi_offset_owner = NULL;
  tci_tx_owner = NULL;
  tci_tx_owner_mode = TCI_TX_OWNER_NONE;
  tci_set_locks_clear_all();
  g_mutex_unlock (&tci_mutex);
  g_atomic_int_set (&tci_iq_stream_clients, 0);
  lws_cancel_service(tci_lws_context);
  for (int i = 0; i < 20 && tci_has_clients(); i++) {
    lws_cancel_service(tci_lws_context);
    g_usleep(10000);
  }
}

//
// Shut down TCI system. Called from CAT/TCI menu
// if TCI is disabled there.
//
void shutdown_tci (void) {
  t_print ("%s\n", __func__);
  tci_audio_set_active (0);
  tci_audio_set_wakeup_callback (NULL);
  tci_audio_set_tx_chrono_callback (NULL);
  cw_engine_set_empty_callback(NULL);
  if (tci_lws_context != NULL) {
    GList *clients = tci_clients_snapshot();
    for (GList *l = clients; l != NULL; l = l->next) {
      CLIENT *client = (CLIENT *) l->data;
      if (client != NULL && client->wsi != NULL) {
        client->rxsensor = 0;
        client->txsensor = 0;
        for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
          client->iq_stream_enabled[i] = 0;
        }
        (void) tci_queue_frame(client, opTEXT, "stop;", 0);
        client->running = 0;
        lws_set_timeout(client->wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
      }
    }
    g_list_free(clients);
    g_mutex_lock (&tci_mutex);
    tci_iq_stream_sample_rate = 0;
    tci_iq_stream_owner = NULL;
    tci_digi_offset_owner = NULL;
    tci_tx_owner = NULL;
    tci_tx_owner_mode = TCI_TX_OWNER_NONE;
    tci_set_locks_clear_all();
    g_mutex_unlock (&tci_mutex);
    g_atomic_int_set (&tci_iq_stream_clients, 0);
    lws_cancel_service (tci_lws_context);
    for (int i = 0; i < 50 && tci_has_clients(); i++) {
      lws_cancel_service (tci_lws_context);
      g_usleep(10000);
    }
  }
  tci_running = 0;
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

static int tci_iq_sample_rate_valid (int samplerate) {
  return samplerate == 48000 || samplerate == 96000 ||
         samplerate == 192000 || samplerate == 384000;
}

static int tci_iq_current_radio_sample_rate (void) {
  if (active_receiver != NULL) {
    return active_receiver->sample_rate;
  }
  return 48000;
}

static void tci_update_iq_stream_global (void) {
  int enabled = 0;
  int owner_enabled = 0;
  g_mutex_lock (&tci_mutex);
  for (GList *l = tci_clients; l != NULL && (!enabled || (tci_iq_stream_owner != NULL && !owner_enabled)); l = l->next) {
    CLIENT *client = (CLIENT *) l->data;
    if (client == NULL || !client->running) {
      continue;
    }
    for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
      if (client->iq_stream_enabled[i]) {
        enabled = 1;
        if (client == tci_iq_stream_owner) {
          owner_enabled = 1;
        }
        break;
      }
    }
  }
  if (tci_iq_stream_owner != NULL && !owner_enabled) {
    tci_iq_stream_owner = NULL;
    tci_iq_stream_sample_rate = 0;
  }
  if (!enabled) {
    tci_iq_stream_sample_rate = 0;
    tci_iq_stream_owner = NULL;
  }
  g_mutex_unlock (&tci_mutex);
  g_atomic_int_set (&tci_iq_stream_clients, enabled);
}

static int tci_iq_effective_sample_rate (CLIENT *client, int requested) {
  int samplerate;
  if (!tci_iq_sample_rate_valid (requested)) {
    requested = tci_iq_current_radio_sample_rate();
  }
  g_mutex_lock (&tci_mutex);
  if (tci_iq_stream_sample_rate != 0) {
    samplerate = tci_iq_stream_sample_rate;
  } else {
    samplerate = requested;
  }
  if (client != NULL) {
    client->iq_sample_rate = samplerate;
  }
  g_mutex_unlock (&tci_mutex);
  return samplerate;
}

void tci_rx_iq_block (RECEIVER *rx, const double *iq, guint frames) {
  unsigned char frame[sizeof(TCI_STREAM_HEADER) + (2048 * TCI_AUDIO_CHANNELS * sizeof(float))];
  TCI_STREAM_HEADER header;
  guint out_frames;
  size_t data_bytes;
  size_t frame_len;
  GList *clients;
  double cw_shift = 0.0;
  if (!g_atomic_int_get (&tci_iq_stream_clients)) { return; }
  if (rx == NULL || iq == NULL || frames == 0) { return; }
  if (rx->id < 0 || rx->id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return; }
  out_frames = frames;
  if (out_frames > 2048) {
    out_frames = 2048;
  }
  if (vfo[rx->id].mode == modeCWU) {
    cw_shift = (double) cw_keyer_sidetone_frequency;
  } else if (vfo[rx->id].mode == modeCWL) {
    cw_shift = - (double) cw_keyer_sidetone_frequency;
  }
  memset (&header, 0, sizeof (header));
  header.receiver = (uint32_t) rx->id;
  header.sample_rate = (uint32_t) rx->sample_rate;
  header.format = TCI_AUDIO_FORMAT_FLOAT32;
  header.length = (uint32_t) (out_frames * TCI_AUDIO_CHANNELS);
  header.type = 0; // IQ_STREAM
  header.channels = TCI_AUDIO_CHANNELS;
  data_bytes = (size_t) out_frames * TCI_AUDIO_CHANNELS * sizeof(float);
  frame_len = sizeof (header) + data_bytes;
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT *) l->data;
    if (client != NULL && client->running && client->iq_stream_enabled[rx->id]) {
      double phase = client->iq_cw_phase[rx->id];
      double phase_inc = 0.0;
      memcpy (frame, &header, sizeof (header));
      if (cw_shift != 0.0 && rx->sample_rate > 0) {
        phase_inc = (2.0 * G_PI * cw_shift) / (double) rx->sample_rate;
      }
      for (guint i = 0; i < out_frames; i++) {
        float fs0;
        float fs1;
        double is = iq[(i * 2)];
        double qs = iq[(i * 2) + 1];
        if (phase_inc != 0.0) {
          double c = cos(phase);
          double s = sin(phase);
          double ri = (is * c) - (qs * s);
          double rq = (is * s) + (qs * c);
          is = ri;
          qs = rq;
          phase += phase_inc;
          if (phase >= G_PI) {
            phase -= 2.0 * G_PI;
          } else if (phase < -G_PI) {
            phase += 2.0 * G_PI;
          }
        } else {
          phase = 0.0;
        }
        if (tci_iq_swap) {
          fs0 = (float) qs;
          fs1 = (float) is;
        } else {
          fs0 = (float) is;
          fs1 = (float) qs;
        }
        /*
         * This only affects the outgoing TCI IQ stream. Some TCI clients expect the
         * opposite complex spectral orientation. Conjugating here mirrors the exported
         * spectrum without changing the internal receiver, panadapter, audio or TX paths.
         */
        if (tci_iq_conjugate) {
          fs1 = -fs1;
        }
        memcpy (frame + sizeof (header) + ((i * 2) * sizeof (float)), &fs0, sizeof (float));
        memcpy (frame + sizeof (header) + (((i * 2) + 1) * sizeof (float)), &fs1, sizeof (float));
      }
      client->iq_cw_phase[rx->id] = phase;
      (void) tci_queue_binary_frame (client, frame, frame_len);
    }
  }
  g_list_free (clients);
}

static GList *tci_clients_snapshot (void) {
  GList *clients;
  g_mutex_lock (&tci_mutex);
  clients = g_list_copy (tci_clients);
  g_mutex_unlock (&tci_mutex);
  return clients;
}

static void tci_cw_msg_reset_state(void) {
  tci_cw_msg_pending_callsign[0] = 0;
  tci_cw_msg_active_callsign[0] = 0;
  tci_cw_msg_active_suffix[0] = 0;
  tci_cw_msg_active = 0;
  tci_cw_msg_call_pos = 0;
  tci_cw_msg_call_repeat = 1;
  tci_cw_msg_call_repeat_index = 0;
  tci_cw_msg_suffix_pending = 0;
}

static void tci_cw_send_to_all(const char *msg) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_text(client, msg);
    }
  }
  g_list_free(clients);
}

static int tci_cw_msg_queue_next(void) {
  int call_len;
  if (!tci_cw_msg_active) {
    return 0;
  }
  call_len = (int) strlen(tci_cw_msg_active_callsign);
  if (call_len > 0 && tci_cw_msg_call_repeat_index < tci_cw_msg_call_repeat) {
    if (tci_cw_msg_call_pos < call_len) {
      if (!cw_engine_queue_char(tci_cw_msg_active_callsign[tci_cw_msg_call_pos])) {
        return 0;
      }
      tci_cw_msg_call_pos++;
      return 1;
    }
    tci_cw_msg_call_repeat_index++;
    if (tci_cw_msg_call_repeat_index < tci_cw_msg_call_repeat) {
      tci_cw_msg_call_pos = 0;
      if (!cw_engine_queue_char(' ')) {
        return 0;
      }
      return 1;
    }
  }
  if (tci_cw_msg_pending_callsign[0] != 0) {
    char callsign_msg[MAXMSGSIZE];
    snprintf(callsign_msg, sizeof(callsign_msg), "callsign_send:%s;", tci_cw_msg_pending_callsign);
    tci_cw_send_to_all(callsign_msg);
    tci_cw_msg_pending_callsign[0] = 0;
  }
  if (tci_cw_msg_suffix_pending) {
    tci_cw_msg_suffix_pending = 0;
    if (tci_cw_msg_active_suffix[0] != 0) {
      int queued;
      char suffix_text[MAXMSGSIZE];
      snprintf(suffix_text, sizeof(suffix_text), " %s", tci_cw_msg_active_suffix);
      queued = cw_engine_queue_text(suffix_text);
      if (queued > 0) {
        return 1;
      }
    }
  }
  tci_cw_msg_reset_state();
  return 0;
}

static void tci_cw_macros_empty (void) {
  if (tci_cw_msg_queue_next()) {
    return;
  }
  tci_cw_send_to_all("cw_macros_empty;");
}

static int tci_has_audio_monitor_source (void) {
  int enabled = 0;
  g_mutex_lock (&tci_mutex);
  for (GList *l = tci_clients; l != NULL && !enabled; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running && client->tx_audio_enabled) {
      enabled = 1;
      break;
    }
  }
  g_mutex_unlock (&tci_mutex);
  return enabled;
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
  if (rigctl_debug) {
    t_print ("TCI RX audio global active=%d\n", enabled);
  }
}

static void tci_audio_wakeup (void) {
  tci_lws_pending_writable = 1;
  if (tci_lws_context != NULL) {
    lws_cancel_service (tci_lws_context);
  }
}

static void tci_audio_tx_chrono_wakeup (void) {
  tci_service_tx_chrono();
  tci_lws_pending_writable = 1;
  if (tci_lws_context != NULL) {
    lws_cancel_service (tci_lws_context);
  }
}

static void tci_queue_rx_audio_frame (CLIENT *client, int receiver_id) {
  unsigned char frame[TCI_AUDIO_RX_FRAME_MAX_BYTES];
  size_t frame_len;
  guint frames;
  int queued;
  if (client == NULL || !client->running || !client->rx_audio_enabled[receiver_id]) { return; }
  frames = tci_audio_get_frame (receiver_id, &client->rx_audio_read_count[receiver_id], frame, sizeof (frame),
                                &frame_len,
                                client->audio_sample_rate,
                                &client->rx_audio_resampler_l[receiver_id],
                                &client->rx_audio_resampler_r[receiver_id]);
  if (frames == 0) {
    client->rx_audio_empty_count[receiver_id]++;
    if (tci_debug && (client->rx_audio_empty_count[receiver_id] <= 10 ||
                      (client->rx_audio_empty_count[receiver_id] % 100) == 0)) {
      t_print ("TCI%d RX_AUDIO no frame rx=%d empty_count=%llu read=%llu write=%llu enabled=%d global_active=%d sample_rate=%d\n",
               client->seq,
               receiver_id,
               (unsigned long long) client->rx_audio_empty_count[receiver_id],
               (unsigned long long) client->rx_audio_read_count[receiver_id],
               (unsigned long long) tci_audio_get_write_count (receiver_id),
               client->rx_audio_enabled[receiver_id],
               tci_audio_is_active(),
               client->audio_sample_rate);
    }
    return;
  }
  queued = tci_queue_binary_frame (client, frame, frame_len);
  client->rx_audio_queue_count[receiver_id]++;
  if (tci_debug && (client->rx_audio_queue_count[receiver_id] <= 10 ||
                    (client->rx_audio_queue_count[receiver_id] % 100) == 0)) {
    TCI_STREAM_HEADER header;
    size_t payload_bytes = (frame_len >= sizeof (header)) ? frame_len - sizeof (header) : 0;
    memset (&header, 0, sizeof (header));
    if (frame_len >= sizeof (header)) {
      memcpy (&header, frame, sizeof (header));
    }
    t_print ("TCI%d RX_AUDIO queued count=%llu rx=%d queued=%d ws_len=%zu payload=%zu receiver=%u sample_rate=%u format=%u type=%u length=%u channels=%u frames=%u read=%llu write=%llu enabled=%d global_active=%d\n",
             client->seq,
             (unsigned long long) client->rx_audio_queue_count[receiver_id],
             receiver_id,
             queued,
             frame_len,
             payload_bytes,
             header.receiver,
             header.sample_rate,
             header.format,
             header.type,
             header.length,
             header.channels,
             frames,
             (unsigned long long) client->rx_audio_read_count[receiver_id],
             (unsigned long long) tci_audio_get_write_count (receiver_id),
             client->rx_audio_enabled[receiver_id],
             tci_audio_is_active());
  }
}


static int tci_queue_tx_chrono_frame (CLIENT *client) {
  TCI_STREAM_HEADER header;
  int queued;
  if (client == NULL || !client->running || !client->tx_audio_enabled) { return 0; }
  memset (&header, 0, sizeof (header));
  header.receiver = 0;
  header.sample_rate = (uint32_t) client->audio_sample_rate;
  header.format = TCI_AUDIO_FORMAT_FLOAT32;
  header.length = (uint32_t) (tci_audio_get_stream_frames() * TCI_AUDIO_CHANNELS);
  header.type = TCI_STREAM_TX_CHRONO;
  header.channels = TCI_AUDIO_CHANNELS;
  queued = tci_queue_binary_frame (client, (const unsigned char*) &header, sizeof (header));
  if (queued) {
    client->tx_chrono_queue_count++;
    if (tci_debug && (client->tx_chrono_queue_count <= 10 || (client->tx_chrono_queue_count % 100) == 0)) {
      t_print ("TCI%d TX_CHRONO queued count=%llu ws_len=%zu receiver=%u sample_rate=%u format=%u type=%u length=%u channels=%u requested_payload_bytes=%zu stream_frames=%u tx_audio_enabled=%d\n",
               client->seq,
               (unsigned long long) client->tx_chrono_queue_count,
               sizeof (header),
               header.receiver,
               header.sample_rate,
               header.format,
               header.type,
               header.length,
               header.channels,
               (size_t) header.length * sizeof (float),
               tci_audio_get_stream_frames(),
               client->tx_audio_enabled);
    }
  } else if (tci_debug) {
    t_print ("TCI%d TX chrono queue FAILED enabled=%d running=%d\n",
             client->seq,
             client->tx_audio_enabled,
             client->running);
  }
  return queued;
}

static void tci_service_tx_chrono (void) {
  GList *clients;
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    int send_chrono = 0;
    if (client == NULL || !client->running) { continue; }
    g_mutex_lock (&tci_mutex);
    if (client->tx_audio_enabled) {
      if (client->audio_sample_rate == TCI_AUDIO_SAMPLE_RATE_24K) {
        client->tx_chrono_tick++;
        if (client->tx_chrono_tick >= 2) {
          client->tx_chrono_tick = 0;
          send_chrono = 1;
        }
      } else {
        client->tx_chrono_tick = 0;
        send_chrono = 1;
      }
    } else {
      client->tx_chrono_next_us = 0;
      client->tx_chrono_tick = 0;
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
      guint64 ring_write = 0;
      guint64 ring_read = 0;
      guint64 ring_available = 0;
      guint ring_dropped = 0;
      int ring_enabled = 0;
      client->tx_audio_rx_count++;
      if (tci_debug && (client->tx_audio_rx_count <= 10 || (client->tx_audio_rx_count % 100) == 0)) {
        size_t payload_bytes = (len >= sizeof (TCI_STREAM_HEADER)) ? (len - sizeof (TCI_STREAM_HEADER)) : 0;
        size_t expected_bytes = sizeof (TCI_STREAM_HEADER) + ((size_t) header.length * sizeof (float));
        guint frames_by_header_channels = (header.channels > 0) ? (guint) (header.length / header.channels) : 0;
        guint frames_as_stereo = (guint) (header.length / TCI_AUDIO_CHANNELS);
        t_print ("TCI%d RX TX_AUDIO count=%llu ws_len=%zu payload=%zu expected=%zu receiver=%u sample_rate=%u format=%u type=%u length=%u channels=%u frames_by_channels=%u frames_as_stereo=%u client_audio_rate=%d tx_audio_enabled=%d\n",
                 client->seq,
                 (unsigned long long) client->tx_audio_rx_count,
                 len,
                 payload_bytes,
                 expected_bytes,
                 header.receiver,
                 header.sample_rate,
                 header.format,
                 header.type,
                 header.length,
                 header.channels,
                 frames_by_header_channels,
                 frames_as_stereo,
                 client->audio_sample_rate,
                 client->tx_audio_enabled);
      }
      tci_audio_handle_tx_frame (data, len, client->audio_sample_rate,
                                 &client->tx_audio_resampler_24_to_48);
      if (tci_debug && (client->tx_audio_rx_count <= 10 || (client->tx_audio_rx_count % 100) == 0)) {
        tci_audio_tx_debug_snapshot (&ring_write, &ring_read, &ring_dropped, &ring_available, &ring_enabled);
        t_print ("TCI%d TX audio ring after push count=%llu enabled=%d write=%llu read=%llu available=%llu dropped=%u\n",
                 client->seq,
                 (unsigned long long) client->tx_audio_rx_count,
                 ring_enabled,
                 (unsigned long long) ring_write,
                 (unsigned long long) ring_read,
                 (unsigned long long) ring_available,
                 ring_dropped);
      }
    } else if (tci_debug) {
      t_print ("TCI%d TX audio ignored: tx_audio_enabled=0 len=%zu receiver=%u sample_rate=%u format=%u type=%u length=%u channels=%u\n",
               client->seq,
               len,
               header.receiver,
               header.sample_rate,
               header.format,
               header.type,
               header.length,
               header.channels);
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

static void tci_broadcast_tx_footswitch_state (int state) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      if (state) {
        tci_send_text (client, "tx_footswitch:0,true;");
      } else {
        tci_send_text (client, "tx_footswitch:0,false;");
      }
    }
  }
  g_list_free (clients);
}

void tci_tx_footswitch_changed (int state) {
  if (!tci_running) { return; }
  tci_broadcast_tx_footswitch_state (state);
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

static void tci_send_lock (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "lock:%d,%s;", receiver_id, locked ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_vfo_lock (CLIENT *client, int receiver_id, int channel_id) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  if (channel_id < 0 || channel_id > 1) { return; }
  snprintf (msg, MAXMSGSIZE, "vfo_lock:%d,%d,%s;", receiver_id, channel_id, locked ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_vfo_locks (CLIENT *client, int receiver_id) {
  tci_send_vfo_lock (client, receiver_id, 0);
  tci_send_vfo_lock (client, receiver_id, 1);
}

static void tci_broadcast_lock (void) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_lock (client, VFO_A);
      tci_send_vfo_locks (client, VFO_A);
    }
  }
  g_list_free (clients);
}

void tci_lock_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_lock();
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
  if (v >= receivers) { return; }
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
  if (VfoNr >= receivers) { return; }
  if (Ch < 0 || Ch > 1) { return; }
  tci_begin_apply();
  if (VfoNr == VFO_A && Ch == 0) {
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

static void tci_send_drive (CLIENT *client) {
  char msg[MAXMSGSIZE];
  int tx_drive;
  tx_drive = radio_get_drive_as_int();
  snprintf (msg, MAXMSGSIZE, "drive:0,%d;", tx_drive);
  tci_send_text (client, msg);
}

static void tci_format_tenth (char *dst, size_t len, double value) {
  int tenths;
  int sign;
  if (dst == NULL || len == 0) {
    return;
  }
  tenths = (int) ((value * 10.0) + (value >= 0.0 ? 0.5 : -0.5));
  sign = tenths < 0;
  if (sign) {
    tenths = -tenths;
  }
  snprintf (dst, len, "%s%d.%d", sign ? "-" : "", tenths / 10, tenths % 10);
}

static void tci_send_tx_sensors (CLIENT *client) {
  char msg[MAXMSGSIZE];
  char mic_s[32];
  char rms_s[32];
  char peak_s[32];
  char swr_s[32];
  double mic;
  double rms;
  double peak;
  double swr;
  if (client == NULL || transmitter == NULL || !can_transmit) {
    return;
  }
  if (!radio_is_transmitting() || transmitter->fwd <= 0.01) {
    return;
  }
  mic = GetTXAMeter (transmitter->id, TXA_MIC_PK);
  rms = transmitter->fwd;
  peak = transmitter->fwd;
  swr = transmitter->swr;
  if (mic < -300.0) {
    mic = 0.0;
  }
  tci_format_tenth (mic_s, sizeof (mic_s), mic);
  tci_format_tenth (rms_s, sizeof (rms_s), rms);
  tci_format_tenth (peak_s, sizeof (peak_s), peak);
  tci_format_tenth (swr_s, sizeof (swr_s), swr);
  snprintf (msg, MAXMSGSIZE, "tx_sensors:0,%s,%s,%s,%s;", mic_s, rms_s, peak_s, swr_s);
  tci_send_text (client, msg);
}

static int tci_get_tune_drive_as_int (void) {
  int value;
  if (transmitter == NULL) {
    return 0;
  }
  value = transmitter->tune_use_drive ? radio_get_drive_as_int() : transmitter->tune_drive;
  if (value < 0) {
    value = 0;
  }
  if (value > 100) {
    value = 100;
  }
  return value;
}

static void tci_send_tune_drive (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "tune_drive:0,%d;", tci_get_tune_drive_as_int());
  tci_send_text (client, msg);
}


static void tci_send_rx_filter_band (CLIENT *client, int v) {
  char msg[MAXMSGSIZE];
  int mode;
  int filter_id;
  FILTER *filter;
  if (v < 0 || v >= receivers || receiver[v] == NULL) { return; }
  mode = vfo[v].mode;
  filter_id = vfo[v].filter;
  if (mode < 0 || mode >= MODES) { return; }
  if (filter_id < 0 || filter_id >= FILTERS) { return; }
  filter = &filters[mode][filter_id];
  snprintf (msg, MAXMSGSIZE, "rx_filter_band:%d,%d,%d;", v, filter->low, filter->high);
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_filter_band_value (int receiver_id, int low, int high) {
  char msg[MAXMSGSIZE];
  GList *clients = tci_clients_snapshot();
  snprintf (msg, MAXMSGSIZE, "rx_filter_band:%d,%d,%d;", receiver_id, low, high);
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_text (client, msg);
    }
  }
  g_list_free (clients);
}

void tci_rx_filter_band_changed (int receiver_id) {
  GList *clients;
  if (!tci_running) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_filter_band (client, receiver_id);
    }
  }
  g_list_free (clients);
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

static void tci_send_rx_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (client == NULL || receiver_id < 0 || receiver_id >= receivers) {
    return;
  }
  snprintf (msg, MAXMSGSIZE, "rx_enable:%d,true;", receiver_id);
  tci_send_text (client, msg);
}

static void tci_broadcast_rit_offset (int receiver_id);
static void tci_broadcast_xit_offset (void);

static void tci_send_rit_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rit_enable:%d,%s;",
            receiver_id, vfo[receiver_id].rit_enabled ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rit_enable (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rit_enable (client, receiver_id);
    }
  }
  g_list_free (clients);
}

void tci_rit_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  tci_broadcast_rit_enable (receiver_id);
  if (vfo[receiver_id].rit_enabled) {
    if (vfo[receiver_id].rit != 0) {
      tci_broadcast_rit_offset (receiver_id);
    }
  } else {
    tci_broadcast_rit_offset (receiver_id);
  }
}

static void tci_send_xit_enable (CLIENT *client) {
  char msg[MAXMSGSIZE];
  int txvfo;
  if (client == NULL) { return; }
  txvfo = vfo_get_tx_vfo();
  if (txvfo < VFO_A || txvfo > VFO_B) { return; }
  snprintf (msg, MAXMSGSIZE, "xit_enable:0,%s;",
            vfo[txvfo].xit_enabled ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_xit_enable (void) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_xit_enable (client);
    }
  }
  g_list_free (clients);
}

void tci_xit_enable_changed (void) {
  int txvfo;
  if (!tci_running) { return; }
  txvfo = vfo_get_tx_vfo();
  if (txvfo < VFO_A || txvfo > VFO_B) { return; }
  tci_broadcast_xit_enable();
  if (vfo[txvfo].xit_enabled) {
    if (vfo[txvfo].xit != 0) {
      tci_broadcast_xit_offset();
    }
  } else {
    tci_broadcast_xit_offset();
  }
}

static void tci_send_rit_offset_value (CLIENT *client, int receiver_id, long long value) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rit_offset:%d,%lld;", receiver_id, value);
  tci_send_text (client, msg);
}

static void tci_send_rit_offset (CLIENT *client, int receiver_id) {
  long long value;
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  value = vfo[receiver_id].rit_enabled ? vfo[receiver_id].rit : 0;
  tci_send_rit_offset_value (client, receiver_id, value);
}

static void tci_broadcast_rit_offset (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rit_offset (client, receiver_id);
    }
  }
  g_list_free (clients);
}

void tci_rit_offset_changed (int receiver_id) {
  if (!tci_running) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  if (!vfo[receiver_id].rit_enabled) { return; }
  tci_broadcast_rit_offset (receiver_id);
}

static void tci_send_xit_offset_value (CLIENT *client, long long value) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "xit_offset:0,%lld;", value);
  tci_send_text (client, msg);
}

static void tci_send_xit_offset (CLIENT *client) {
  int txvfo;
  long long value;
  if (client == NULL) { return; }
  txvfo = vfo_get_tx_vfo();
  if (txvfo < VFO_A || txvfo > VFO_B) { return; }
  value = vfo[txvfo].xit_enabled ? vfo[txvfo].xit : 0;
  tci_send_xit_offset_value (client, value);
}

static void tci_broadcast_xit_offset (void) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_xit_offset (client);
    }
  }
  g_list_free (clients);
}

void tci_xit_offset_changed (void) {
  int txvfo;
  if (!tci_running) { return; }
  txvfo = vfo_get_tx_vfo();
  if (txvfo < VFO_A || txvfo > VFO_B) { return; }
  if (!vfo[txvfo].xit_enabled) { return; }
  tci_broadcast_xit_offset();
}

static int tci_get_active_digu_offset(void) {
  if (active_receiver != NULL) {
    return active_receiver->digi_offset_u;
  }
  if (receivers > 0 && receiver[0] != NULL) {
    return receiver[0]->digi_offset_u;
  }
  return 0;
}

static int tci_get_active_digl_offset(void) {
  if (active_receiver != NULL) {
    return active_receiver->digi_offset_l;
  }
  if (receivers > 0 && receiver[0] != NULL) {
    return receiver[0]->digi_offset_l;
  }
  return 0;
}

static void tci_send_digu_offset_value(CLIENT *client, int value) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  snprintf(msg, MAXMSGSIZE, "digu_offset:%d;", value);
  tci_send_text(client, msg);
}

static void tci_send_digl_offset_value(CLIENT *client, int value) {
  char msg[MAXMSGSIZE];
  if (client == NULL) { return; }
  snprintf(msg, MAXMSGSIZE, "digl_offset:%d;", value);
  tci_send_text(client, msg);
}

static void tci_send_digu_offset(CLIENT *client) {
  tci_send_digu_offset_value(client, tci_get_active_digu_offset());
}

static void tci_send_digl_offset(CLIENT *client) {
  tci_send_digl_offset_value(client, tci_get_active_digl_offset());
}

static int tci_digi_offset_claim(CLIENT *client) {
  int allowed;
  if (client == NULL) { return 0; }
  g_mutex_lock(&tci_mutex);
  if (tci_digi_offset_owner == NULL) {
    tci_digi_offset_owner = client;
  }
  allowed = (tci_digi_offset_owner == client);
  g_mutex_unlock(&tci_mutex);
  return allowed;
}

static int tci_set_lock_allowed(CLIENT *client, TCI_SET_LOCK_ID lock_id) {
  gint64 now;
  int allowed = 0;
  if (client == NULL || lock_id < 0 || lock_id >= TCI_SET_LOCK_COUNT) {
    return 0;
  }
  now = g_get_monotonic_time();
  g_mutex_lock(&tci_mutex);
  if (tci_set_locks[lock_id].owner == NULL || now >= tci_set_locks[lock_id].until_us) {
    tci_set_locks[lock_id].owner = client;
    tci_set_locks[lock_id].until_us = now + 200000;
    allowed = 1;
  } else if (tci_set_locks[lock_id].owner == client) {
    tci_set_locks[lock_id].until_us = now + 200000;
    allowed = 1;
  }
  g_mutex_unlock(&tci_mutex);
  if (!allowed) {
    t_print("TCI%d set command locked for 200 ms by another client\n", client->seq);
  }
  return allowed;
}

static void tci_tx_client_cleanup_tx_audio(CLIENT *client) {
  if (client == NULL) {
    return;
  }
  g_mutex_lock(&tci_mutex);
  client->tx_audio_enabled = 0;
  client->tx_chrono_next_us = 0;
  client->tx_chrono_tick = 0;
  client->tx_chrono_queue_count = 0;
  client->tx_audio_rx_count = 0;
  g_mutex_unlock(&tci_mutex);
  tci_audio_tx_set_active(0);
  tci_audio_tx_reset();
  tci_audio_destroy_tx_resampler(&client->tx_audio_resampler_24_to_48);
#ifdef PORTAUDIO
  if (tci_audio_monitor && !tci_has_audio_monitor_source()) {
    audio_close_tci_monitor();
  }
#endif
  tci_lws_binary_reset(client);
}

static void tci_set_locks_clear_client(CLIENT *client) {
  if (client == NULL) {
    return;
  }
  for (int i = 0; i < TCI_SET_LOCK_COUNT; i++) {
    if (tci_set_locks[i].owner == client) {
      tci_set_locks[i].owner = NULL;
      tci_set_locks[i].until_us = 0;
    }
  }
}

static void tci_set_locks_clear_all(void) {
  for (int i = 0; i < TCI_SET_LOCK_COUNT; i++) {
    tci_set_locks[i].owner = NULL;
    tci_set_locks[i].until_us = 0;
  }
}

static void tci_tx_owner_sync_local_state(void) {
  CLIENT *owner = NULL;
  TCI_TX_OWNER_MODE owner_mode = TCI_TX_OWNER_NONE;
  int clear_owner = 0;
  g_mutex_lock(&tci_mutex);
  if (tci_tx_owner != NULL) {
    if (tci_tx_owner_mode == TCI_TX_OWNER_MOX && !radio_is_transmitting()) {
      clear_owner = 1;
    } else if (tci_tx_owner_mode == TCI_TX_OWNER_TUNE && !tune) {
      clear_owner = 1;
    }
    if (clear_owner) {
      owner = tci_tx_owner;
      owner_mode = tci_tx_owner_mode;
      tci_tx_owner = NULL;
      tci_tx_owner_mode = TCI_TX_OWNER_NONE;
    }
  }
  g_mutex_unlock(&tci_mutex);
  if (clear_owner && owner != NULL) {
    if (owner_mode == TCI_TX_OWNER_MOX) {
      tci_tx_client_cleanup_tx_audio(owner);
    }
    t_print("TCI%d %s owner released by local control\n",
            owner->seq,
            owner_mode == TCI_TX_OWNER_TUNE ? "TUNE" : "TX");
  }
}

static void tci_broadcast_digu_offset(void) {
  GList *clients = tci_clients_snapshot();
  int value = tci_get_active_digu_offset();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_digu_offset_value(client, value);
    }
  }
  g_list_free(clients);
}

static void tci_broadcast_digl_offset(void) {
  GList *clients = tci_clients_snapshot();
  int value = tci_get_active_digl_offset();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_digl_offset_value(client, value);
    }
  }
  g_list_free(clients);
}

void tci_digu_offset_changed(void) {
  if (!tci_running) { return; }
  tci_broadcast_digu_offset();
}

void tci_digl_offset_changed(void) {
  if (!tci_running) { return; }
  tci_broadcast_digl_offset();
}

static void tci_send_tune (CLIENT *client) {
  if (tune) {
    tci_send_text (client, "tune:0,true;");
  } else {
    tci_send_text (client, "tune:0,false;");
  }
}

static void tci_send_mute (CLIENT *client) {
  char msg[MAXMSGSIZE];
  int state = 0;
  if (active_receiver != NULL) {
    state = active_receiver->mute_radio ? 1 : 0;
  }
  snprintf (msg, MAXMSGSIZE, "mute:%s;", state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_mute_state (CLIENT *client, int state) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "mute:%s;", state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_mute_state (int state) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_mute_state (client, state);
    }
  }
  g_list_free (clients);
}

static void tci_send_rx_mute (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_mute:%d,%s;", receiver_id, receiver[receiver_id]->mute_radio ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_rx_mute_state (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_mute:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_mute_state (int receiver_id, int state) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_mute_state (client, receiver_id, state);
    }
  }
  g_list_free (clients);
}


void tci_mute_changed (int receiver_id) {
  if (!tci_running) { return; }
  if (active_receiver != NULL) {
    tci_broadcast_mute_state(active_receiver->mute_radio ? 1 : 0);
  }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  tci_broadcast_rx_mute_state(receiver_id, receiver[receiver_id]->mute_radio ? 1 : 0);
}

void tci_rx_mute_changed (int receiver_id) {
  if (!tci_running) { return; }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  tci_broadcast_rx_mute_state(receiver_id, receiver[receiver_id]->mute_radio ? 1 : 0);
}

static double tci_sql_db_from_slider(double value) {
  if (value < 0.0) { value = 0.0; }
  if (value > 100.0) { value = 100.0; }
  return ((value / 100.0) * 140.0) - 140.0;
}

static double tci_sql_slider_from_db(double value) {
  if (value < -140.0) { value = -140.0; }
  if (value > 0.0) { value = 0.0; }
  return ((value + 140.0) / 140.0) * 100.0;
}

static double tci_clamp_sql_level(double value) {
  if (value < -140.0) { return -140.0; }
  if (value > 0.0) { return 0.0; }
  return value;
}

static void tci_send_sql_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "sql_enable:%d,%s;", receiver_id,
            receiver[receiver_id]->squelch_enable ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_sql_enable_value (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "sql_enable:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_sql_level (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  double value;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  value = tci_sql_db_from_slider(receiver[receiver_id]->squelch);
  snprintf (msg, MAXMSGSIZE, "sql_level:%d,%0.0f;", receiver_id, value);
  tci_send_text (client, msg);
}

static void tci_send_sql_level_value (CLIENT *client, int receiver_id, double value) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  value = tci_clamp_sql_level(value);
  snprintf (msg, MAXMSGSIZE, "sql_level:%d,%0.0f;", receiver_id, value);
  tci_send_text (client, msg);
}

static void tci_broadcast_sql_enable (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_sql_enable (client, receiver_id);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_sql_enable_value (int receiver_id, int state) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_sql_enable_value (client, receiver_id, state);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_sql_level (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_sql_level (client, receiver_id);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_sql_level_value (int receiver_id, double value) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  value = tci_clamp_sql_level(value);
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_sql_level_value (client, receiver_id, value);
    }
  }
  g_list_free (clients);
}

void tci_sql_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_sql_enable(receiver_id);
}

void tci_sql_level_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_sql_level(receiver_id);
}

static void tci_send_rx_anf_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  int state;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  state = rx_anf_allowed(receiver[receiver_id]) ? receiver[receiver_id]->anf : 0;
  snprintf (msg, MAXMSGSIZE, "rx_anf_enable:%d,%s;", receiver_id,
            state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_rx_anf_enable_value (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_anf_enable:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_anf_enable (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_anf_enable (client, receiver_id);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_rx_anf_enable_value (int receiver_id, int state) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_anf_enable_value (client, receiver_id, state);
    }
  }
  g_list_free (clients);
}

void tci_rx_anf_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_anf_enable(receiver_id);
}

static void tci_send_rx_nf_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_nf_enable:%d,%s;", receiver_id,
            receiver[receiver_id]->mnf ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_rx_nf_enable_value (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_nf_enable:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_nf_enable (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_nf_enable (client, receiver_id);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_rx_nf_enable_value (int receiver_id, int state) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_nf_enable_value (client, receiver_id, state);
    }
  }
  g_list_free (clients);
}

void tci_rx_nf_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_nf_enable(receiver_id);
}

static int tci_rx_nb_allowed (int receiver_id) {
  int mode;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return 0;
  }
  mode = vfo[receiver_id].mode;
  return mode != modeDIGL && mode != modeDIGU;
}

static int tci_rx_nb_effective_state (int receiver_id) {
  if (!tci_rx_nb_allowed(receiver_id)) {
    return 0;
  }
  return receiver[receiver_id]->snb ? 1 : 0;
}

static void tci_send_rx_nb_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_nb_enable:%d,%s;", receiver_id,
            tci_rx_nb_effective_state(receiver_id) ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_rx_nb_enable_value (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_nb_enable:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_nb_enable (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_nb_enable (client, receiver_id);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_rx_nb_enable_value (int receiver_id, int state) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_nb_enable_value (client, receiver_id, state);
    }
  }
  g_list_free (clients);
}

void tci_rx_nb_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_nb_enable(receiver_id);
}

static void tci_send_rx_bin_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  int state;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  state = rx_binaural_allowed(receiver[receiver_id]) ? receiver[receiver_id]->binaural : 0;
  snprintf (msg, MAXMSGSIZE, "rx_bin_enable:%d,%s;", receiver_id,
            state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_rx_bin_enable_value (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_bin_enable:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_bin_enable (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_bin_enable (client, receiver_id);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_rx_bin_enable_value (int receiver_id, int state) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_bin_enable_value (client, receiver_id, state);
    }
  }
  g_list_free (clients);
}

void tci_rx_bin_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_bin_enable(receiver_id);
}

static int tci_rx_apf_allowed (int receiver_id) {
  int mode;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return 0;
  }
  mode = vfo[receiver_id].mode;
  return mode == modeCWU || mode == modeCWL;
}

static int tci_rx_apf_effective_state (int receiver_id) {
  if (!tci_rx_apf_allowed(receiver_id)) {
    return 0;
  }
  return (vfo[receiver_id].cwAudioPeakFilter && receiver[receiver_id]->use_cw_dp_filter) ? 1 : 0;
}

static void tci_send_rx_apf_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_apf_enable:%d,%s;", receiver_id,
            tci_rx_apf_effective_state(receiver_id) ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_rx_apf_enable_value (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_apf_enable:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_apf_enable (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_apf_enable (client, receiver_id);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_rx_apf_enable_value (int receiver_id, int state) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_apf_enable_value (client, receiver_id, state);
    }
  }
  g_list_free (clients);
}

void tci_rx_apf_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_apf_enable(receiver_id);
}

static int tci_rx_nr_default_for_mode (int mode) {
  switch (mode) {
  case modeUSB:
  case modeLSB:
  case modeCWU:
  case modeCWL:
    return 4;
  case modeAM:
  case modeSAM:
    return 3;
  default:
    return 0;
  }
}

static int tci_rx_nr_allowed (int receiver_id) {
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return 0;
  }
  return tci_rx_nr_default_for_mode(vfo[receiver_id].mode) != 0;
}

static int tci_rx_nr_effective_state (int receiver_id) {
  if (!tci_rx_nr_allowed(receiver_id)) {
    return 0;
  }
  return receiver[receiver_id]->nr != 0;
}

static void tci_send_rx_nr_enable (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_nr_enable:%d,%s;", receiver_id,
            tci_rx_nr_effective_state(receiver_id) ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_send_rx_nr_enable_value (CLIENT *client, int receiver_id, int state) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf (msg, MAXMSGSIZE, "rx_nr_enable:%d,%s;", receiver_id, state ? "true" : "false");
  tci_send_text (client, msg);
}

static void tci_broadcast_rx_nr_enable (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_nr_enable (client, receiver_id);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_rx_nr_enable_value (int receiver_id, int state) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_nr_enable_value (client, receiver_id, state);
    }
  }
  g_list_free (clients);
}

void tci_rx_nr_enable_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_rx_nr_enable(receiver_id);
}

static double tci_clamp_volume(double value) {
  if (value < -40.0) { return -40.0; }
  if (value > 0.0) { return 0.0; }
  return value;
}

static void tci_send_volume (CLIENT *client) {
  char msg[MAXMSGSIZE];
  double value = 0.0;
  if (active_receiver != NULL) {
    value = tci_clamp_volume(active_receiver->volume);
  }
  snprintf (msg, MAXMSGSIZE, "volume:%0.0f;", value);
  tci_send_text (client, msg);
}

static void tci_send_volume_value (CLIENT *client, double value) {
  char msg[MAXMSGSIZE];
  value = tci_clamp_volume(value);
  snprintf (msg, MAXMSGSIZE, "volume:%0.0f;", value);
  tci_send_text (client, msg);
}

static void tci_send_rx_volume (CLIENT *client, int receiver_id, int channel) {
  char msg[MAXMSGSIZE];
  double value;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  if (channel < 0 || channel > 1) { return; }
  value = tci_clamp_volume(receiver[receiver_id]->volume);
  snprintf (msg, MAXMSGSIZE, "rx_volume:%d,%d,%0.0f;", receiver_id, channel, value);
  tci_send_text (client, msg);
}

static void tci_send_rx_volume_value (CLIENT *client, int receiver_id, int channel, double value) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  if (channel < 0 || channel > 1) { return; }
  value = tci_clamp_volume(value);
  snprintf (msg, MAXMSGSIZE, "rx_volume:%d,%d,%0.0f;", receiver_id, channel, value);
  tci_send_text (client, msg);
}

static void tci_broadcast_volume (void) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_volume (client);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_rx_volume (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_rx_volume (client, receiver_id, 0);
      tci_send_rx_volume (client, receiver_id, 1);
    }
  }
  g_list_free (clients);
}

void tci_volume_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_volume();
  tci_broadcast_rx_volume(receiver_id);
}


static double tci_clamp_agc_gain(double value) {
  if (value < -20.0) { return -20.0; }
  if (value > 120.0) { return 120.0; }
  return value;
}

static void tci_send_agc_gain (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  double value;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  value = tci_clamp_agc_gain(receiver[receiver_id]->agc_gain);
  snprintf (msg, MAXMSGSIZE, "agc_gain:%d,%0.0f;", receiver_id, value);
  tci_send_text (client, msg);
}

static void tci_send_agc_gain_value (CLIENT *client, int receiver_id, double value) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  value = tci_clamp_agc_gain(value);
  snprintf (msg, MAXMSGSIZE, "agc_gain:%d,%0.0f;", receiver_id, value);
  tci_send_text (client, msg);
}

static void tci_broadcast_agc_gain (int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_agc_gain (client, receiver_id);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_agc_gain_value (int receiver_id, double value) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  value = tci_clamp_agc_gain(value);
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_agc_gain_value(client, receiver_id, value);
    }
  }
  g_list_free(clients);
}

void tci_agc_gain_changed (int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_agc_gain(receiver_id);
}

static const char *tci_agc_mode_name(int agc) {
  switch (agc) {
  case AGC_OFF:
    return "off";
  case AGC_FAST:
    return "fast";
  default:
    return "normal";
  }
}

static int tci_parse_agc_mode(const char *mode) {
  if (mode == NULL) {
    return AGC_MEDIUM;
  }
  if (!g_ascii_strcasecmp(mode, "off")) {
    return AGC_OFF;
  }
  if (!g_ascii_strcasecmp(mode, "fast")) {
    return AGC_FAST;
  }
  if (!g_ascii_strcasecmp(mode, "normal")) {
    return AGC_MEDIUM;
  }
  return -1;
}

static void tci_send_agc_mode(CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf(msg, MAXMSGSIZE, "agc_mode:%d,%s;", receiver_id, tci_agc_mode_name(receiver[receiver_id]->agc));
  tci_send_text(client, msg);
}

static void tci_send_agc_mode_value(CLIENT *client, int receiver_id, int agc) {
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  snprintf(msg, MAXMSGSIZE, "agc_mode:%d,%s;", receiver_id, tci_agc_mode_name(agc));
  tci_send_text(client, msg);
}

static void tci_broadcast_agc_mode(int receiver_id) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_agc_mode(client, receiver_id);
    }
  }
  g_list_free(clients);
}

static void tci_broadcast_agc_mode_value(int receiver_id, int agc) {
  GList *clients;
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) { return; }
  clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_agc_mode_value(client, receiver_id, agc);
    }
  }
  g_list_free(clients);
}

void tci_agc_mode_changed(int receiver_id) {
  if (!tci_running) { return; }
  tci_broadcast_agc_mode(receiver_id);
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

static void tci_broadcast_drive (void) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_drive (client);
    }
  }
  g_list_free (clients);
}

static void tci_broadcast_tune_drive (void) {
  GList *clients = tci_clients_snapshot();
  for (GList *l = clients; l != NULL; l = l->next) {
    CLIENT *client = (CLIENT*) l->data;
    if (client != NULL && client->running) {
      tci_send_tune_drive (client);
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
  if (v >= receivers || receiver[v] == NULL) { return; }
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
      tci_send_rx_filter_band (client, v);
      if (m == modeDIGU) {
        tci_send_digu_offset (client);
      } else if (m == modeDIGL) {
        tci_send_digl_offset (client);
      }
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
    if (receivers > 1) {
      tci_broadcast_vfo (VFO_B, 0);
      tci_broadcast_vfo (VFO_B, 1);
    }
  }
}

void tci_vfos_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_vfo (VFO_A, 0);
  tci_broadcast_vfo (VFO_A, 1);
  tci_broadcast_mode_value (VFO_A, vfo[VFO_A].mode);
  if (receivers > 1) {
    tci_broadcast_vfo (VFO_B, 0);
    tci_broadcast_vfo (VFO_B, 1);
    tci_broadcast_mode_value (VFO_B, vfo[VFO_B].mode);
  }
  tci_broadcast_txfreq();
  tci_broadcast_drive();
  tci_broadcast_tune_drive();
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

void tci_drive_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_drive();
  if (transmitter != NULL && transmitter->tune_use_drive) {
    tci_broadcast_tune_drive();
  }
}

void tci_tune_drive_changed (void) {
  if (!tci_running) { return; }
  tci_broadcast_tune_drive();
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
  tci_broadcast_mode_value (mc->vfo_id, mc->mode);
  g_free (mc);
  return G_SOURCE_REMOVE;
}

static void tci_set_mode (CLIENT *client, int VfoNr, const char* mode_str) {
  if (VfoNr < 0 || VfoNr > 1) { return; }
  if (VfoNr >= receivers || receiver[VfoNr] == NULL) { return; }
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
}

static void tci_send_trx_count (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "trx_count:1;");
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

static void tci_send_cw_macros_delay(CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf(msg, MAXMSGSIZE, "cw_macros_delay:%d;", tci_cw_macros_delay_ms);
  tci_send_text(client, msg);
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

static double tci_double (const char* s, double def) {
  return s != NULL ? atof (s) : def;
}

static int tci_clamp_int(int value, int min, int max) {
  if (value < min) { return min; }
  if (value > max) { return max; }
  return value;
}

static int tci_apply_sql_enable_update (void *data) {
  TCI_SQL_ENABLE_UPDATE *su = (TCI_SQL_ENABLE_UPDATE*) data;
  int receiver_id;
  int state;
  if (su == NULL) { return G_SOURCE_REMOVE; }
  receiver_id = su->receiver_id;
  state = su->state ? 1 : 0;
  if (receiver_id >= 0 && receiver_id < receivers && receiver_id < 2 && receiver[receiver_id] != NULL) {
    receiver[receiver_id]->squelch_enable = state;
    rx_set_squelch(receiver[receiver_id]);
    update_slider_squelch(receiver[receiver_id]);
    tci_broadcast_sql_enable_value(receiver_id, state);
  }
  g_free(su);
  return G_SOURCE_REMOVE;
}

static int tci_apply_sql_level_update (void *data) {
  TCI_SQL_LEVEL_UPDATE *su = (TCI_SQL_LEVEL_UPDATE*) data;
  int receiver_id;
  double level_db;
  if (su == NULL) { return G_SOURCE_REMOVE; }
  receiver_id = su->receiver_id;
  level_db = tci_clamp_sql_level(su->level_db);
  if (receiver_id >= 0 && receiver_id < receivers && receiver_id < 2 && receiver[receiver_id] != NULL) {
    receiver[receiver_id]->squelch = tci_sql_slider_from_db(level_db);
    rx_set_squelch(receiver[receiver_id]);
    update_slider_squelch(receiver[receiver_id]);
    tci_broadcast_sql_level_value(receiver_id, level_db);
  }
  g_free(su);
  return G_SOURCE_REMOVE;
}

static int tci_apply_rx_apf_enable_update (void *data) {
  TCI_RX_APF_ENABLE_UPDATE *au = (TCI_RX_APF_ENABLE_UPDATE*) data;
  int receiver_id;
  int state;
  int mode;
  if (au == NULL) { return G_SOURCE_REMOVE; }
  receiver_id = au->receiver_id;
  state = au->state ? 1 : 0;
  if (receiver_id >= 0 && receiver_id < receivers && receiver_id < 2 && receiver[receiver_id] != NULL) {
    if (state && !tci_rx_apf_allowed(receiver_id)) {
      state = 0;
    }
    mode = vfo[receiver_id].mode;
    if (vfo[receiver_id].cwAudioPeakFilter != state || (state && !receiver[receiver_id]->use_cw_dp_filter)) {
      tci_begin_apply();
      vfo[receiver_id].cwAudioPeakFilter = state;
      if (receiver_id == 0) {
        mode_settings[mode].cwPeak = state;
        copy_mode_settings(mode);
      }
      if (state) {
        for (int i = 0; i < RECEIVERS; i++) {
          if (receiver[i] != NULL) {
            receiver[i]->use_cw_dp_filter = 1;
          }
        }
      }
      rx_filter_changed(receiver[receiver_id]);
      g_idle_add(ext_vfo_update, NULL);
      tci_end_apply();
    }
    tci_broadcast_rx_apf_enable_value(receiver_id, state);
  }
  g_free(au);
  return G_SOURCE_REMOVE;
}

static int tci_apply_rx_nb_enable_update (void *data) {
  TCI_RX_NB_ENABLE_UPDATE *nu = (TCI_RX_NB_ENABLE_UPDATE*) data;
  int receiver_id;
  int state;
  if (nu == NULL) { return G_SOURCE_REMOVE; }
  receiver_id = nu->receiver_id;
  state = nu->state ? 1 : 0;
  if (receiver_id >= 0 && receiver_id < receivers && receiver_id < 2 && receiver[receiver_id] != NULL) {
    if (state && !tci_rx_nb_allowed(receiver_id)) {
      state = 0;
    }
    if (receiver[receiver_id]->snb != state) {
      tci_begin_apply();
      receiver[receiver_id]->snb = state;
      if (receiver_id == 0) {
        int mode = vfo[receiver_id].mode;
        mode_settings[mode].snb = state;
        copy_mode_settings(mode);
      }
      if (receiver[receiver_id] == active_receiver) {
        update_noise();
      } else {
        rx_set_noise(receiver[receiver_id]);
        g_idle_add(ext_vfo_update, NULL);
      }
      tci_end_apply();
    }
    tci_broadcast_rx_nb_enable_value(receiver_id, state);
  }
  g_free(nu);
  return G_SOURCE_REMOVE;
}

static int tci_apply_rx_anf_enable_update (void *data) {
  TCI_RX_ANF_ENABLE_UPDATE *au = (TCI_RX_ANF_ENABLE_UPDATE*) data;
  int receiver_id;
  int state;
  if (au == NULL) { return G_SOURCE_REMOVE; }
  receiver_id = au->receiver_id;
  state = au->state ? 1 : 0;
  if (receiver_id >= 0 && receiver_id < receivers && receiver_id < 2 && receiver[receiver_id] != NULL) {
    if (state && !rx_anf_allowed(receiver[receiver_id])) {
      state = 0;
    }
    if (receiver[receiver_id]->anf != state) {
      tci_begin_apply();
      receiver[receiver_id]->anf = state;
      if (receiver[receiver_id] == active_receiver) {
        update_anf();
      } else {
        rx_set_anf(receiver[receiver_id]);
        g_idle_add(ext_vfo_update, NULL);
      }
      tci_end_apply();
      tci_broadcast_rx_anf_enable_value(receiver_id, state);
    }
  }
  g_free(au);
  return G_SOURCE_REMOVE;
}

static int tci_apply_rx_nf_enable_update (void *data) {
  TCI_RX_NF_ENABLE_UPDATE *nu = (TCI_RX_NF_ENABLE_UPDATE*) data;
  int receiver_id;
  int state;
  if (nu == NULL) { return G_SOURCE_REMOVE; }
  receiver_id = nu->receiver_id;
  state = nu->state ? 1 : 0;
  if (receiver_id >= 0 && receiver_id < receivers && receiver_id < 2 && receiver[receiver_id] != NULL) {
    if (receiver[receiver_id]->mnf != state) {
      tci_begin_apply();
      receiver[receiver_id]->mnf = state;
      if (receiver[receiver_id] == active_receiver) {
        update_notch();
      } else {
        rx_set_notch(receiver[receiver_id]);
        g_idle_add(ext_vfo_update, NULL);
      }
      tci_end_apply();
      tci_broadcast_rx_nf_enable_value(receiver_id, state);
    }
  }
  g_free(nu);
  return G_SOURCE_REMOVE;
}

static int tci_apply_rx_nr_enable_update (void *data) {
  TCI_RX_NR_ENABLE_UPDATE *nu = (TCI_RX_NR_ENABLE_UPDATE*) data;
  int receiver_id;
  int state;
  int new_nr;
  if (nu == NULL) { return G_SOURCE_REMOVE; }
  receiver_id = nu->receiver_id;
  state = nu->state ? 1 : 0;
  if (receiver_id >= 0 && receiver_id < receivers && receiver_id < 2 && receiver[receiver_id] != NULL) {
    new_nr = state ? tci_rx_nr_default_for_mode(vfo[receiver_id].mode) : 0;
    if (new_nr == 0) {
      state = 0;
    }
    if (receiver[receiver_id]->nr != new_nr) {
      tci_begin_apply();
      receiver[receiver_id]->nr = new_nr;
      if (receiver[receiver_id] == active_receiver) {
        update_noise();
      } else {
        rx_set_noise(receiver[receiver_id]);
        g_idle_add(ext_vfo_update, NULL);
      }
      tci_end_apply();
    }
    tci_broadcast_rx_nr_enable_value(receiver_id, state);
  }
  g_free(nu);
  return G_SOURCE_REMOVE;
}

static int tci_apply_mute_update (void *data) {
  int state = GPOINTER_TO_INT (data) ? 1 : 0;
  if (active_receiver != NULL) {
    active_receiver->mute_radio = state;
    g_idle_add (ext_vfo_update, NULL);
    update_slider_af_gain_btn();
  }
  return G_SOURCE_REMOVE;
}

static int tci_apply_rx_bin_enable_update (void *data) {
  TCI_RX_BIN_ENABLE_UPDATE *bu = (TCI_RX_BIN_ENABLE_UPDATE*) data;
  int receiver_id;
  int state;
  if (bu == NULL) { return G_SOURCE_REMOVE; }
  receiver_id = bu->receiver_id;
  state = bu->state ? 1 : 0;
  if (receiver_id >= 0 && receiver_id < receivers && receiver_id < 2 && receiver[receiver_id] != NULL) {
    if (state && !rx_binaural_allowed(receiver[receiver_id])) {
      state = 0;
    }
    if (receiver[receiver_id]->binaural != state) {
      tci_begin_apply();
      receiver[receiver_id]->binaural = state;
      rx_set_af_binaural(receiver[receiver_id]);
      if (receiver[receiver_id] == active_receiver) {
        update_slider_binaural_btn();
      }
      tci_end_apply();
      tci_broadcast_rx_bin_enable_value(receiver_id, state);
    }
  }
  g_free(bu);
  return G_SOURCE_REMOVE;
}

static int tci_apply_rx_mute_update (void *data) {
  TCI_RX_MUTE_UPDATE *mu = (TCI_RX_MUTE_UPDATE*) data;
  int receiver_id;
  int state;
  if (mu == NULL) {
    return G_SOURCE_REMOVE;
  }
  receiver_id = mu->receiver_id;
  state = mu->state ? 1 : 0;
  if (receiver_id >= 0 && receiver_id < receivers && receiver[receiver_id] != NULL) {
    receiver[receiver_id]->mute_radio = state;
    g_idle_add (ext_vfo_update, NULL);
    update_slider_af_gain_btn();
  }
  g_free (mu);
  return G_SOURCE_REMOVE;
}

static long long tci_ll (const char* s, long long def) {
  return s != NULL ? atoll (s) : def;
}


static int tci_apply_split_update (void *data) {
  int state = GPOINTER_TO_INT (data) ? 1 : 0;
  tci_begin_apply();
  radio_set_split (state);
  if (state) {
    vfo_a_to_b();
  }
  update_slider_split_btn();
  tci_end_apply();
  tci_broadcast_split();
  tci_broadcast_txfreq();
  return G_SOURCE_REMOVE;
}

static int tci_apply_rit_enable_update (void *data) {
  TCI_RIT_ENABLE_UPDATE *ru = (TCI_RIT_ENABLE_UPDATE*) data;
  int receiver_id;
  int state;
  if (ru == NULL) { return G_SOURCE_REMOVE; }
  receiver_id = ru->receiver_id;
  state = ru->state ? 1 : 0;
  if (receiver_id >= 0 && receiver_id < receivers && receiver[receiver_id] != NULL) {
    tci_begin_apply();
    vfo_rit_onoff (receiver_id, state);
    tci_end_apply();
    tci_rit_enable_changed (receiver_id);
  }
  g_free (ru);
  return G_SOURCE_REMOVE;
}

static int tci_apply_xit_enable_update (void *data) {
  int state = GPOINTER_TO_INT (data) ? 1 : 0;
  tci_begin_apply();
  vfo_xit_onoff (state);
  tci_end_apply();
  tci_xit_enable_changed();
  return G_SOURCE_REMOVE;
}

static void tci_cmd_rit_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id;
  if (cmd->argc < 1) { return; }
  receiver_id = tci_int (cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_RIT)) {
      tci_send_rit_enable(client, receiver_id);
      return;
    }
    TCI_RIT_ENABLE_UPDATE *ru = g_new (TCI_RIT_ENABLE_UPDATE, 1);
    ru->receiver_id = receiver_id;
    ru->state = tci_bool (cmd->argv[1]) ? 1 : 0;
    g_idle_add (tci_apply_rit_enable_update, ru);
  } else {
    tci_send_rit_enable (client, receiver_id);
  }
}

static void tci_cmd_xit_enable (CLIENT *client, const TCI_CMD *cmd) {
  int trx;
  if (cmd->argc < 1) { return; }
  trx = tci_int (cmd->argv[0], -1);
  if (trx != 0) { return; }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_XIT)) {
      tci_send_xit_enable(client);
      return;
    }
    int state = tci_bool (cmd->argv[1]) ? 1 : 0;
    g_idle_add (tci_apply_xit_enable_update, GINT_TO_POINTER (state));
  } else {
    tci_send_xit_enable (client);
  }
}

static int tci_apply_rit_offset_update (void *data) {
  TCI_RIT_OFFSET_UPDATE *ru = (TCI_RIT_OFFSET_UPDATE*) data;
  int receiver_id;
  long long value;
  if (ru == NULL) { return G_SOURCE_REMOVE; }
  receiver_id = ru->receiver_id;
  value = ru->value;
  if (receiver_id >= 0 && receiver_id < receivers && receiver[receiver_id] != NULL) {
    tci_begin_apply();
    vfo_rit_value (receiver_id, value);
    tci_end_apply();
    if (vfo[receiver_id].rit_enabled) {
      tci_broadcast_rit_offset (receiver_id);
    }
  }
  g_free (ru);
  return G_SOURCE_REMOVE;
}

static int tci_apply_xit_offset_update (void *data) {
  long long value = (long long) GPOINTER_TO_INT (data);
  int txvfo = vfo_get_tx_vfo();
  if (txvfo < VFO_A || txvfo > VFO_B) { return G_SOURCE_REMOVE; }
  tci_begin_apply();
  vfo_xit_value (value);
  tci_end_apply();
  if (vfo[txvfo].xit_enabled) {
    tci_broadcast_xit_offset();
  }
  return G_SOURCE_REMOVE;
}

static int tci_apply_digi_offset_update(void *data) {
  TCI_DIGI_OFFSET_UPDATE *du = (TCI_DIGI_OFFSET_UPDATE*) data;
  if (du == NULL) {
    return G_SOURCE_REMOVE;
  }
  tci_begin_apply();
  for (int i = 0; i < receivers; i++) {
    if (receiver[i] != NULL) {
      if (du->is_digu) {
        receiver[i]->digi_offset_u = du->value;
      } else {
        receiver[i]->digi_offset_l = du->value;
      }
      rx_frequency_changed(receiver[i]);
    }
  }
  tci_end_apply();
  g_idle_add(ext_vfo_update, NULL);
  if (du->is_digu) {
    tci_broadcast_digu_offset();
  } else {
    tci_broadcast_digl_offset();
  }
  g_free(du);
  return G_SOURCE_REMOVE;
}

static void tci_cmd_rit_offset (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id;
  if (cmd->argc < 1) { return; }
  receiver_id = tci_int (cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_RIT)) {
      tci_send_rit_offset(client, receiver_id);
      return;
    }
    TCI_RIT_OFFSET_UPDATE *ru = g_new (TCI_RIT_OFFSET_UPDATE, 1);
    ru->receiver_id = receiver_id;
    ru->value = tci_ll (cmd->argv[1], 0);
    tci_send_rit_offset_value (client, receiver_id, ru->value);
    g_idle_add (tci_apply_rit_offset_update, ru);
  } else {
    tci_send_rit_offset (client, receiver_id);
  }
}

static void tci_cmd_xit_offset (CLIENT *client, const TCI_CMD *cmd) {
  int trx;
  if (cmd->argc < 1) { return; }
  trx = tci_int (cmd->argv[0], -1);
  if (trx != 0) { return; }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_XIT)) {
      tci_send_xit_offset(client);
      return;
    }
    long long value = tci_ll (cmd->argv[1], 0);
    tci_send_xit_offset_value (client, value);
    g_idle_add (tci_apply_xit_offset_update, GINT_TO_POINTER ((int) value));
  } else {
    tci_send_xit_offset (client);
  }
}


static void tci_cmd_digl_offset (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1) {
    int value = tci_int(cmd->argv[0], -1);
    if (value < 0 || value > 4000) {
      return;
    }
    if (!tci_digi_offset_claim(client)) {
      tci_send_digl_offset(client);
      return;
    }
    TCI_DIGI_OFFSET_UPDATE *du = g_new(TCI_DIGI_OFFSET_UPDATE, 1);
    du->is_digu = 0;
    du->value = value;
    tci_send_digl_offset_value(client, value);
    g_idle_add(tci_apply_digi_offset_update, du);
  } else {
    tci_send_digl_offset(client);
  }
}

static void tci_cmd_digu_offset (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1) {
    int value = tci_int(cmd->argv[0], -1);
    if (value < 0 || value > 4000) {
      return;
    }
    if (!tci_digi_offset_claim(client)) {
      tci_send_digu_offset(client);
      return;
    }
    TCI_DIGI_OFFSET_UPDATE *du = g_new(TCI_DIGI_OFFSET_UPDATE, 1);
    du->is_digu = 1;
    du->value = value;
    tci_send_digu_offset_value(client, value);
    g_idle_add(tci_apply_digi_offset_update, du);
  } else {
    tci_send_digu_offset(client);
  }
}

static int tci_is_bool_arg (const char *s) {
  return s != NULL && (!g_ascii_strcasecmp (s, "true") ||
                       !g_ascii_strcasecmp (s, "false"));
}

static void tci_set_lock_state (CLIENT *client, int receiver_id, int state, int vfo_lock, int channel_id) {
  int changed;
  tci_begin_apply();
  changed = set_locked(state ? 1 : 0);
  tci_end_apply();
  if (changed) {
    tci_broadcast_lock();
  } else if (vfo_lock) {
    if (channel_id >= 0) {
      tci_send_vfo_lock (client, receiver_id, channel_id);
    } else {
      tci_send_vfo_locks (client, receiver_id);
    }
  } else {
    tci_send_lock (client, receiver_id);
  }
}

static void tci_cmd_lock_common (CLIENT *client, const TCI_CMD *cmd, int vfo_lock) {
  int receiver_id;
  int channel_id;
  if (cmd->argc < 1) {
    if (vfo_lock) {
      tci_send_vfo_locks (client, VFO_A);
    } else {
      tci_send_lock (client, VFO_A);
    }
    return;
  }
  receiver_id = tci_int (cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id > 1) { return; }
  if (receiver_id >= receivers) { return; }
  if (!vfo_lock) {
    if (cmd->argc >= 2) {
      if (!tci_set_lock_allowed(client, TCI_SET_LOCK_LOCK)) {
        tci_send_lock(client, receiver_id);
        return;
      }
      tci_set_lock_state (client, receiver_id, tci_bool (cmd->argv[1]), 0, -1);
    } else {
      tci_send_lock (client, receiver_id);
    }
    return;
  }
  if (cmd->argc >= 3) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_LOCK)) {
      tci_send_vfo_locks(client, receiver_id);
      return;
    }
    channel_id = tci_int (cmd->argv[1], -1);
    if (channel_id < 0 || channel_id > 1) { return; }
    tci_set_lock_state (client, receiver_id, tci_bool (cmd->argv[2]), 1, channel_id);
  } else if (cmd->argc == 2) {
    if (tci_is_bool_arg (cmd->argv[1])) {
      if (!tci_set_lock_allowed(client, TCI_SET_LOCK_LOCK)) {
        tci_send_vfo_locks(client, receiver_id);
        return;
      }
      tci_set_lock_state (client, receiver_id, tci_bool (cmd->argv[1]), 1, -1);
    } else {
      channel_id = tci_int (cmd->argv[1], -1);
      if (channel_id < 0 || channel_id > 1) { return; }
      tci_send_vfo_lock (client, receiver_id, channel_id);
    }
  } else {
    tci_send_vfo_locks (client, receiver_id);
  }
}

static void tci_cmd_lock (CLIENT *client, const TCI_CMD *cmd) {
  tci_cmd_lock_common (client, cmd, 0);
}

static void tci_cmd_vfo_lock (CLIENT *client, const TCI_CMD *cmd) {
  tci_cmd_lock_common (client, cmd, 1);
}

static void tci_cmd_split_enable (CLIENT *client, const TCI_CMD *cmd) {
  int trx;
  if (cmd->argc < 1) {
    tci_send_split (client);
    return;
  }
  trx = tci_int (cmd->argv[0], -1);
  if (trx != 0) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_SPLIT)) {
      tci_send_split(client);
      return;
    }
    int state = tci_bool (cmd->argv[1]) ? 1 : 0;
    g_idle_add (tci_apply_split_update, GINT_TO_POINTER (state));
  } else {
    tci_send_split (client);
  }
}

static void tci_cmd_trx_count (CLIENT *client, const TCI_CMD *cmd) {
  (void) cmd;
  tci_send_trx_count (client);
}

static void tci_cmd_trx (CLIENT *client, const TCI_CMD *cmd) {
  int trx = 0;
  int source_tci;
  int state;
  int allow;
  int owner_seq;
  int tx_active;
  int tune_active;
  if (cmd->argc >= 1) {
    trx = tci_int (cmd->argv[0], -1);
    if (trx != 0) {
      return;
    }
  }
  source_tci = (cmd->argc >= 3 && cmd->argv[2] != NULL && !g_ascii_strcasecmp (cmd->argv[2], "tci"));
  tci_tx_owner_sync_local_state();
  if (cmd->argc >= 2) {
    state = tci_bool (cmd->argv[1]) ? 1 : 0;
    allow = 0;
    owner_seq = -1;
    tx_active = radio_is_transmitting();
    tune_active = tune ? 1 : 0;
    g_mutex_lock (&tci_mutex);
    if (state) {
      if (tci_tx_owner == client && tci_tx_owner_mode == TCI_TX_OWNER_MOX) {
        allow = 1;
      } else if (tci_tx_owner == NULL && !tx_active && !tune_active) {
        tci_tx_owner = client;
        tci_tx_owner_mode = TCI_TX_OWNER_MOX;
        allow = 1;
      } else if (tci_tx_owner != NULL) {
        owner_seq = tci_tx_owner->seq;
      }
    } else {
      if (tci_tx_owner == client && tci_tx_owner_mode == TCI_TX_OWNER_MOX) {
        tci_tx_owner = NULL;
        tci_tx_owner_mode = TCI_TX_OWNER_NONE;
        allow = 1;
      } else if (tci_tx_owner == NULL && !tx_active) {
        allow = 1;
      } else if (tci_tx_owner != NULL) {
        owner_seq = tci_tx_owner->seq;
      }
    }
    g_mutex_unlock (&tci_mutex);
    if (!allow) {
      if (owner_seq >= 0) {
        t_print ("TCI%d %s request ignored, TX owned by TCI%d\n",
                 client->seq,
                 state ? "TX" : "RX",
                 owner_seq);
      } else {
        t_print ("TCI%d %s request ignored, TX controlled locally\n",
                 client->seq,
                 state ? "TX" : "RX");
      }
      tci_send_mox (client);
      return;
    }
    if (state) {
      if (source_tci) {
        tci_audio_tx_reset();
        tci_audio_tx_set_active (1);
        g_mutex_lock (&tci_mutex);
        client->tx_audio_enabled = 1;
        client->tx_chrono_next_us = 0;
        client->tx_chrono_tick = 0;
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
        tci_send_audio_samplerate (client);
        tci_send_text (client, "audio_stream_sample_type:float32;");
        tci_send_text (client, "audio_stream_channels:1;");
        tci_send_audio_stream_samples (client);
        tci_send_text (client, "tx_stream_audio_buffering:50;");
        tci_send_text (client, "audio_start:0;");
        t_print ("TCI%d TX request (tci audio)\n", client->seq);
      } else {
        t_print ("TCI%d TX request\n", client->seq);
      }
      g_idle_add (ext_mox_update, GINT_TO_POINTER (1));
    } else {
      tci_tx_client_cleanup_tx_audio(client);
      g_timeout_add (50, ext_mox_update, GINT_TO_POINTER (0));
      t_print ("TCI%d RX request\n", client->seq);
    }
  } else {
    tci_send_mox (client);
  }
}

static void tci_cmd_tune (CLIENT *client, const TCI_CMD *cmd) {
  int trx = 0;
  int state;
  int allow;
  int owner_seq;
  int tx_active;
  int tune_active;
  if (cmd->argc >= 1) {
    trx = tci_int (cmd->argv[0], -1);
    if (trx != 0) {
      return;
    }
  }
  tci_tx_owner_sync_local_state();
  if (cmd->argc >= 2) {
    state = tci_bool (cmd->argv[1]) ? 1 : 0;
    allow = 0;
    owner_seq = -1;
    tx_active = radio_is_transmitting();
    tune_active = tune ? 1 : 0;
    g_mutex_lock (&tci_mutex);
    if (state) {
      if (tci_tx_owner == client && tci_tx_owner_mode == TCI_TX_OWNER_TUNE) {
        allow = 1;
      } else if (tci_tx_owner == NULL && !tx_active && !tune_active) {
        tci_tx_owner = client;
        tci_tx_owner_mode = TCI_TX_OWNER_TUNE;
        allow = 1;
      } else if (tci_tx_owner != NULL) {
        owner_seq = tci_tx_owner->seq;
      }
    } else {
      if (tci_tx_owner == client && tci_tx_owner_mode == TCI_TX_OWNER_TUNE) {
        tci_tx_owner = NULL;
        tci_tx_owner_mode = TCI_TX_OWNER_NONE;
        allow = 1;
      } else if (tci_tx_owner == NULL && !tx_active && !tune_active) {
        allow = 1;
      } else if (tci_tx_owner != NULL) {
        owner_seq = tci_tx_owner->seq;
      }
    }
    g_mutex_unlock (&tci_mutex);
    if (!allow) {
      if (owner_seq >= 0) {
        t_print ("TCI%d TUNE request=%d ignored, TX owned by TCI%d\n",
                 client->seq,
                 state,
                 owner_seq);
      } else {
        t_print ("TCI%d TUNE request=%d ignored, TX controlled locally\n",
                 client->seq,
                 state);
      }
      tci_send_tune (client);
      tci_send_mox (client);
      return;
    }
    g_idle_add (ext_tune_update, GINT_TO_POINTER (state));
    t_print ("TCI%d TUNE request=%d\n", client->seq, state);
  } else {
    tci_send_tune (client);
  }
}

static void tci_cmd_mute (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_MUTE)) {
      tci_send_mute(client);
      return;
    }
    int state = tci_bool (cmd->argv[0]) ? 1 : 0;
    g_idle_add (tci_apply_mute_update, GINT_TO_POINTER (state));
    tci_broadcast_mute_state (state);
  } else {
    tci_send_mute (client);
  }
}

static void tci_cmd_rx_mute (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_RX_MUTE)) {
      tci_send_rx_mute(client, receiver_id);
      return;
    }
    int state = tci_bool (cmd->argv[1]) ? 1 : 0;
    TCI_RX_MUTE_UPDATE *mu = g_new (TCI_RX_MUTE_UPDATE, 1);
    mu->receiver_id = receiver_id;
    mu->state = state;
    g_idle_add (tci_apply_rx_mute_update, mu);
    tci_broadcast_rx_mute_state (receiver_id, state);
  } else {
    tci_send_rx_mute (client, receiver_id);
  }
}

static void tci_cmd_rx_apf_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_RX_DSP)) {
      tci_send_rx_apf_enable(client, receiver_id);
      return;
    }
    TCI_RX_APF_ENABLE_UPDATE *au = g_new(TCI_RX_APF_ENABLE_UPDATE, 1);
    au->receiver_id = receiver_id;
    au->state = tci_bool(cmd->argv[1]) ? 1 : 0;
    if (au->state && !tci_rx_apf_allowed(receiver_id)) {
      au->state = 0;
    }
    g_idle_add(tci_apply_rx_apf_enable_update, au);
  } else {
    tci_send_rx_apf_enable(client, receiver_id);
  }
}

static void tci_cmd_rx_nb_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_RX_DSP)) {
      tci_send_rx_nb_enable(client, receiver_id);
      return;
    }
    TCI_RX_NB_ENABLE_UPDATE *nu = g_new(TCI_RX_NB_ENABLE_UPDATE, 1);
    nu->receiver_id = receiver_id;
    nu->state = tci_bool(cmd->argv[1]) ? 1 : 0;
    if (nu->state && !tci_rx_nb_allowed(receiver_id)) {
      nu->state = 0;
    }
    g_idle_add(tci_apply_rx_nb_enable_update, nu);
  } else {
    tci_send_rx_nb_enable(client, receiver_id);
  }
}

static void tci_cmd_rx_anf_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_RX_DSP)) {
      tci_send_rx_anf_enable(client, receiver_id);
      return;
    }
    TCI_RX_ANF_ENABLE_UPDATE *au = g_new(TCI_RX_ANF_ENABLE_UPDATE, 1);
    au->receiver_id = receiver_id;
    au->state = tci_bool(cmd->argv[1]) ? 1 : 0;
    if (au->state && !rx_anf_allowed(receiver[receiver_id])) {
      au->state = 0;
    }
    tci_send_rx_anf_enable_value(client, receiver_id, au->state);
    g_idle_add(tci_apply_rx_anf_enable_update, au);
  } else {
    tci_send_rx_anf_enable(client, receiver_id);
  }
}

static void tci_cmd_rx_nf_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_RX_DSP)) {
      tci_send_rx_nf_enable(client, receiver_id);
      return;
    }
    TCI_RX_NF_ENABLE_UPDATE *nu = g_new(TCI_RX_NF_ENABLE_UPDATE, 1);
    nu->receiver_id = receiver_id;
    nu->state = tci_bool(cmd->argv[1]) ? 1 : 0;
    tci_send_rx_nf_enable_value(client, receiver_id, nu->state);
    g_idle_add(tci_apply_rx_nf_enable_update, nu);
  } else {
    tci_send_rx_nf_enable(client, receiver_id);
  }
}

static void tci_cmd_rx_bin_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_RX_DSP)) {
      tci_send_rx_bin_enable(client, receiver_id);
      return;
    }
    TCI_RX_BIN_ENABLE_UPDATE *bu = g_new(TCI_RX_BIN_ENABLE_UPDATE, 1);
    bu->receiver_id = receiver_id;
    bu->state = tci_bool(cmd->argv[1]) ? 1 : 0;
    if (bu->state && !rx_binaural_allowed(receiver[receiver_id])) {
      bu->state = 0;
    }
    tci_send_rx_bin_enable_value(client, receiver_id, bu->state);
    g_idle_add(tci_apply_rx_bin_enable_update, bu);
  } else {
    tci_send_rx_bin_enable(client, receiver_id);
  }
}

static void tci_cmd_rx_nr_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_RX_DSP)) {
      tci_send_rx_nr_enable(client, receiver_id);
      return;
    }
    TCI_RX_NR_ENABLE_UPDATE *nu = g_new(TCI_RX_NR_ENABLE_UPDATE, 1);
    nu->receiver_id = receiver_id;
    nu->state = tci_bool(cmd->argv[1]) ? 1 : 0;
    if (nu->state && !tci_rx_nr_allowed(receiver_id)) {
      nu->state = 0;
    }
    g_idle_add(tci_apply_rx_nr_enable_update, nu);
  } else {
    tci_send_rx_nr_enable(client, receiver_id);
  }
}

static void tci_cmd_volume (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_VOLUME)) {
      tci_send_volume(client);
      return;
    }
    double value = tci_clamp_volume(tci_double(cmd->argv[0], 0.0));
    if (active_receiver != NULL && active_receiver->id >= 0 && active_receiver->id < receivers && active_receiver->id < 2) {
      EXT_AF_GAIN_UPDATE *ag = g_new (EXT_AF_GAIN_UPDATE, 1);
      ag->receiver_id = active_receiver->id;
      ag->value = value;
      g_idle_add (ext_set_af_gain, ag);
      tci_send_volume_value (client, value);
    }
  } else {
    tci_send_volume (client);
  }
}

static void tci_cmd_rx_volume (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  int channel = tci_int (cmd->argv[1], 0);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (channel < 0 || channel > 1) {
    return;
  }
  if (cmd->argc >= 3) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_VOLUME)) {
      tci_send_rx_volume(client, receiver_id, channel);
      return;
    }
    double value = tci_clamp_volume(tci_double(cmd->argv[2], 0.0));
    EXT_AF_GAIN_UPDATE *ag = g_new (EXT_AF_GAIN_UPDATE, 1);
    ag->receiver_id = receiver_id;
    ag->value = value;
    g_idle_add (ext_set_af_gain, ag);
    tci_send_rx_volume_value (client, receiver_id, channel, value);
  } else {
    tci_send_rx_volume (client, receiver_id, channel);
  }
}


static void tci_cmd_agc_gain (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_AGC)) {
      tci_send_agc_gain(client, receiver_id);
      return;
    }
    double value = tci_clamp_agc_gain(tci_double(cmd->argv[1], receiver[receiver_id]->agc_gain));
    EXT_AGC_GAIN_UPDATE *ag = g_new (EXT_AGC_GAIN_UPDATE, 1);
    ag->receiver_id = receiver_id;
    ag->value = value;
    g_idle_add (ext_set_agc_gain, ag);
    tci_broadcast_agc_gain_value (receiver_id, value);
  } else {
    tci_send_agc_gain (client, receiver_id);
  }
}

static void tci_cmd_agc_mode (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], 0);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_AGC)) {
      tci_send_agc_mode(client, receiver_id);
      return;
    }
    int agc = tci_parse_agc_mode(cmd->argv[1]);
    if (agc < 0 || agc >= AGC_LAST) {
      return;
    }
    EXT_AGC_MODE_UPDATE *am = g_new(EXT_AGC_MODE_UPDATE, 1);
    am->receiver_id = receiver_id;
    am->agc = agc;
    g_idle_add(ext_set_agc_mode, am);
    tci_broadcast_agc_mode_value(receiver_id, agc);
  } else {
    tci_send_agc_mode(client, receiver_id);
  }
}

static void tci_cmd_sql_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_SQL)) {
      tci_send_sql_enable(client, receiver_id);
      return;
    }
    TCI_SQL_ENABLE_UPDATE *su = g_new(TCI_SQL_ENABLE_UPDATE, 1);
    su->receiver_id = receiver_id;
    su->state = tci_bool(cmd->argv[1]) ? 1 : 0;
    tci_send_sql_enable_value(client, receiver_id, su->state);
    g_idle_add(tci_apply_sql_enable_update, su);
  } else {
    tci_send_sql_enable(client, receiver_id);
  }
}

static void tci_cmd_sql_level (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int(cmd->argv[0], -1);
  if (receiver_id < 0 || receiver_id >= receivers || receiver_id >= 2 || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_SQL)) {
      tci_send_sql_level(client, receiver_id);
      return;
    }
    TCI_SQL_LEVEL_UPDATE *su = g_new(TCI_SQL_LEVEL_UPDATE, 1);
    su->receiver_id = receiver_id;
    su->level_db = tci_clamp_sql_level(tci_double(cmd->argv[1], tci_sql_db_from_slider(receiver[receiver_id]->squelch)));
    tci_send_sql_level_value(client, receiver_id, su->level_db);
    g_idle_add(tci_apply_sql_level_update, su);
  } else {
    tci_send_sql_level(client, receiver_id);
  }
}

static int tci_sensor_interval_ms (const TCI_CMD *cmd) {
  int interval = 1000;
  if (cmd != NULL && cmd->argc >= 2) {
    interval = tci_int(cmd->argv[1], 1000);
  }
  if (interval < 30) {
    interval = 30;
  } else if (interval > 1000) {
    interval = 1000;
  }
  return interval;
}

static void tci_cmd_rx_sensors_enable (CLIENT *client, const TCI_CMD *cmd) {
  g_mutex_lock (&tci_mutex);
  client->rxsensor = tci_bool (cmd->argv[0]);
  client->rxsensor_interval_ms = tci_sensor_interval_ms(cmd);
  client->rxsensor_last_us = 0;
  g_mutex_unlock (&tci_mutex);
}

static void tci_cmd_tx_sensors_enable (CLIENT *client, const TCI_CMD *cmd) {
  g_mutex_lock (&tci_mutex);
  client->txsensor = tci_bool (cmd->argv[0]);
  client->txsensor_interval_ms = tci_sensor_interval_ms(cmd);
  client->txsensor_last_us = 0;
  g_mutex_unlock (&tci_mutex);
}

static void tci_cmd_spot (CLIENT *client, const TCI_CMD *cmd) {
  const char *dxcall;
  char *endptr = NULL;
  long long freq_hz;
  (void) client;
  if (cmd->argc < 3 || cmd->argv[0] == NULL || cmd->argv[2] == NULL) {
    return;
  }
  dxcall = cmd->argv[0];
  if (dxcall[0] == '\0') {
    return;
  }
  freq_hz = g_ascii_strtoll(cmd->argv[2], &endptr, 10);
  if (endptr == cmd->argv[2] || freq_hz <= 0) {
    return;
  }
  pan_add_dx_spot((double) freq_hz / 1000.0, dxcall);
}

static void tci_cmd_spot_delete (CLIENT *client, const TCI_CMD *cmd) {
  (void) client;
  if (cmd->argc < 1 || cmd->argv[0] == NULL || cmd->argv[0][0] == '\0') {
    return;
  }
  pan_delete_dx_spot(cmd->argv[0]);
}

static void tci_cmd_spot_clear (CLIENT *client, const TCI_CMD *cmd) {
  (void) client;
  (void) cmd;
  pan_clear_labels();
}

static void tci_send_iq_stream_start (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "iq_start:%d;", receiver_id);
  tci_send_text (client, msg);
}

static void tci_send_iq_stream_stop (CLIENT *client, int receiver_id) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "iq_stop:%d;", receiver_id);
  tci_send_text (client, msg);
}

static void tci_cmd_iq_start (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = 0;
  int samplerate;
  int apply_samplerate = 0;
  if (client == NULL) { return; }
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    receiver_id = tci_int (cmd->argv[0], 0);
  }
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  if (receiver_id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return; }
  g_mutex_lock (&tci_mutex);
  samplerate = client->iq_sample_rate;
  if (!tci_iq_sample_rate_valid (samplerate)) {
    samplerate = tci_iq_current_radio_sample_rate();
  }
  if (tci_iq_stream_sample_rate != 0) {
    samplerate = tci_iq_stream_sample_rate;
  } else {
    tci_iq_stream_sample_rate = samplerate;
    tci_iq_stream_owner = client;
    apply_samplerate = 1;
  }
  client->iq_sample_rate = samplerate;
  client->iq_stream_enabled[receiver_id] = 1;
  g_mutex_unlock (&tci_mutex);
  if (apply_samplerate && (active_receiver == NULL || active_receiver->sample_rate != samplerate)) {
    g_idle_add(ext_set_iq_samplerate, GINT_TO_POINTER(samplerate));
  }
  tci_update_iq_stream_global();
  tci_send_iq_stream_start (client, receiver_id);
}

static void tci_cmd_iq_stop (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = 0;
  if (client == NULL) { return; }
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    receiver_id = tci_int (cmd->argv[0], 0);
  }
  if (receiver_id < 0 || receiver_id >= TCI_RX_AUDIO_MAX_RECEIVERS) { return; }
  g_mutex_lock (&tci_mutex);
  client->iq_stream_enabled[receiver_id] = 0;
  if (client == tci_iq_stream_owner) {
    for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
      client->iq_stream_enabled[i] = 0;
    }
    tci_iq_stream_owner = NULL;
    tci_iq_stream_sample_rate = 0;
  }
  g_mutex_unlock (&tci_mutex);
  tci_update_iq_stream_global();
  tci_send_iq_stream_stop (client, receiver_id);
}

static void tci_cmd_audio_start (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  g_mutex_lock (&tci_mutex);
  client->rx_audio_enabled[receiver_id] = 1;
  client->rx_audio_read_count[receiver_id] = tci_audio_get_write_count (receiver_id);
  client->rx_audio_queue_count[receiver_id] = 0;
  client->rx_audio_empty_count[receiver_id] = 0;
  g_mutex_unlock (&tci_mutex);
  tci_update_rx_audio_global();
  if (rigctl_debug) {
    t_print ("TCI%d audio_start rx=%d enabled=%d read=%llu write=%llu sample_rate=%d stream_frames=%u channels=2 sample_type=float32\n",
             client->seq,
             receiver_id,
             client->rx_audio_enabled[receiver_id],
             (unsigned long long) client->rx_audio_read_count[receiver_id],
             (unsigned long long) tci_audio_get_write_count (receiver_id),
             client->audio_sample_rate,
             tci_audio_get_stream_frames());
  }
  snprintf (msg, MAXMSGSIZE, "audio_start:%d;", receiver_id);
  tci_send_text (client, msg);
}

static void tci_send_iq_samplerate (CLIENT *client) {
  char msg[MAXMSGSIZE];
  int samplerate;
  if (client == NULL) { return; }
  samplerate = tci_iq_effective_sample_rate (client, client->iq_sample_rate);
  snprintf (msg, MAXMSGSIZE, "iq_samplerate:%d;", samplerate);
  tci_send_text (client, msg);
}

static void tci_send_audio_samplerate (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "audio_samplerate:%d;", client->audio_sample_rate);
  tci_send_text (client, msg);
}

static void tci_send_audio_stream_sample_type (CLIENT *client) {
  tci_send_text (client, "audio_stream_sample_type:float32;");
}

static void tci_send_audio_stream_channels (CLIENT *client) {
  tci_send_text (client, "audio_stream_channels:2;");
}

static void tci_send_audio_stream_samples (CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf (msg, MAXMSGSIZE, "audio_stream_samples:%u;", tci_audio_get_stream_frames());
  tci_send_text (client, msg);
}

static void tci_cmd_audio_samplerate (CLIENT *client, const TCI_CMD *cmd) {
  int sample_rate;
  if (client == NULL) { return; }
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    sample_rate = tci_int (cmd->argv[0], TCI_AUDIO_SAMPLE_RATE);
    sample_rate = tci_audio_normalize_sample_rate (sample_rate);
    if (client->audio_sample_rate != sample_rate) {
      client->audio_sample_rate = sample_rate;
      for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
        tci_audio_destroy_rx_resamplers (&client->rx_audio_resampler_l[i],
                                         &client->rx_audio_resampler_r[i]);
      }
      tci_audio_destroy_tx_resampler (&client->tx_audio_resampler_24_to_48);
    }
  }
  tci_send_audio_samplerate (client);
  tci_send_audio_stream_samples (client);
}

static void tci_cmd_iq_samplerate(CLIENT *client, const TCI_CMD *cmd) {
  int samplerate;
  int requested;
  char msg[MAXMSGSIZE];
  if (client == NULL || cmd->argc != 1 || cmd->argv[0] == NULL) {
    return;
  }
  requested = tci_int(cmd->argv[0], 0);
  if (!tci_iq_sample_rate_valid (requested)) {
    return;
  }
  if (!tci_set_lock_allowed(client, TCI_SET_LOCK_IQ_RATE)) {
    tci_send_iq_samplerate(client);
    return;
  }
  samplerate = tci_iq_effective_sample_rate (client, requested);
  if (g_atomic_int_get (&tci_iq_stream_clients) == 0 &&
      (active_receiver == NULL || active_receiver->sample_rate != samplerate)) {
    g_idle_add(ext_set_iq_samplerate, GINT_TO_POINTER(samplerate));
  }
  snprintf(msg, MAXMSGSIZE, "iq_samplerate:%d;", samplerate);
  tci_send_text(client, msg);
}

static void tci_cmd_mon_volume(CLIENT *client, const TCI_CMD *cmd) {
  (void) cmd;
  if (client == NULL) {
    return;
  }
  tci_send_text(client, "mon_volume:-60;");
}

static void tci_cmd_mon_enable(CLIENT *client, const TCI_CMD *cmd) {
  (void) cmd;
  if (client == NULL) {
    return;
  }
  tci_send_text(client, "mon_enable:false;");
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

static void tci_cmd_tx_stream_audio_buffering (CLIENT *client, const TCI_CMD *cmd) {
  int buffering_ms = 50;
  char msg[MAXMSGSIZE];
  if (client == NULL) {
    return;
  }
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    buffering_ms = tci_int (cmd->argv[0], 50);
  }
  if (buffering_ms < 50) {
    buffering_ms = 50;
  } else if (buffering_ms > 500) {
    buffering_ms = 500;
  }
  snprintf (msg, MAXMSGSIZE, "tx_stream_audio_buffering:%d;", buffering_ms);
  tci_send_text (client, msg);
}

static void tci_cmd_tx_enable (CLIENT *client, const TCI_CMD *cmd) {
  (void) cmd;
  if (client == NULL) {
    return;
  }
  tci_send_tx_enable (client);
}

static void tci_cmd_rx_enable (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = 0;
  if (client == NULL) {
    return;
  }
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    receiver_id = tci_int (cmd->argv[0], 0);
  }
  tci_send_rx_enable (client, receiver_id);
}

static void tci_cmd_audio_stop (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  char msg[MAXMSGSIZE];
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) { return; }
  g_mutex_lock (&tci_mutex);
  client->rx_audio_enabled[receiver_id] = 0;
  g_mutex_unlock (&tci_mutex);
  tci_update_rx_audio_global();
  if (rigctl_debug) {
    t_print ("TCI%d audio_stop rx=%d queued=%llu empty=%llu read=%llu write=%llu sample_rate=%d\n",
             client->seq,
             receiver_id,
             (unsigned long long) client->rx_audio_queue_count[receiver_id],
             (unsigned long long) client->rx_audio_empty_count[receiver_id],
             (unsigned long long) client->rx_audio_read_count[receiver_id],
             (unsigned long long) tci_audio_get_write_count (receiver_id),
             client->audio_sample_rate);
  }
#ifdef PORTAUDIO
  if (tci_audio_monitor && !tci_has_audio_monitor_source()) {
    audio_close_tci_monitor();
  }
#endif
  snprintf (msg, MAXMSGSIZE, "audio_stop:%d;", receiver_id);
  tci_send_text (client, msg);
}

static void tci_cmd_modulation (CLIENT *client, const TCI_CMD *cmd) {
  int VfoNr = tci_int (cmd->argv[0], 0);
  if (cmd->argc >= 2) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_MODE)) {
      tci_send_mode(client, VfoNr);
      return;
    }
    tci_set_mode (client, VfoNr, cmd->argv[1]);
  } else {
    tci_send_mode (client, VfoNr);
  }
}

static void tci_cmd_vfo (CLIENT *client, const TCI_CMD *cmd) {
  int VfoNr = tci_int (cmd->argv[0], 0);
  int Ch = tci_int (cmd->argv[1], 0);
  if (cmd->argc >= 3) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_VFO)) {
      tci_send_vfo(client, VfoNr, Ch);
      return;
    }
    tci_set_vfo (client, VfoNr, Ch, tci_ll (cmd->argv[2], 0));
  } else {
    tci_send_vfo (client, VfoNr, Ch);
  }
}

static void tci_cmd_rx_smeter (CLIENT *client, const TCI_CMD *cmd) {
  tci_send_smeter (client, tci_int (cmd->argv[0], 0));
}

static void tci_cmd_drive (CLIENT *client, const TCI_CMD *cmd) {
  int trx;
  int value;
  int changed = 0;
  if (cmd->argc >= 1) {
    trx = tci_int (cmd->argv[0], -1);
    if (trx != 0) {
      return;
    }
    if (cmd->argc >= 2) {
      if (!tci_set_lock_allowed(client, TCI_SET_LOCK_DRIVE)) {
        tci_send_drive(client);
        return;
      }
      value = tci_int (cmd->argv[1], 0);
      if (value < 0) {
        value = 0;
      }
      if (value > 100) {
        value = 100;
      }
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
      changed = 1;
    }
  }
  if (changed) {
    tci_broadcast_drive();
  } else {
    tci_send_drive (client);
  }
}


static void tci_cmd_tune_drive (CLIENT *client, const TCI_CMD *cmd) {
  int trx;
  int value;
  int changed = 0;
  if (cmd->argc >= 1) {
    trx = tci_int (cmd->argv[0], -1);
    if (trx != 0) {
      return;
    }
    if (cmd->argc >= 2) {
      if (!tci_set_lock_allowed(client, TCI_SET_LOCK_DRIVE)) {
        tci_send_tune_drive(client);
        return;
      }
      value = tci_int (cmd->argv[1], 0);
      if (value < 0) {
        value = 0;
      }
      if (value > 100) {
        value = 100;
      }
      if (transmitter != NULL) {
        int v = vfo_get_tx_vfo();
        int b = vfo[v].band;
        BANDSETTINGS *bs = band_get_settings (b);
        transmitter->tune_drive = value;
        if (bs != NULL) {
          bs->tune_drive = transmitter->tune_drive;
          t_print ("%s: bs->tune_drive = %d\n", __func__, bs->tune_drive);
        }
        if (can_transmit && transmitter->tune_use_drive) {
          transmitter->tune_use_drive = 0;
        }
        if (display_extra_sliders) {
          update_slider_tune_drive_scale (TRUE);
        }
        changed = 1;
      }
    }
  }
  if (changed) {
    tci_broadcast_tune_drive();
  } else {
    tci_send_tune_drive (client);
  }
}


static void tci_cmd_rx_filter_band (CLIENT *client, const TCI_CMD *cmd) {
  int receiver_id = tci_int (cmd->argv[0], 0);
  if (receiver_id < 0 || receiver_id >= receivers || receiver[receiver_id] == NULL) {
    return;
  }
  if (cmd->argc >= 3) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_FILTER)) {
      tci_send_rx_filter_band(client, receiver_id);
      return;
    }
    EXT_RX_FILTER_UPDATE *fu;
    int low = tci_int (cmd->argv[1], 0);
    int high = tci_int (cmd->argv[2], 0);
    if (!ext_normalize_rx_filter_band(vfo[receiver_id].mode, &low, &high)) {
      return;
    }
    fu = g_new (EXT_RX_FILTER_UPDATE, 1);
    fu->receiver_id = receiver_id;
    fu->low = low;
    fu->high = high;
    g_idle_add (ext_rx_filter_update, fu);
    tci_broadcast_rx_filter_band_value (receiver_id, low, high);
  } else {
    tci_send_rx_filter_band (client, receiver_id);
  }
}


static char *tci_cw_decode_text(const char *text) {
  GString *out;
  int speed_open = 0;
  if (text == NULL) {
    return NULL;
  }
  out = g_string_new(NULL);
  for (const char *p = text; *p != '\0'; p++) {
    switch (*p) {
    case '^':
      g_string_append_c(out, ':');
      break;
    case '~':
      g_string_append_c(out, ',');
      break;
    case '*':
      g_string_append_c(out, ';');
      break;
    case '|': {
      const char *end = strchr(p + 1, '|');
      if (end != NULL) {
        if (speed_open) {
          g_string_append_c(out, ']');
          speed_open = 0;
        }
        g_string_append(out, "[.");
        for (const char *q = p + 1; q < end; q++) {
          switch (*q) {
          case '^':
            g_string_append_c(out, ':');
            break;
          case '~':
            g_string_append_c(out, ',');
            break;
          case '*':
            g_string_append_c(out, ';');
            break;
          default:
            g_string_append_c(out, *q);
            break;
          }
        }
        g_string_append_c(out, ']');
        p = end;
      } else {
        g_string_append_c(out, *p);
      }
      break;
    }
    case '>':
      if (speed_open) {
        g_string_append_c(out, ']');
      }
      g_string_append(out, "[+");
      speed_open = 1;
      break;
    case '<':
      if (speed_open) {
        g_string_append_c(out, ']');
      }
      g_string_append(out, "[-");
      speed_open = 1;
      break;
    default:
      g_string_append_c(out, *p);
      break;
    }
  }
  if (speed_open) {
    g_string_append_c(out, ']');
  }
  return g_string_free(out, FALSE);
}

static void tci_cw_msg_append_part(GString *out, const char *part) {
  if (part == NULL || part[0] == 0 || strcmp(part, "_") == 0) {
    return;
  }
  if (out->len > 0) {
    g_string_append_c(out, ' ');
  }
  g_string_append(out, part);
}

static char *tci_cw_msg_extract_callsign(const char *src, int *repeat) {
  char *call;
  char *dollar;
  int r = 1;
  if (src == NULL) {
    if (repeat != NULL) { *repeat = 1; }
    return g_strdup("");
  }
  call = g_strdup(src);
  dollar = strrchr(call, '$');
  if (dollar != NULL) {
    r = atoi(dollar + 1);
    if (r < 1) { r = 1; }
    *dollar = 0;
  }
  if (repeat != NULL) { *repeat = r; }
  return call;
}

static void tci_cmd_cw_msg(CLIENT *client, const TCI_CMD *cmd) {
  char *prefix = NULL;
  char *callsign_arg = NULL;
  char *callsign = NULL;
  char *suffix = NULL;
  GString *prefix_text;
  int repeat = 1;
  int queued = 0;
  if (cmd->argc == 1 && cmd->argv[0] != NULL) {
    if (!tci_cw_msg_active || tci_cw_msg_pending_callsign[0] == 0) {
      t_print("TCI%d cw_msg callsign correction ignored: no active cw_msg\n", client->seq);
      return;
    }
    callsign_arg = tci_cw_decode_text(cmd->argv[0]);
    if (callsign_arg == NULL) {
      return;
    }
    callsign = tci_cw_msg_extract_callsign(callsign_arg, NULL);
    if (callsign != NULL && callsign[0] != 0 && strcmp(callsign, "_") != 0) {
      int new_len = (int) strlen(callsign);
      g_strlcpy(tci_cw_msg_active_callsign, callsign, sizeof(tci_cw_msg_active_callsign));
      g_strlcpy(tci_cw_msg_pending_callsign, callsign, sizeof(tci_cw_msg_pending_callsign));
      if (tci_cw_msg_call_pos > new_len) {
        tci_cw_msg_call_pos = new_len;
      }
      t_print("TCI%d cw_msg callsign correction accepted callsign=%s pos=%d repeat=%d/%d\n", client->seq,
              tci_cw_msg_active_callsign, tci_cw_msg_call_pos, tci_cw_msg_call_repeat_index + 1,
              tci_cw_msg_call_repeat);
    }
    g_free(callsign_arg);
    g_free(callsign);
    return;
  }
  if (cmd->argc < 4 || cmd->argv[1] == NULL || cmd->argv[2] == NULL || cmd->argv[3] == NULL) {
    return;
  }
  prefix = tci_cw_decode_text(cmd->argv[1]);
  callsign_arg = tci_cw_decode_text(cmd->argv[2]);
  suffix = tci_cw_decode_text(cmd->argv[3]);
  if (prefix == NULL || callsign_arg == NULL || suffix == NULL) {
    g_free(prefix);
    g_free(callsign_arg);
    g_free(suffix);
    return;
  }
  callsign = tci_cw_msg_extract_callsign(callsign_arg, &repeat);
  tci_cw_msg_reset_state();
  g_strlcpy(tci_cw_msg_active_callsign, callsign, sizeof(tci_cw_msg_active_callsign));
  g_strlcpy(tci_cw_msg_pending_callsign, callsign, sizeof(tci_cw_msg_pending_callsign));
  if (suffix[0] != 0 && strcmp(suffix, "_") != 0) {
    g_strlcpy(tci_cw_msg_active_suffix, suffix, sizeof(tci_cw_msg_active_suffix));
    tci_cw_msg_suffix_pending = 1;
  }
  tci_cw_msg_call_repeat = repeat;
  tci_cw_msg_call_repeat_index = 0;
  tci_cw_msg_call_pos = 0;
  tci_cw_msg_active = 1;
  prefix_text = g_string_new(NULL);
  tci_cw_msg_append_part(prefix_text, prefix);
  if (prefix_text->len > 0) {
    g_string_append_c(prefix_text, ' ');
    queued = cw_engine_queue_text(prefix_text->str);
  } else {
    queued = tci_cw_msg_queue_next();
  }
  t_print("TCI%d cw_msg queued=%d prefix=%s callsign=%s repeat=%d suffix=%s\n", client->seq, queued,
          prefix_text->str, callsign, repeat, tci_cw_msg_active_suffix[0] ? tci_cw_msg_active_suffix : "_");
  g_string_free(prefix_text, TRUE);
  g_free(prefix);
  g_free(callsign_arg);
  g_free(callsign);
  g_free(suffix);
}

static void tci_cmd_cw_macros_speed (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_CW_SPEED)) {
      tci_send_macros_cwspeed(client);
      return;
    }
    cw_keyer_speed = tci_clamp_int(tci_int(cmd->argv[0], cw_keyer_speed), 1, 100);
  }
  tci_send_macros_cwspeed (client);
}

static void tci_cmd_cw_keyer_speed (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_CW_SPEED)) {
      tci_send_keyer_cwspeed(client);
      return;
    }
    cw_keyer_speed = tci_clamp_int(tci_int(cmd->argv[0], cw_keyer_speed), 1, 100);
  }
  tci_send_keyer_cwspeed (client);
}

static void tci_cmd_cw_macros_delay (CLIENT *client, const TCI_CMD *cmd) {
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    if (!tci_set_lock_allowed(client, TCI_SET_LOCK_CW_SPEED)) {
      tci_send_cw_macros_delay(client);
      return;
    }
    tci_cw_macros_delay_ms = tci_clamp_int(tci_int(cmd->argv[0], tci_cw_macros_delay_ms), 0, 5000);
    cw_engine_set_start_delay(tci_cw_macros_delay_ms);
  }
  tci_send_cw_macros_delay(client);
}

static void tci_cmd_cw_macros_speed_up(CLIENT *client, const TCI_CMD *cmd) {
  int step = 1;
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    step = tci_int(cmd->argv[0], 1);
  }
  if (step < 0) { step = -step; }
  if (!tci_set_lock_allowed(client, TCI_SET_LOCK_CW_SPEED)) {
    tci_send_macros_cwspeed(client);
    return;
  }
  cw_keyer_speed = tci_clamp_int(cw_keyer_speed + step, 1, 100);
  tci_send_macros_cwspeed(client);
}

static void tci_cmd_cw_macros_speed_down(CLIENT *client, const TCI_CMD *cmd) {
  int step = 1;
  if (cmd->argc >= 1 && cmd->argv[0] != NULL) {
    step = tci_int(cmd->argv[0], 1);
  }
  if (step < 0) { step = -step; }
  if (!tci_set_lock_allowed(client, TCI_SET_LOCK_CW_SPEED)) {
    tci_send_macros_cwspeed(client);
    return;
  }
  cw_keyer_speed = tci_clamp_int(cw_keyer_speed - step, 1, 100);
  tci_send_macros_cwspeed(client);
}


static void tci_cmd_cw_macros (CLIENT *client, const TCI_CMD *cmd) {
  GString *raw;
  char *decoded;
  int queued;
  if (cmd->argc < 2 || cmd->argv[1] == NULL) {
    return;
  }
  raw = g_string_new(cmd->argv[1]);
  for (int i = 2; i < cmd->argc; i++) {
    g_string_append_c(raw, ',');
    if (cmd->argv[i] != NULL) {
      g_string_append(raw, cmd->argv[i]);
    }
  }
  decoded = tci_cw_decode_text(raw->str);
  g_string_free(raw, TRUE);
  if (decoded == NULL) {
    return;
  }
  queued = cw_engine_queue_text(decoded);
  t_print("TCI%d cw_macros queued=%d text=%s\n", client->seq, queued, decoded);
  g_free(decoded);
}

static void tci_cmd_cw_macros_stop (CLIENT *client, const TCI_CMD *cmd) {
  (void) cmd;
  cw_engine_set_terminal(0);
  cw_engine_clear();
  tci_cw_msg_reset_state();
  t_print("TCI%d cw_macros_stop\n", client->seq);
}

static void tci_cmd_cw_terminal (CLIENT *client, const TCI_CMD *cmd) {
  int enabled;
  if (cmd->argc < 1 || cmd->argv[0] == NULL) {
    return;
  }
  enabled = g_ascii_strcasecmp(cmd->argv[0], "true") == 0 || strcmp(cmd->argv[0], "1") == 0;
  cw_engine_set_terminal(enabled);
  if (enabled) {
    tci_send_text(client, "cw_terminal:true;");
  } else {
    tci_send_text(client, "cw_terminal:false;");
  }
  t_print("TCI%d cw_terminal=%d\n", client->seq, enabled);
}

static void tci_cmd_stop (CLIENT *client, const TCI_CMD *cmd) {
  TCI_TX_OWNER_MODE owner_mode = TCI_TX_OWNER_NONE;
  (void) cmd;
  client->rxsensor = 0;
  client->txsensor = 0;
  tci_send_text (client, "stop;");
  g_mutex_lock (&tci_mutex);
  if (client == tci_tx_owner) {
    owner_mode = tci_tx_owner_mode;
    tci_tx_owner = NULL;
    tci_tx_owner_mode = TCI_TX_OWNER_NONE;
  }
  client->running = 0;
  g_mutex_unlock (&tci_mutex);
  if (owner_mode == TCI_TX_OWNER_TUNE) {
    g_idle_add (ext_tune_update, GINT_TO_POINTER (0));
    t_print ("TCI%d TUNE owner stopped, forcing TUNE off\n", client->seq);
  } else if (owner_mode == TCI_TX_OWNER_MOX) {
    tci_tx_client_cleanup_tx_audio(client);
    g_idle_add (ext_mox_update, GINT_TO_POINTER (0));
    t_print ("TCI%d TX owner stopped, forcing RX\n", client->seq);
  }
}

static const TCI_DISPATCH tci_dispatch[] = {
  { "trx_count",         0,  0, tci_cmd_trx_count },
  { "trx",               0, -1, tci_cmd_trx },
  { "tune",              0, -1, tci_cmd_tune },
  { "split_enable",      1,  2, tci_cmd_split_enable },
  { "lock",              1,  2, tci_cmd_lock },
  { "vfo_lock",          1,  3, tci_cmd_vfo_lock },
  { "sql_enable",        1,  2, tci_cmd_sql_enable },
  { "sql_level",         1,  2, tci_cmd_sql_level },
  { "rx_anf_enable",     1,  2, tci_cmd_rx_anf_enable },
  { "rx_apf_enable",     1,  2, tci_cmd_rx_apf_enable },
  { "rx_nb_enable",      1,  2, tci_cmd_rx_nb_enable },
  { "rx_nf_enable",      1,  2, tci_cmd_rx_nf_enable },
  { "rx_bin_enable",     1,  2, tci_cmd_rx_bin_enable },
  { "rx_nr_enable",      1,  2, tci_cmd_rx_nr_enable },
  { "rit_enable",        1,  2, tci_cmd_rit_enable },
  { "xit_enable",        1,  2, tci_cmd_xit_enable },
  { "rit_offset",        1,  2, tci_cmd_rit_offset },
  { "xit_offset",        1,  2, tci_cmd_xit_offset },
  { "digl_offset",      0,  1, tci_cmd_digl_offset },
  { "digu_offset",      0,  1, tci_cmd_digu_offset },
  { "mute",              0,  1, tci_cmd_mute },
  { "rx_mute",           1,  2, tci_cmd_rx_mute },
  { "volume",            0,  1, tci_cmd_volume },
  { "rx_volume",         2,  3, tci_cmd_rx_volume },
  { "agc_gain",          1,  2, tci_cmd_agc_gain },
  { "agc_mode",          1,  2, tci_cmd_agc_mode },
  { "iq_start",          0,  1, tci_cmd_iq_start },
  { "iq_stop",           0,  1, tci_cmd_iq_stop },
  { "iq_samplerate",     1,  1, tci_cmd_iq_samplerate },
  { "mon_volume",        0,  1, tci_cmd_mon_volume },
  { "mon_enable",        0,  1, tci_cmd_mon_enable },
  { "audio_samplerate",            0, -1, tci_cmd_audio_samplerate },
  { "audio_stream_sample_type",    0, -1, tci_cmd_audio_stream_sample_type },
  { "audio_stream_channels",       0, -1, tci_cmd_audio_stream_channels },
  { "audio_stream_samples",        0, -1, tci_cmd_audio_stream_samples },
  { "tx_stream_audio_buffering",   0,  1, tci_cmd_tx_stream_audio_buffering },
  { "tx_enable",         1,  2, tci_cmd_tx_enable },
  { "rx_enable",         1,  2, tci_cmd_rx_enable },
  { "rx_sensors_enable", 1,  2, tci_cmd_rx_sensors_enable },
  { "tx_sensors_enable", 1,  2, tci_cmd_tx_sensors_enable },
  { "spot",              3,  5, tci_cmd_spot },
  { "spot_delete",       1,  1, tci_cmd_spot_delete },
  { "spot_clear",        0,  0, tci_cmd_spot_clear },
  { "audio_start",       1,  1, tci_cmd_audio_start },
  { "audio_stop",        1,  1, tci_cmd_audio_stop },
  { "modulation",        1,  2, tci_cmd_modulation },
  { "vfo",               2,  3, tci_cmd_vfo },
  { "rx_smeter",         1,  3, tci_cmd_rx_smeter },
  { "drive",             0,  2, tci_cmd_drive },
  { "tune_drive",        1,  2, tci_cmd_tune_drive },
  { "rx_filter_band",    1,  3, tci_cmd_rx_filter_band },
  { "cw_macros",         2, -1, tci_cmd_cw_macros },
  { "cw_macros_stop",    0,  0, tci_cmd_cw_macros_stop },
  { "cw_msg",             1,  4, tci_cmd_cw_msg },
  { "cw_terminal",        1,  1, tci_cmd_cw_terminal },
  { "cw_macros_speed",   0,  1, tci_cmd_cw_macros_speed },
  { "cw_keyer_speed",    0,  1, tci_cmd_cw_keyer_speed },
  { "cw_macros_delay",   0,  1, tci_cmd_cw_macros_delay },
  { "cw_macros_speed_up",   1,  1, tci_cmd_cw_macros_speed_up },
  { "cw_macros_speed_down", 1,  1, tci_cmd_cw_macros_speed_down },
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
  if (v >= receivers || receiver[v] == NULL) { return; }
  lvl = (int) (receiver[v]->meter - 0.5);
  // snprintf(msg, MAXMSGSIZE, "rx_smeter:%d,0,%d.0;",v,lvl);
  // tci_send_text(client, msg);
  // snprintf(msg, MAXMSGSIZE, "rx_smeter:%d,1,%d.0;",v,lvl);
  // tci_send_text(client, msg);
  snprintf (msg, MAXMSGSIZE, "rx_sensors:%d,%d.0;", v, lvl);
  tci_send_text (client, msg);
}

__attribute__ ((unused)) static void tci_send_rx (CLIENT *client, int v) {
  //
  // Send S-meter reading.
  // ATTENTION: in some countries, %f sends a comma instead of a decimal
  //            point and this is a desaster. Therefore we fake a
  //            floating point number.
  //
  char msg[MAXMSGSIZE];
  int lvl;
  if (v < 0 || v > 1) { return; }
  if (v >= receivers || receiver[v] == NULL) { return; }
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
    gint64 now_us = g_get_monotonic_time();
    if (client->rxsensor && (!mx || duplex) &&
        (client->rxsensor_last_us == 0 ||
         (now_us - client->rxsensor_last_us) >= ((gint64) client->rxsensor_interval_ms * 1000))) {
      client->rxsensor_last_us = now_us;
      if (receivers == 1) {
        tci_send_smeter (client, 0);
      } else {
        tci_send_smeter (client, 0);
        tci_send_smeter (client, 1);
      }
    }
    if (client->txsensor && mx && transmitter != NULL && can_transmit && transmitter->fwd > 0.01 &&
        (client->txsensor_last_us == 0 ||
         (now_us - client->txsensor_last_us) >= ((gint64) client->txsensor_interval_ms * 1000))) {
      client->txsensor_last_us = now_us;
      tci_send_tx_sensors (client);
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
      if (receivers > 1) {
        tci_send_vfo (client, 1, 0);
        tci_send_vfo (client, 1, 1);
      }
    }
    if (ma  != client->last_ma) {
      tci_send_mode (client, 0);
      tci_send_rx_filter_band (client, 0);
    }
    if (mb  != client->last_mb) {
      if (receivers > 1) {
        tci_send_mode (client, 1);
        tci_send_rx_filter_band (client, 1);
      } else {
        client->last_mb = mb;
      }
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
  client->rxsensor_interval_ms = 1000;
  client->txsensor_interval_ms = 1000;
  client->rxsensor_last_us = 0;
  client->txsensor_last_us = 0;
  client->tx_chrono_tick  =  0;
  client->idle_queued     =  0;
  client->tci_timer       =  0;
  client->wsi             = NULL;
  client->lws_tx_queue    = NULL;
  client->initial_sent    =  0;
  client->device          = NULL;
  client->device_index    = -1;
  client->audio_sample_rate = TCI_AUDIO_SAMPLE_RATE;
  client->iq_sample_rate = 0;
  client->tx_audio_resampler_24_to_48 = NULL;
  for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
    client->rx_audio_enabled[i] = 0;
    client->rx_audio_read_count[i] = 0;
    client->rx_audio_queue_count[i] = 0;
    client->rx_audio_empty_count[i] = 0;
    client->rx_audio_resampler_l[i] = NULL;
    client->rx_audio_resampler_r[i] = NULL;
    client->iq_stream_enabled[i] = 0;
    client->iq_cw_phase[i] = 0.0;
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
  tci_send_text (client, "channel_count:2;");
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
  tci_send_iq_samplerate (client);
  tci_send_audio_samplerate (client);
  tci_send_text (client, "audio_stream_sample_type:float32;");
  tci_send_text (client, "audio_stream_channels:1;");
  tci_send_text (client, "modulations_list:LSB,USB,DSB,CW,FMN,AM,DIGU,SPEC,DIGL,SAM,DRM;");
  tci_send_dds (client, VFO_A);
  tci_send_text (client, "if:0,0,0;");
  tci_send_text (client, "if:0,1,0;");
  tci_send_vfo (client, VFO_A, 0);
  tci_send_vfo (client, VFO_A, 1);
  tci_send_mode (client, VFO_A);
  tci_send_rx_filter_band (client, VFO_A);
  if (receivers > 1) {
    tci_send_dds (client, VFO_B);
    tci_send_text (client, "if:1,0,0;");
    tci_send_text (client, "if:1,1,0;");
    tci_send_vfo (client, VFO_B, 0);
    tci_send_vfo (client, VFO_B, 1);
    tci_send_mode (client, VFO_B);
    tci_send_rx_filter_band (client, VFO_B);
  }
  tci_send_rx_enable (client, 0);
  if (receivers > 1) {
    tci_send_rx_enable (client, 1);
  }
  tci_send_tx_enable (client);
  tci_send_split (client);
  tci_send_lock (client, VFO_A);
  tci_send_vfo_locks (client, VFO_A);
  tci_send_sql_enable (client, VFO_A);
  tci_send_sql_level (client, VFO_A);
  tci_send_rx_anf_enable (client, VFO_A);
  tci_send_rx_apf_enable (client, VFO_A);
  tci_send_rx_nb_enable (client, VFO_A);
  tci_send_rx_nf_enable (client, VFO_A);
  tci_send_rx_bin_enable (client, VFO_A);
  tci_send_rx_nr_enable (client, VFO_A);
  tci_send_rit_enable (client, VFO_A);
  tci_send_rit_offset (client, VFO_A);
  tci_send_xit_enable (client);
  tci_send_xit_offset (client);
  tci_send_digu_offset (client);
  tci_send_digl_offset (client);
  tci_send_mox (client);
  tci_send_tune (client);
  for (int i = 0; i < receivers && i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
    tci_send_iq_stream_stop (client, i);
  }
  tci_send_tune_drive (client);
  tci_send_mute (client);
  tci_send_rx_mute (client, VFO_A);
  tci_send_volume (client);
  tci_send_rx_volume (client, VFO_A, 0);
  tci_send_rx_volume (client, VFO_A, 1);
  tci_send_agc_gain (client, VFO_A);
  tci_send_agc_mode (client, VFO_A);
  if (receivers > 1) {
    tci_send_sql_enable (client, VFO_B);
    tci_send_sql_level (client, VFO_B);
    tci_send_rx_anf_enable (client, VFO_B);
    tci_send_rx_apf_enable (client, VFO_B);
    tci_send_rx_nb_enable (client, VFO_B);
    tci_send_rx_nf_enable (client, VFO_B);
    tci_send_rx_bin_enable (client, VFO_B);
    tci_send_rx_nr_enable (client, VFO_B);
    tci_send_rit_enable (client, VFO_B);
    tci_send_rit_offset (client, VFO_B);
    tci_send_rx_mute (client, VFO_B);
    tci_send_rx_volume (client, VFO_B, 0);
    tci_send_rx_volume (client, VFO_B, 1);
    tci_send_agc_gain (client, VFO_B);
    tci_send_agc_mode (client, VFO_B);
  }
  tci_send_macros_cwspeed (client);
  tci_send_cw_macros_delay(client);
  tci_send_keyer_cwspeed (client);
  tci_send_text (client, "ready;");
  tci_send_text (client, "start;");
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
    if (client == NULL || client->wsi == NULL) { return 0; }
    if (!client->running) { return -1; }
    // if (rigctl_debug && client->lws_tx_queue != NULL && !g_queue_is_empty(client->lws_tx_queue)) {
    //  t_print("LWS WRITEABLE queued=%u\n", g_queue_get_length(client->lws_tx_queue));
    // }
    if (!client->initial_sent) {
      tci_send_initial_state (client);
      client->initial_sent = 1;
    }
    return tci_lws_write_queued (client);
  case LWS_CALLBACK_CLOSED:
    if (rigctl_debug) {
      t_print ("LWS CLOSED client=%d\n", client ? client->seq : -1);
    }
    if (client == NULL) { break; }
    TCI_TX_OWNER_MODE owner_mode = TCI_TX_OWNER_NONE;
    int cleanup_tx_audio = 0;
    g_mutex_lock (&tci_mutex);
    cleanup_tx_audio = client->tx_audio_enabled;
    client->tx_audio_enabled = 0;
    client->tx_chrono_next_us = 0;
    client->tx_chrono_tick = 0;
    client->running = 0;
    client->wsi = NULL;
    if (client == tci_iq_stream_owner) {
      tci_iq_stream_owner = NULL;
      tci_iq_stream_sample_rate = 0;
    }
    if (client == tci_digi_offset_owner) {
      tci_digi_offset_owner = NULL;
    }
    if (client == tci_tx_owner) {
      owner_mode = tci_tx_owner_mode;
      tci_tx_owner = NULL;
      tci_tx_owner_mode = TCI_TX_OWNER_NONE;
    }
    tci_set_locks_clear_client(client);
    tci_clients = g_list_remove (tci_clients, client);
    g_mutex_unlock (&tci_mutex);
    if (owner_mode == TCI_TX_OWNER_TUNE) {
      g_idle_add (ext_tune_update, GINT_TO_POINTER (0));
      t_print ("TCI%d TUNE owner disconnected, forcing TUNE off\n", client->seq);
    } else if (owner_mode == TCI_TX_OWNER_MOX) {
      g_idle_add (ext_mox_update, GINT_TO_POINTER (0));
      t_print ("TCI%d TX owner disconnected, forcing RX\n", client->seq);
    }
    if (cleanup_tx_audio || owner_mode == TCI_TX_OWNER_MOX) {
      tci_tx_client_cleanup_tx_audio(client);
    }
    tci_update_rx_audio_global();
    tci_update_iq_stream_global();
    if (client->tci_timer != 0) {
      g_source_remove (client->tci_timer);
      client->tci_timer = 0;
    }
    tci_lws_free_queue (client);
    for (int i = 0; i < TCI_RX_AUDIO_MAX_RECEIVERS; i++) {
      tci_audio_destroy_rx_resamplers (&client->rx_audio_resampler_l[i],
                                       &client->rx_audio_resampler_r[i]);
    }
    tci_audio_destroy_tx_resampler (&client->tx_audio_resampler_24_to_48);
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
  static int first = 1;
  struct lws_context_creation_info info;
  int port = GPOINTER_TO_INT (data);
  lws_set_log_level(LLL_ERR, NULL);
  memset (&info, 0, sizeof (info));
  signal (SIGPIPE, SIG_IGN);
  info.port = port;
  info.protocols = tci_lws_protocols;
  info.gid = -1;
  info.uid = -1;
  if (first) {
    info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    first = 0;
  }
  t_print ("%s: starting TCI LWS server on port %d\n", __func__, port);
  tci_lws_context = lws_create_context (&info);
  if (tci_lws_context == NULL) {
    t_print ("%s: lws_create_context failed\n", __func__);
    return NULL;
  }
  while (tci_running) {
    int do_writable = 0;
    tci_service_rx_audio();
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
