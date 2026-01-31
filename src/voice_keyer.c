/* Copyright (C)
* 2024-2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
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
*
* Voice Keyer (WAV) for deskHPSDR
*
* Policy: ONLY RIFF/WAVE, PCM16, MONO, 48000 Hz.
* Implementation: load WAV into existing capture_data[] and use existing CAP_XMIT injection
* (tx_add_mic_sample()).
*/

#include <gtk/gtk.h>
#include <glib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "voice_keyer.h"
#include "radio.h"
#include "actions.h"
#include "property.h"
#include "message.h"
#include "new_menu.h"
#include "main.h"

#define VK_SLOTS 6
#define VK_MAX_SECONDS 30
#define VK_SAMPLE_RATE 48000
#define VK_MAX_SAMPLES (VK_MAX_SECONDS * VK_SAMPLE_RATE)

static GtkWidget *vk_window = NULL;
static GtkWidget *vk_label_file[VK_SLOTS] = { NULL };
static GtkWidget *vk_label_status = NULL;
static GtkWidget *vk_btn_play[VK_SLOTS] = { NULL };
static GtkWidget *vk_btn_load[VK_SLOTS] = { NULL };

static char vk_paths[VK_SLOTS][1024] = {{0}};

// Voice Keyer MOX ownership + watchdog (internal)
static int vk_keyed_mox = 0;
static guint vk_mox_watch_id = 0;

// Voice Keyer playback lock (internal)
// Future-proof for VK_SLOTS > 1: only one playback at a time.
static int vk_play_lock = 0;
static int vk_active_slot = -1;


int is_vk = 0;

static void voicekeyerSaveState(void) {
  clearProperties();

  for (int i = 0; i < VK_SLOTS; i++) {
    SetPropS1("vk_path[%d]", i, vk_paths[i]);
  }

  saveProperties("voicekeyer.props");
}

static void voicekeyerRestoreState(void) {
  loadProperties("voicekeyer.props");

  for (int i = 0; i < VK_SLOTS; i++) {
    vk_paths[i][0] = 0;
  }

  for (int i = 0; i < VK_SLOTS; i++) {
    char name[128];
    snprintf(name, sizeof(name), "vk_path[%d]", i);
    const char *value = getProperty(name);

    if (value) {
      g_strlcpy(vk_paths[i], value, sizeof(vk_paths[i]));
    }
  }

  clearProperties();
}

static uint32_t rd_u32_le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16_le(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void cleanup(void) {
  if (vk_window != NULL) {
    voicekeyerSaveState();
    gtk_widget_destroy(vk_window);
    vk_window = NULL;
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static void set_status(const char *msg) {
  if (vk_label_status) {
    gtk_label_set_text(GTK_LABEL(vk_label_status), msg ? msg : "");
  }
}

static void error_dialog(GtkWindow *parent, const char *msg) {
  GtkWidget *d = gtk_message_dialog_new(parent,
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_ERROR,
                                        GTK_BUTTONS_CLOSE,
                                        "%s", msg ? msg : "Error");
  gtk_dialog_run(GTK_DIALOG(d));
  gtk_widget_destroy(d);
}

static gboolean load_wav_pcm16_mono_48k_into_capture(const char *path, char **err_out) {
  gsize len = 0;
  guint8 *buf = NULL;

  if (err_out) {
    *err_out = NULL;
  }

  if (!g_file_get_contents(path, (gchar **)&buf, &len, NULL)) {
    if (err_out) {
      *err_out = g_strdup("Cannot read file.");
    }

    return FALSE;
  }

  if (len < 44 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
    if (err_out) {
      *err_out = g_strdup("Not a RIFF/WAVE file.");
    }

    g_free(buf);
    return FALSE;
  }

  uint32_t fmt_off = 0, fmt_sz = 0;
  uint32_t data_off = 0, data_sz = 0;
  uint32_t off = 12;

  while (off + 8 <= len) {
    const uint8_t *ck = buf + off;
    uint32_t cksz = rd_u32_le(ck + 4);

    if (off + 8 + cksz > len) {
      break;
    }

    if (memcmp(ck, "fmt ", 4) == 0) {
      fmt_off = off + 8;
      fmt_sz = cksz;
    }

    if (memcmp(ck, "data", 4) == 0) {
      data_off = off + 8;
      data_sz = cksz;
    }

    off += 8 + cksz + (cksz & 1);
  }

  if (!fmt_off || fmt_sz < 16 || !data_off || !data_sz) {
    if (err_out) {
      *err_out = g_strdup("WAV missing fmt/data chunk.");
    }

    g_free(buf);
    return FALSE;
  }

  const uint8_t *fmt = buf + fmt_off;
  uint16_t audio_format = rd_u16_le(fmt + 0);
  uint16_t num_channels = rd_u16_le(fmt + 2);
  uint32_t sample_rate = rd_u32_le(fmt + 4);
  uint16_t bits_per_sample = rd_u16_le(fmt + 14);

  if (audio_format != 1) {
    if (err_out) {
      *err_out = g_strdup("WAV must be PCM (format 1).");
    }

    g_free(buf);
    return FALSE;
  }

  if (num_channels != 1) {
    if (err_out) {
      *err_out = g_strdup("WAV must be MONO.");
    }

    g_free(buf);
    return FALSE;
  }

  if (bits_per_sample != 16) {
    if (err_out) {
      *err_out = g_strdup("WAV must be 16-bit PCM.");
    }

    g_free(buf);
    return FALSE;
  }

  if (sample_rate != 48000) {
    if (err_out) {
      *err_out = g_strdup("WAV must be 48000 Hz.");
    }

    g_free(buf);
    return FALSE;
  }

  uint32_t samples = data_sz / sizeof(int16_t);

  if ((int)samples <= 0) {
    if (err_out) {
      *err_out = g_strdup("WAV has no audio samples.");
    }

    g_free(buf);
    return FALSE;
  }

  // Do not resize capture_max here. Operator can set capture time in the existing UI.
  if ((int)samples > capture_max) {
    if (err_out) {
      *err_out = g_strdup("WAV is longer than current capture buffer. Increase CAPTURE time.");
    }

    g_free(buf);
    return FALSE;
  }

  if (capture_data == NULL) {
    capture_data = g_new(double, capture_max);
  }

  const guint8 *p = buf + data_off;

  for (uint32_t i = 0; i < samples; i++) {
    int16_t s;
    memcpy(&s, p, sizeof(s));
    capture_data[i] = (double)s / 32768.0;
    p += sizeof(s);
  }

  capture_record_pointer = (int)samples;
  capture_replay_pointer = 0;
  capture_state = CAP_AVAIL;
  g_free(buf);
  return TRUE;
}

static const char *vk_basename_no_ext(const char *path) {
  char *base = g_path_get_basename(path);
  static char buf[256];
  g_strlcpy(buf, base, sizeof(buf));
  g_free(base);
  char *dot = g_strrstr(buf, ".wav");

  if (dot) {
    *dot = '\0';
  }

  return buf;
}

static void vk_set_play_button_label_from_path(GtkWidget *btn, const char *path) {
  if (!btn) { return; }

  if (!path || !path[0]) {
    gtk_button_set_label(GTK_BUTTON(btn), "Play");
    return;
  }

  char buf[256];
  char *base = g_path_get_basename(path);
  g_strlcpy(buf, base, sizeof(buf));
  g_free(base);
  char *dot = g_strrstr(buf, ".wav");

  if (dot) { *dot = '\0'; }

  gtk_button_set_label(GTK_BUTTON(btn), buf);
}

/* Update UI according to playback lock / active slot */
static void vk_update_slot_ui(void) {
  for (int i = 0; i < VK_SLOTS; i++) {
    if (!vk_btn_play[i]) {
      continue;
    }

    if (!vk_play_lock) {
      /* idle state */
      gtk_widget_set_sensitive(vk_btn_play[i],
                               vk_paths[i][0] != 0);
      vk_set_play_button_label_from_path(
        vk_btn_play[i],
        vk_paths[i][0] ? vk_paths[i] : NULL
      );
    } else {
      if (i == vk_active_slot) {
        gtk_widget_set_sensitive(vk_btn_play[i], TRUE);
        gtk_button_set_label(GTK_BUTTON(vk_btn_play[i]), "▶ PLAYING");
      } else {
        gtk_widget_set_sensitive(vk_btn_play[i], FALSE);
      }
    }
  }
}

static void on_load_clicked(GtkButton *btn, gpointer user_data) {
  int slot = GPOINTER_TO_INT(user_data);
  GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(btn)));
  GtkWidget *dlg = gtk_file_chooser_dialog_new("Load WAV (48k/mono/PCM16)",
                   parent,
                   GTK_FILE_CHOOSER_ACTION_OPEN,
                   "_Cancel", GTK_RESPONSE_CANCEL,
                   "_Open", GTK_RESPONSE_ACCEPT,
                   NULL);
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "WAV files (*.wav)");
  gtk_file_filter_add_pattern(filter, "*.wav");
  gtk_file_filter_add_pattern(filter, "*.WAV");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filter);

  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));

    if (path) {
      /* Lazy-load: store path only, load WAV on Play */
      g_strlcpy(vk_paths[slot], path, sizeof(vk_paths[slot]));
      voicekeyerSaveState();

      if (vk_label_file[slot]) {
        gtk_label_set_text(GTK_LABEL(vk_label_file[slot]), vk_paths[slot]);
      }

      if (vk_btn_play[slot]) {
        gtk_widget_set_sensitive(vk_btn_play[slot], TRUE);
        vk_set_play_button_label_from_path(vk_btn_play[slot], vk_paths[slot]);
      }

      set_status("Path set.");
      g_free(path);
    }
  }

  gtk_widget_destroy(dlg);
}


// State machine for deterministic MOX handling (avoid race with radio_is_transmitting()
// and ensure radio_end_xmit_captured_data() has run before unkeying MOX).
typedef enum {
  VK_WATCH_NONE = 0,
  VK_WATCH_WAIT_TX_ON,
  VK_WATCH_WAIT_PLAYBACK_END
} vk_watch_mode_t;

static vk_watch_mode_t vk_watch_mode = VK_WATCH_NONE;

static void vk_watch_stop(void) {
  if (vk_mox_watch_id != 0) {
    g_source_remove(vk_mox_watch_id);
    vk_mox_watch_id = 0;
  }

  vk_watch_mode = VK_WATCH_NONE;
}

static void vk_trigger_tx_playback(void) {
  // Preconditions are checked by caller. This uses the existing CAPTURE state machine.
  capture_replay_pointer = 0;
  capture_state = CAP_AVAIL;
  is_vk  = 1;
  is_cap = 0;
  schedule_action(VK_PLAYBACK, PRESSED, 0);
}

static gboolean vk_mox_watch_cb(gpointer data) {
  (void)data;

  switch (vk_watch_mode) {
  case VK_WATCH_WAIT_TX_ON:

    // Wait until TX is actually on, then trigger the CAPTURE TX-playback path.
    if (radio_is_transmitting()) {
      vk_trigger_tx_playback();
      vk_watch_mode = VK_WATCH_WAIT_PLAYBACK_END;
      set_status("TX started.");
    }

    return G_SOURCE_CONTINUE;

  case VK_WATCH_WAIT_PLAYBACK_END:

    // Playback ended when capture leaves CAP_XMIT. CAP_XMIT_DONE is already "ended enough".
    if (capture_state != CAP_XMIT) {
      vk_play_lock = 0;
      vk_active_slot = -1;
      vk_update_slot_ui();

      if (vk_keyed_mox) {
        radio_set_mox(0);
        vk_keyed_mox = 0;
      }

      vk_watch_stop();
      set_status("TX ended.");
      return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;

  default:
    vk_watch_stop();
    vk_play_lock = 0;
    vk_active_slot = -1;
    vk_update_slot_ui();
    return G_SOURCE_REMOVE;
  }
}

static void on_play_clicked(GtkButton *btn, gpointer user_data) {
  int slot = GPOINTER_TO_INT(user_data);
  GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(btn)));

  /* Hard lock: ignore Play while VK playback is active */
  if (vk_play_lock) {
    return;
  }

  /* Prevent re-trigger during active playback (would cancel watchdog / lose MOX ownership) */
  if (vk_watch_mode == VK_WATCH_WAIT_PLAYBACK_END ||
      capture_state == CAP_XMIT ||
      capture_state == CAP_XMIT_DONE) {
    return;
  }

  {
    char *err = NULL;

    if (!load_wav_pcm16_mono_48k_into_capture(vk_paths[slot], &err)) {
      error_dialog(parent, err ? err : "WAV load failed.");
      g_free(err);
      vk_paths[slot][0] = 0;
      voicekeyerSaveState();

      if (vk_label_file[slot]) {
        gtk_label_set_text(GTK_LABEL(vk_label_file[slot]), "(none)");
      }

      if (vk_btn_play[slot]) {
        gtk_widget_set_sensitive(vk_btn_play[slot], FALSE);
        vk_set_play_button_label_from_path(vk_btn_play[slot], NULL);
      }

      set_status("Load failed (slot cleared).");
      return;
    }
  }

  if (!can_transmit) {
    error_dialog(parent, "Transmit not available (can_transmit=0).");
    return;
  }

  // Stop any previous watchdog
  vk_watch_stop();
  // Acquire lock for selected slot
  vk_play_lock = 1;
  vk_active_slot = slot;
  vk_update_slot_ui();
  // Ensure TX is on (CAPTURE plays back via TX only if radio_is_transmitting() is true).
  vk_keyed_mox = 0;

  if (!radio_is_transmitting()) {
    radio_set_mox(1);
    vk_keyed_mox = 1;
    // MOX enabling may not be instantaneous; avoid racing into the RX-path (CAP_RECORDING).
    vk_watch_mode = VK_WATCH_WAIT_TX_ON;
    vk_mox_watch_id = g_timeout_add(20, vk_mox_watch_cb, NULL);
    set_status("Enabling TX (MOX)...");
    return;
  }

  // TX already on -> trigger immediately
  vk_trigger_tx_playback();
  // Always watch for playback end (unkeys MOX only if we own it).
  vk_watch_mode = VK_WATCH_WAIT_PLAYBACK_END;
  vk_mox_watch_id = g_timeout_add(20, vk_mox_watch_cb, NULL);
}

static void on_stop_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  (void)user_data;

  // If we are still waiting for TX-on, cancel and roll back MOX.
  if (vk_watch_mode == VK_WATCH_WAIT_TX_ON) {
    vk_watch_stop();

    if (vk_keyed_mox) {
      radio_set_mox(0);
      vk_keyed_mox = 0;
    }

    vk_play_lock = 0;
    vk_active_slot = -1;
    vk_update_slot_ui();
    set_status("Stopped.");
    return;
  }

  // Request stop via existing CAPTURE state machine stop path (restores Mic gain etc.).
  if (capture_state == CAP_XMIT || capture_state == CAP_XMIT_DONE) {
    is_vk  = 1;
    is_cap = 0;
    schedule_action(VK_PLAYBACK, PRESSED, 0);
  }

  // Do NOT unkey MOX immediately here (would race with radio_end_xmit_captured_data()).
  // Instead, watch until playback actually ends and then unkey if we keyed MOX.
  if (vk_keyed_mox) {
    vk_watch_stop();
    vk_watch_mode = VK_WATCH_WAIT_PLAYBACK_END;
    vk_mox_watch_id = g_timeout_add(20, vk_mox_watch_cb, NULL);
    set_status("Stopping TX...");
  } else {
    vk_play_lock = 0;
    vk_active_slot = -1;
    set_status("Stopped.");
  }
}

void voice_keyer_show(void) {
  if (vk_window != NULL) {
    gtk_window_present(GTK_WINDOW(vk_window));
    return;
  }

  voicekeyerRestoreState();
  vk_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_transient_for(GTK_WINDOW(vk_window), GTK_WINDOW(top_window));
  win_set_bgcolor(vk_window, &mwin_bgcolor);
  char win_title[32];
  snprintf(win_title, sizeof(win_title), "%s - Voice Keyer", PGNAME);
  gtk_window_set_title(GTK_WINDOW(vk_window), win_title);
  gtk_window_set_default_size(GTK_WINDOW(vk_window), 520, 160);
  gtk_container_set_border_width(GTK_CONTAINER(vk_window), 10);
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_add(GTK_CONTAINER(vk_window), vbox);
  GtkWidget *row_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(vbox), row_top, FALSE, FALSE, 0);
  GtkWidget *close_btn = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_btn, "close_button");
  g_signal_connect(close_btn, "clicked", G_CALLBACK(close_cb), NULL);
  gtk_box_pack_start(GTK_BOX(row_top), close_btn, FALSE, FALSE, 0);
  GtkWidget *btn_stop = gtk_button_new_with_label("Stop XMIT");
  gtk_widget_set_name(btn_stop, "close_button");
  gtk_widget_set_tooltip_text(btn_stop, "Stop XMIT playback");
  gtk_box_pack_start(GTK_BOX(row_top), btn_stop, FALSE, FALSE, 0);

  for (int i = 0; i < VK_SLOTS; i++) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), row, FALSE, FALSE, 0);
    vk_btn_load[i] = gtk_button_new_with_label("Load WAV");
    gtk_widget_set_tooltip_text(vk_btn_load[i], "Load WAV file (required format 48kHz/mono/PCM16)");
    gtk_box_pack_start(GTK_BOX(row), vk_btn_load[i], FALSE, FALSE, 0);
    vk_btn_play[i] = gtk_button_new_with_label("Play");
    gtk_widget_set_sensitive(vk_btn_play[i], vk_paths[i][0] != 0);
    gtk_box_pack_start(GTK_BOX(row), vk_btn_play[i], FALSE, FALSE, 0);
    vk_label_file[i] = gtk_label_new(vk_paths[i][0] ? vk_paths[i] : "(none)");
    gtk_label_set_xalign(GTK_LABEL(vk_label_file[i]), 0.0f);
    gtk_box_pack_start(GTK_BOX(row), vk_label_file[i], TRUE, TRUE, 0);

    if (vk_paths[i][0]) {
      gtk_button_set_label(GTK_BUTTON(vk_btn_play[i]),
                           vk_basename_no_ext(vk_paths[i]));
    }

    g_signal_connect(vk_btn_load[i], "clicked",
                     G_CALLBACK(on_load_clicked), GINT_TO_POINTER(i));
    g_signal_connect(vk_btn_play[i], "clicked",
                     G_CALLBACK(on_play_clicked), GINT_TO_POINTER(i));
  }

  GtkWidget *row3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(vbox), row3, FALSE, FALSE, 0);
  GtkWidget *lbl_status = gtk_label_new("Status:");
  gtk_box_pack_start(GTK_BOX(row3), lbl_status, FALSE, FALSE, 0);
  vk_label_status = gtk_label_new("Load 48kHz/mono/PCM16 WAV.");
  gtk_label_set_xalign(GTK_LABEL(vk_label_status), 0.0f);
  gtk_box_pack_start(GTK_BOX(row3), vk_label_status, TRUE, TRUE, 0);
  g_signal_connect(btn_stop, "clicked", G_CALLBACK(on_stop_clicked), vk_window);
  g_signal_connect(vk_window, "destroy", G_CALLBACK(gtk_widget_destroyed), &vk_window);
  vk_update_slot_ui();
  gtk_widget_show_all(vk_window);
}

