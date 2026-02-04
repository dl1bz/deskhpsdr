/* Copyright (C)
*   2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
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
#include <glib/gstdio.h>
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
// static GtkWidget *vk_label_file[VK_SLOTS] = { NULL };
static GtkWidget *vk_label_status = NULL;
static GtkWidget *vk_btn_play[VK_SLOTS] = { NULL };
static GtkWidget *vk_btn_replay[VK_SLOTS] = { NULL };
static GtkWidget *vk_btn_load[VK_SLOTS] = { NULL };

static char vk_paths[VK_SLOTS][1024] = {{0}};

// Voice Keyer MOX ownership + watchdog (internal)
static int vk_keyed_mox = 0;
static guint vk_mox_watch_id = 0;

// Voice Keyer playback lock (internal)
// Future-proof for VK_SLOTS > 1: only one playback at a time.
static int vk_play_lock = 0;
static int vk_replay_lock = 0;
static guint vk_replay_watch_id = 0;
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

static void close_cb(GtkButton *button, gpointer user_data) {
  (void)button;
  (void)user_data;

  if (vk_window == NULL) {
    return;
  }

  /* MUSS IMMER: State sichern, egal ob wir schließen dürfen */
  voicekeyerSaveState();

  /* Wenn Playback läuft: nicht schließen */
  if (vk_active_slot >= 0 || vk_play_lock) {
    GtkWidget *dlg = gtk_message_dialog_new(
                       GTK_WINDOW(vk_window),
                       GTK_DIALOG_MODAL,
                       GTK_MESSAGE_WARNING,
                       GTK_BUTTONS_OK,
                       "Voice Keyer is still running.\n"
                       "Please stop playback before closing the window.");
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    return;
  }

  /* Jetzt wirklich schließen */
  gtk_widget_destroy(vk_window);
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
  /* DC blocker state (reset per import) */
  double x1 = 0.0, y1 = 0.0;
  const double R = 0.999;  /* 48 kHz: sanfter DC-Block */

  for (uint32_t i = 0; i < samples; i++) {
    gint16 s_le;
    memcpy(&s_le, p, sizeof(s_le));
    const gint16 s = GINT16_FROM_LE(s_le);
    double x = (double)s / 32768.0 * 0.9;
    /* DC blocker: y[n] = x[n] - x[n-1] + R*y[n-1] */
    double y = x - x1 + R * y1;
    x1 = x;
    y1 = y;

    /* safety clamp */
    if (y > 1.0) { y = 1.0; }

    if (y < -1.0) { y = -1.0; }

    capture_data[i] = y;
    p += sizeof(s_le);
  }

  capture_record_pointer = (int)samples;
  capture_replay_pointer = 0;
  capture_state = CAP_AVAIL;
  g_free(buf);
  return TRUE;
}

__attribute__((unused)) static const char *vk_basename_no_ext(const char *path) {
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
      /* During local replay: disable TX playback buttons (XMIT) */
      if (vk_replay_lock) {
        gtk_widget_set_sensitive(vk_btn_play[i], FALSE);
      } else {
        gtk_widget_set_sensitive(vk_btn_play[i],
                                 vk_paths[i][0] != 0);
      }

      vk_set_play_button_label_from_path(
        vk_btn_play[i],
        vk_paths[i][0] ? vk_paths[i] : NULL
      );
      /* Tooltip: voller Pfad oder Hinweis */
      gtk_widget_set_tooltip_text(
        vk_btn_play[i],
        vk_paths[i][0] ? vk_paths[i] : "No file assigned"
      );

      if (vk_btn_replay[i]) {
        const int replay_active = vk_replay_lock;

        if (replay_active) {
          /* During replay: only active slot can stop */
          gtk_widget_set_sensitive(vk_btn_replay[i],
                                   i == vk_active_slot);

          if (i == vk_active_slot) {
            gtk_button_set_label(GTK_BUTTON(vk_btn_replay[i]), "▶ REPLAYING");
          } else {
            gtk_button_set_label(GTK_BUTTON(vk_btn_replay[i]), "Listen");
          }
        } else {
          gtk_widget_set_sensitive(vk_btn_replay[i],
                                   vk_paths[i][0] != 0);
          gtk_button_set_label(GTK_BUTTON(vk_btn_replay[i]), "Listen");
        }

        gtk_widget_set_tooltip_text(
          vk_btn_replay[i],
          vk_paths[i][0] ? vk_paths[i] : "No file assigned"
        );
      }
    } else {
      if (i == vk_active_slot) {
        gtk_widget_set_sensitive(vk_btn_play[i], TRUE);
        gtk_button_set_label(GTK_BUTTON(vk_btn_play[i]), "▶ PLAYING");
        /* Tooltip auch während PLAYING aktuell halten */
        gtk_widget_set_tooltip_text(
          vk_btn_play[i],
          vk_paths[i][0] ? vk_paths[i] : "No file assigned"
        );
      } else {
        gtk_widget_set_sensitive(vk_btn_play[i], FALSE);
      }

      /* Disable local replay while VK TX playback is active */
      if (vk_btn_replay[i]) {
        gtk_widget_set_sensitive(vk_btn_replay[i], FALSE);
      }
    }
  }
}

#ifdef __APPLE__
static void on_load_clicked(GtkButton *btn, gpointer user_data) {
  int slot = GPOINTER_TO_INT(user_data);

  if (slot < 0 || slot >= VK_SLOTS) {
    set_status("Invalid slot.");
    return;
  }

  GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(btn));
  GtkWindow *parent = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
  GtkFileChooserNative *dlg = gtk_file_chooser_native_new(
                                "Load WAV (48k/mono/PCM16)",
                                parent,
                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                "_Open",
                                "_Cancel"
                              );
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "WAV files (*.wav)");
  gtk_file_filter_add_pattern(filter, "*.wav");
  gtk_file_filter_add_pattern(filter, "*.WAV");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filter);

  if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));

    if (path) {
      if (g_access(path, R_OK) != 0) {
        set_status("File not readable.");
        g_free(path);
        goto out;
      }

      /* Lazy-load: store path only, load WAV on Play */
      g_strlcpy(vk_paths[slot], path, sizeof(vk_paths[slot]));
      voicekeyerSaveState();

      if (vk_btn_play[slot]) {
        gtk_widget_set_sensitive(vk_btn_play[slot], TRUE);
        vk_set_play_button_label_from_path(vk_btn_play[slot], vk_paths[slot]);
      }

      set_status("Path set.");
      g_free(path);
    }
  }

out:
  // UI komplett aktualisieren (Label/Sensitivity/Tooltip)
  vk_update_slot_ui();
  g_object_unref(dlg);
}

#else
static void on_load_clicked(GtkButton *btn, gpointer user_data) {
  int slot = GPOINTER_TO_INT(user_data);

  if (slot < 0 || slot >= VK_SLOTS) {
    set_status("Invalid slot.");
    return;
  }

  GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(btn));
  GtkWindow *parent = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
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
      if (g_access(path, R_OK) != 0) {
        set_status("File not readable.");
        g_free(path);
        goto out;
      }

      /* Lazy-load: store path only, load WAV on Play */
      g_strlcpy(vk_paths[slot], path, sizeof(vk_paths[slot]));
      voicekeyerSaveState();

      // if (vk_label_file[slot]) {
      // gtk_label_set_text(GTK_LABEL(vk_label_file[slot]), vk_paths[slot]);
      // }

      if (vk_btn_play[slot]) {
        gtk_widget_set_sensitive(vk_btn_play[slot], TRUE);
        vk_set_play_button_label_from_path(vk_btn_play[slot], vk_paths[slot]);
      }

      set_status("Path set.");
      g_free(path);
    }
  }

out:
  // UI komplett aktualisieren (Label/Sensitivity/Tooltip)
  vk_update_slot_ui();
  gtk_widget_destroy(dlg);
}
#endif

// State machine for deterministic MOX handling (avoid race with radio_is_transmitting()
// and ensure radio_end_xmit_captured_data() has run before unkeying MOX).
typedef enum {
  VK_WATCH_NONE = 0,
  VK_WATCH_WAIT_TX_ON,
  VK_WATCH_WAIT_PLAYBACK_END
} vk_watch_mode_t;

static vk_watch_mode_t vk_watch_mode = VK_WATCH_NONE;
// Latch: becomes 1 after we have actually entered CAP_XMIT (avoids race vs schedule_action)
static int vk_seen_xmit = 0;

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
  vk_seen_xmit = 0;
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

    // Avoid race: after vk_trigger_tx_playback() we are still CAP_AVAIL until VK_PLAYBACK is processed.
    // Only consider playback "ended" after we have actually seen CAP_XMIT at least once.
    if (!vk_seen_xmit) {
      if (capture_state == CAP_XMIT) {
        vk_seen_xmit = 1;
      }

      return G_SOURCE_CONTINUE;
    }

    // Playback ended when capture leaves CAP_XMIT. CAP_XMIT_DONE is already "ended enough".
    if (capture_state != CAP_XMIT) {
      vk_play_lock = 0;
      vk_active_slot = -1;
      vk_update_slot_ui();

      if (vk_keyed_mox) {
        radio_set_mox(0);
        vk_keyed_mox = 0;
      }

      // VK finished -> re-enable VOX logic again
      is_vk = 0;

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

  /*
   * Policy A: Block Voice Keyer playback while TUNE is active.
   * Reason: TRX is transmitting in TUNE state; mixing VK playback into that state is undefined / unwanted.
   */
  if (tune) {
    error_dialog(parent, "Playback blocked: TUNE is active.");
    return;
  }

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

      // if (vk_label_file[slot]) {
      // gtk_label_set_text(GTK_LABEL(vk_label_file[slot]), "(none)");
      // }

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

  if (!radio_get_mox()) {
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

static gboolean vk_replay_watch_cb(gpointer data) {
  (void)data;

  /* Keep watching until replay ends */
  if (capture_state == CAP_REPLAY || capture_state == CAP_REPLAY_DONE) {
    return TRUE;
  }

  vk_replay_lock = 0;
  vk_replay_watch_id = 0;
  vk_active_slot = -1;
  set_status("VK Replay: finished");
  vk_update_slot_ui();
  return FALSE;
}

static void on_replay_clicked(GtkButton *btn, gpointer user_data) {
  int slot = GPOINTER_TO_INT(user_data);
  GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(btn)));

  /* Do not interfere with active VK TX playback watchdog / MOX ownership */
  if (vk_play_lock) {
    return;
  }

  /* Toggle stop if already replaying */
  if (capture_state == CAP_REPLAY || capture_state == CAP_REPLAY_DONE) {
    schedule_action(REPLAY, PRESSED, 0);
    vk_replay_lock = 0;
    set_status("VK Replay: stopped");

    if (vk_replay_watch_id) {
      g_source_remove(vk_replay_watch_id);
      vk_replay_watch_id = 0;
    }

    vk_active_slot = -1;
    vk_update_slot_ui();
    return;
  }

  {
    char *err = NULL;

    if (!load_wav_pcm16_mono_48k_into_capture(vk_paths[slot], &err)) {
      error_dialog(parent, err ? err : "WAV load failed.");
      g_free(err);
      vk_paths[slot][0] = 0;
      voicekeyerSaveState();

      if (vk_btn_play[slot]) {
        gtk_widget_set_sensitive(vk_btn_play[slot], FALSE);
        vk_set_play_button_label_from_path(vk_btn_play[slot], NULL);
      }

      if (vk_btn_replay[slot]) {
        gtk_widget_set_sensitive(vk_btn_replay[slot], FALSE);
        gtk_button_set_label(GTK_BUTTON(vk_btn_replay[slot]), "Listen");
      }

      set_status("Load failed (slot cleared).");
      vk_update_slot_ui();
      return;
    }
  }

  /* Start local replay via existing REPLAY state machine */
  vk_active_slot = slot;
  vk_replay_lock = 1;
  {
    char msg[64];
    snprintf(msg, sizeof(msg), "VK Replay: slot %d", slot + 1);
    set_status(msg);
  }

  if (!vk_replay_watch_id) {
    vk_replay_watch_id = g_timeout_add(50, vk_replay_watch_cb, NULL);
  }

  capture_replay_pointer = 0;
  capture_state = CAP_AVAIL;
  schedule_action(REPLAY, PRESSED, 0);
  vk_update_slot_ui();
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
    vk_update_slot_ui();
    set_status("Stopped.");
  }
}



static gboolean vk_key_press_cb(GtkWidget *w, GdkEventKey *ev, gpointer user_data) {
  (void)w;
  (void)user_data;

  if (ev->keyval == GDK_KEY_space) {
    schedule_action(MOX, PRESSED, 1);
    return TRUE;   // block default GTK button activation
  }

  return FALSE;
}

static gboolean vk_key_release_cb(GtkWidget *w, GdkEventKey *ev, gpointer user_data) {
  (void)w;
  (void)user_data;

  if (ev->keyval == GDK_KEY_space) {
    schedule_action(MOX, RELEASED, 0);
    return TRUE;
  }

  return FALSE;
}

void voice_keyer_play_slot(int slot) {
  if (!vk_window) { return; }                 // VK-Fenster nicht offen

  if (slot < 0 || slot >= VK_SLOTS) { return; }

  if (!vk_btn_play[slot]) { return; }         // UI noch nicht gebaut

  if (!gtk_widget_get_sensitive(vk_btn_play[slot])) { return; } // kein File / gelockt

  on_play_clicked(GTK_BUTTON(vk_btn_play[slot]), GINT_TO_POINTER(slot));
}

void voice_keyer_stop(void) {
  if (!vk_window) { return; }

  on_stop_clicked(NULL, NULL);
}

/* Stop VK playback because an external PTT wants to take over (mic). */
void voice_keyer_stop_for_ptt_takeover(void) {
  /* Wenn VK nicht aktiv ist: nichts zu tun */
  if (!vk_play_lock &&
      vk_watch_mode != VK_WATCH_WAIT_TX_ON &&
      capture_state != CAP_XMIT &&
      capture_state != CAP_XMIT_DONE) {
    return;
  }

  /*
   * Entscheidend: VK darf MOX jetzt NICHT mehr "besitzen",
   * sonst würde der Watchdog ggf. wieder unkeyen.
   */
  vk_keyed_mox = 0;

  /* Wenn VK noch auf TX-on wartet: Watchdog stoppen + Lock freigeben */
  if (vk_watch_mode == VK_WATCH_WAIT_TX_ON) {
    vk_watch_stop();
    vk_play_lock = 0;
    vk_active_slot = -1;
    vk_update_slot_ui();
    set_status("VK stopped (PTT takeover).");
    return;
  }

  /* Wenn bereits XMIT Playback läuft: sauber über die Capture-State-Maschine stoppen */
  if (capture_state == CAP_XMIT || capture_state == CAP_XMIT_DONE) {
    is_vk  = 1;
    is_cap = 0;
    schedule_action(VK_PLAYBACK, PRESSED, 0);
  }

  /* Lock/UI zurücksetzen – MOX bleibt an (weil vk_keyed_mox=0) */
  vk_watch_stop();
  vk_play_lock = 0;
  vk_active_slot = -1;
  vk_update_slot_ui();
  set_status("VK stopped (PTT takeover).");
}

int voice_keyer_is_open(void) {
  return vk_window != NULL;
}

static gboolean on_vk_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
  (void)event;
  (void)user_data;

  if (vk_active_slot >= 0 || vk_play_lock) {
    GtkWidget *dlg = gtk_message_dialog_new(
                       GTK_WINDOW(widget),
                       GTK_DIALOG_MODAL,
                       GTK_MESSAGE_WARNING,
                       GTK_BUTTONS_OK,
                       "Voice Keyer is still running.\n"
                       "Please stop playback before closing the window.");
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    return TRUE;
  }

  voicekeyerSaveState();  // Save on real close via WM
  return FALSE;
}

static void use_tx_audiochain_btn_cb(GtkToggleButton *cbtn, gpointer user_data) {
  int *flag = (int *)user_data;
  *flag = gtk_toggle_button_get_active(cbtn) ? 1 : 0;
  t_print("%s use_tx_audiochain = %d\n", __func__, use_tx_audiochain);
}

void voice_keyer_show(void) {
  if (vk_window != NULL) {
    gtk_window_present(GTK_WINDOW(vk_window));
    return;
  }

  voicekeyerRestoreState();
  vk_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_widget_add_events(vk_window, GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
  g_signal_connect(vk_window, "key-press-event", G_CALLBACK(vk_key_press_cb), NULL);
  g_signal_connect(vk_window, "key-release-event", G_CALLBACK(vk_key_release_cb), NULL);
  g_signal_connect(vk_window, "delete-event", G_CALLBACK(on_vk_delete_event), NULL);
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
  GtkWidget *tx_audiochain_btn = gtk_check_button_new_with_label("XMIT through WDSP TX audiochain");
  gtk_widget_set_tooltip_text(tx_audiochain_btn, "If ENABLED, audio from file goes through the whole\n"
                                                 "WDSP TX audio chain like TX-EQ, CFC, Limiter, Speech Processor\n"
                                                 "except the Mic Gain ▶ always set to 0.0db if playback.\n\n"
                                                 "If DISABLED, the WDSP TX audio chain is set to BYPASS\n"
                                                 "and the audio file will be sent flat without any editing.");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tx_audiochain_btn), use_tx_audiochain);
  g_signal_connect(tx_audiochain_btn, "toggled", G_CALLBACK(use_tx_audiochain_btn_cb), &use_tx_audiochain);
  gtk_box_pack_start(GTK_BOX(row_top), tx_audiochain_btn, FALSE, FALSE, 0);

  for (int i = 0; i < VK_SLOTS; i++) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), row, FALSE, FALSE, 0);
    vk_btn_load[i] = gtk_button_new_with_label("Load WAV");
    gtk_widget_set_tooltip_text(vk_btn_load[i], "Load WAV file (required format 48kHz/mono/PCM16)\n\n"
                                                "The max. playback length is ever equal with the\n"
                                                "setting in Radio Menu <Audio Capture Time>, so first adjust\n"
                                                "this value in Radio Menu. The upper limit is MAX 120s.");
    gtk_box_pack_start(GTK_BOX(row), vk_btn_load[i], FALSE, FALSE, 0);
    vk_btn_play[i] = gtk_button_new_with_label("Play");
    gtk_widget_set_sensitive(vk_btn_play[i], vk_paths[i][0] != 0);
    gtk_box_pack_start(GTK_BOX(row), vk_btn_play[i], TRUE, TRUE, 0);
    /*
    vk_label_file[i] = gtk_label_new(vk_paths[i][0] ? vk_paths[i] : "(none)");
    gtk_label_set_xalign(GTK_LABEL(vk_label_file[i]), 0.0f);
    gtk_box_pack_start(GTK_BOX(row), vk_label_file[i], TRUE, TRUE, 0);

    if (vk_paths[i][0]) {
      gtk_button_set_label(GTK_BUTTON(vk_btn_play[i]),
                           vk_basename_no_ext(vk_paths[i]));
    }
    */
    vk_btn_replay[i] = gtk_button_new_with_label("Listen");
    gtk_widget_set_sensitive(vk_btn_replay[i], vk_paths[i][0] != 0);
    gtk_widget_set_size_request(vk_btn_replay[i], 150, -1);
    gtk_box_pack_start(GTK_BOX(row), vk_btn_replay[i], FALSE, FALSE, 0);
    g_signal_connect(vk_btn_load[i], "clicked",
                     G_CALLBACK(on_load_clicked), GINT_TO_POINTER(i));
    g_signal_connect(vk_btn_play[i], "clicked",
                     G_CALLBACK(on_play_clicked), GINT_TO_POINTER(i));
    g_signal_connect(vk_btn_replay[i], "clicked",
                     G_CALLBACK(on_replay_clicked), GINT_TO_POINTER(i));
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

