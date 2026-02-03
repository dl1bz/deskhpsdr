/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
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

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "new_menu.h"
#include "radio.h"
#include "sliders.h"
#include "transmitter.h"
#include "ext.h"
#include "filter.h"
#include "mode.h"
#include "vfo.h"
#include "new_protocol.h"
#include "message.h"
#include "property.h"
#include "equalizer_menu.h"
#include "toolset.h"
#include "startup.h"

static GtkWidget *dialog = NULL;
static GtkWidget *input;
static GtkWidget *tx_spin_low;
static GtkWidget *tx_spin_high;

static GtkWidget *tx_container;
static GtkWidget *cfc_container;
static GtkWidget *dexp_container;
static GtkWidget *peaks_container;

static GtkWidget *load_button;
static GtkWidget *audio_profile;
static GtkWidget *save_button;
//
// Some symbolic constants used in callbacks
//

enum _containers {
  TX_CONTAINER = 1,
  CFC_CONTAINER,
  DEXP_CONTAINER,
  PEAKS_CONTAINER
};
static int which_container = TX_CONTAINER;

#define TX         0
#define DEXP     256
#define CFC      512
#define CFCFREQ  768
#define CFCLVL  1024
#define CFCPOST 1280

enum _tx_choices {
  TX_FPS = TX,
  TX_LINEIN,
  TX_COMP,
  TX_FILTER_LOW,
  TX_FILTER_HIGH,
  TX_PAN_LOW,
  TX_PAN_HIGH,
  TX_PAN_STEP,
  TX_AM_CARRIER,
  TX_TUNE_DRIVE,
  TX_DIGI_DRIVE,
  TX_LEVELER_GAIN,
  TX_LEVELER_DECAY,
  TX_LEVELER_ENABLE,
  TX_PHROT_ENABLE,
  TX_PHROT_STAGE,
  TX_PHROT_FREQ,
  TX_CESSB_ENABLE,
  TX_CTFMODE,
  TX_ADDGAIN_ENABLE,
  TX_ADDGAIN_GAIN,
  TX_SWR_ALARM,
  TX_DISPLAY_FILLED,
  TX_COMP_ENABLE,
  TX_CTCSS_ENABLE,
  TX_TUNE_USE_DRIVE,
  TX_SWR_PROTECTION,
  TX_USE_RX_FILTER,
  TX_LOCAL_MIC,
  TX_FM_EMP
};

enum _dexp_choices {
  DEXP_ONOFF = DEXP,
  DEXP_FILTER,
  DEXP_FILTER_LOW,
  DEXP_FILTER_HIGH,
  DEXP_ATTACK,
  DEXP_RELEASE,
  DEXP_HOLD,
  DEXP_TAU,
  DEXP_HYST,
  DEXP_TRIGGER,
  DEXP_EXP
};

enum _cfc_choices {
  CFC_ONOFF = CFC,
  CFC_EQ
};

static gboolean block_delete(GtkWidget *w, GdkEvent *e, gpointer data) {
  return TRUE;
}

static void on_errdlg_response(GtkDialog *dlg, gint response_id, gpointer user_data) {
  if (response_id == GTK_RESPONSE_CLOSE) {
    gtk_widget_destroy(GTK_WIDGET(dlg));
  }
}

static void gui_show_error_dialog(const char *msg) {
  GtkWidget *dlg = gtk_message_dialog_new(
                     NULL,
                     GTK_DIALOG_MODAL,
                     GTK_MESSAGE_ERROR,
                     GTK_BUTTONS_NONE,
                     "%s", msg);
  gtk_window_set_title(GTK_WINDOW(dlg), "deskHPSDR - Important notice");
  // gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);
  gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER_ALWAYS);
  gtk_window_set_keep_above(GTK_WINDOW(dlg), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);
  gtk_dialog_add_button(GTK_DIALOG(dlg), "_Continue", GTK_RESPONSE_CLOSE);
  g_signal_connect(dlg, "delete-event", G_CALLBACK(block_delete), NULL);
  g_signal_connect(dlg, "response", G_CALLBACK(on_errdlg_response), NULL);
  gtk_widget_show_all(dlg);
  gtk_window_present(GTK_WINDOW(dlg));
}

static int check_file(int p) {
  char DateiName[64];
  snprintf(DateiName, 64, "audio_profile_%d.prop", p);
  FILE *file = fopen(DateiName, "r");  // Versucht, die Datei im Lesemodus zu öffnen

  if (file) {
    t_print("%s: Access file %s successful.\n", __func__, DateiName);
    fclose(file); // Datei wurde erfolgreich geöffnet, also existiert sie
    return 1;     // Datei existiert
  } else {
    t_print("%s: Access file %s failed.\n", __func__, DateiName);
    return 0;     // Datei existiert nicht
  }
}

void audioSaveProfile(const char *filename) {
  if (!filename || !*filename) {
    return;
  }

  clearProperties();
  // save only for LSB
  int i = modeLSB;
  SetPropS0("PGNAME",                              PGNAME);
#if defined (__CPYMODE__)
  SetPropS1("modeset.%d.microphone_name", i,       mode_settings[i].microphone_name);
#endif
  SetPropI1("modeset.%d.en_txeq", i,               mode_settings[i].en_txeq);
  SetPropI1("modeset.%d.en_rxeq", i,               mode_settings[i].en_rxeq);
  SetPropI1("modeset.%d.compressor", i,            mode_settings[i].compressor);
  SetPropF1("modeset.%d.compressor_level", i,      mode_settings[i].compressor_level);
  SetPropI1("modeset.%d.lev_enable", i,            mode_settings[i].lev_enable);
  SetPropF1("modeset.%d.lev_gain", i,              mode_settings[i].lev_gain);
  SetPropI1("modeset.%d.phrot_enable", i,          mode_settings[i].phrot_enable);
  SetPropI1("modeset.%d.cfc", i,                   mode_settings[i].cfc);
  SetPropI1("modeset.%d.cfc_eq", i,                mode_settings[i].cfc_eq);

  for (int j = 0; j < 11; j++) {
    SetPropF2("modeset.%d.txeq.%d", i, j,          mode_settings[i].tx_eq_gain[j]);
    SetPropF2("modeset.%d.txeqfrq.%d", i, j,       mode_settings[i].tx_eq_freq[j]);
    SetPropF2("modeset.%d.rxeq.%d", i, j,          mode_settings[i].rx_eq_gain[j]);
    SetPropF2("modeset.%d.rxeqfrq.%d", i, j,       mode_settings[i].rx_eq_freq[j]);
    SetPropF2("modeset.%d.cfc_frq.%d", i, j,       mode_settings[i].cfc_freq[j]);
    SetPropF2("modeset.%d.cfc_lvl.%d", i, j,       mode_settings[i].cfc_lvl[j]);
    SetPropF2("modeset.%d.cfc_post.%d", i, j,      mode_settings[i].cfc_post[j]);
  }

#if defined (__EQ12__)

  for (int jj = 11; jj < 13; jj++) {
    SetPropF2("modeset.%d.txeq.%d", i, jj,         mode_settings[i].tx_eq_gain[jj]);
    SetPropF2("modeset.%d.txeqfrq.%d", i, jj,      mode_settings[i].tx_eq_freq[jj]);
    SetPropF2("modeset.%d.rxeq.%d", i, jj,         mode_settings[i].rx_eq_gain[jj]);
    SetPropF2("modeset.%d.rxeqfrq.%d", i, jj,      mode_settings[i].rx_eq_freq[jj]);
    SetPropF2("modeset.%d.cfc_frq.%d", i, jj,      mode_settings[i].cfc_freq[jj]);
    SetPropF2("modeset.%d.cfc_lvl.%d", i, jj,      mode_settings[i].cfc_lvl[jj]);
    SetPropF2("modeset.%d.cfc_post.%d", i, jj,     mode_settings[i].cfc_post[jj]);
  }

#endif
  SetPropI0("transmitter.addgain_enable",          transmitter->addgain_enable);
  SetPropF0("transmitter.addgain_gain",            transmitter->addgain_gain);
  SetPropI0("transmitter.tx_filter_high",          tx_filter_high);
  SetPropI0("transmitter.tx_filter_low",           tx_filter_low);
  SetPropI0("transmitter.eq_ctfmode",              transmitter->eq_ctfmode);
  saveProperties(filename);
}

static void audioLoadProfile(const char *filename) {
  int i = modeLSB;
  char pg_name[16];

  // snprintf(DateiName, 64, "audio_profile_%d.prop", mic_prof.nr);

  if (!filename || access(filename, F_OK) != 0) {
    return;
  }

  t_print("%s: file=%s mode=%d\n", __func__, filename, i);
  loadProperties(filename);
  GetPropS0("PGNAME",                              pg_name);

  if (strcmp(pg_name, PGNAME) != 0) {
    t_print("%s: Load file %s failed, not deskHPSDR format\n", __func__, filename);
    gui_show_error_dialog("ERROR: Not an deskHPSDR audio profile");
    return;
  }

  GetPropI1("modeset.%d.en_txeq", i,               mode_settings[i].en_txeq);
  GetPropI1("modeset.%d.en_rxeq", i,               mode_settings[i].en_rxeq);
  GetPropI1("modeset.%d.compressor", i,            mode_settings[i].compressor);
  GetPropF1("modeset.%d.compressor_level", i,      mode_settings[i].compressor_level);
  GetPropI1("modeset.%d.lev_enable", i,            mode_settings[i].lev_enable);
  GetPropF1("modeset.%d.lev_gain", i,              mode_settings[i].lev_gain);
  GetPropI1("modeset.%d.phrot_enable", i,          mode_settings[i].phrot_enable);
  GetPropI1("modeset.%d.cfc", i,                   mode_settings[i].cfc);
  GetPropI1("modeset.%d.cfc_eq", i,                mode_settings[i].cfc_eq);

  for (int j = 0; j < 11; j++) {
    GetPropF2("modeset.%d.txeq.%d", i, j,          mode_settings[i].tx_eq_gain[j]);
    GetPropF2("modeset.%d.txeqfrq.%d", i, j,       mode_settings[i].tx_eq_freq[j]);
    GetPropF2("modeset.%d.rxeq.%d", i, j,          mode_settings[i].rx_eq_gain[j]);
    GetPropF2("modeset.%d.rxeqfrq.%d", i, j,       mode_settings[i].rx_eq_freq[j]);
    GetPropF2("modeset.%d.cfc_frq.%d", i, j,       mode_settings[i].cfc_freq[j]);
    GetPropF2("modeset.%d.cfc_lvl.%d", i, j,       mode_settings[i].cfc_lvl[j]);
    GetPropF2("modeset.%d.cfc_post.%d", i, j,      mode_settings[i].cfc_post[j]);
  }

#if defined (__EQ12__)

  for (int jj = 11; jj < 13; jj++) {
    GetPropF2("modeset.%d.txeq.%d", i, jj,         mode_settings[i].tx_eq_gain[jj]);
    GetPropF2("modeset.%d.txeqfrq.%d", i, jj,      mode_settings[i].tx_eq_freq[jj]);
    GetPropF2("modeset.%d.rxeq.%d", i, jj,         mode_settings[i].rx_eq_gain[jj]);
    GetPropF2("modeset.%d.rxeqfrq.%d", i, jj,      mode_settings[i].rx_eq_freq[jj]);
    GetPropF2("modeset.%d.cfc_frq.%d", i, jj,      mode_settings[i].cfc_freq[jj]);
    GetPropF2("modeset.%d.cfc_lvl.%d", i, jj,      mode_settings[i].cfc_lvl[jj]);
    GetPropF2("modeset.%d.cfc_post.%d", i, jj,     mode_settings[i].cfc_post[jj]);
  }

#endif
  GetPropI0("transmitter.addgain_enable",          transmitter->addgain_enable);
  GetPropF0("transmitter.addgain_gain",            transmitter->addgain_gain);
  transmitter->eq_enable        = mode_settings[i].en_txeq;
  transmitter->compressor       = mode_settings[i].compressor;
  transmitter->compressor_level = mode_settings[i].compressor_level;
  transmitter->lev_enable       = mode_settings[i].lev_enable;
  transmitter->lev_gain         = mode_settings[i].lev_gain;
  transmitter->phrot_enable     = mode_settings[i].phrot_enable;
  transmitter->cfc              = mode_settings[i].cfc;
  transmitter->cfc_eq           = mode_settings[i].cfc_eq;

  for (int j = 0; j < 11; j++) {
    transmitter->eq_gain[j]     = mode_settings[i].tx_eq_gain[j];
    transmitter->eq_freq[j]     = mode_settings[i].tx_eq_freq[j];
    transmitter->cfc_freq[j]    = mode_settings[i].cfc_freq[j];
    transmitter->cfc_lvl[j]     = mode_settings[i].cfc_lvl[j];
    transmitter->cfc_post[j]    = mode_settings[i].cfc_post[j];
  }

#if defined (__EQ12__)

  for (int jj = 11; jj < 13; jj++) {
    transmitter->eq_gain[jj]    = mode_settings[i].tx_eq_gain[jj];
    transmitter->eq_freq[jj]    = mode_settings[i].tx_eq_freq[jj];
    transmitter->cfc_freq[jj]   = mode_settings[i].cfc_freq[jj];
    transmitter->cfc_lvl[jj]    = mode_settings[i].cfc_lvl[jj];
    transmitter->cfc_post[jj]   = mode_settings[i].cfc_post[jj];
  }

#endif
  GetPropI0("transmitter.tx_filter_high",          tx_filter_high);
  GetPropI0("transmitter.tx_filter_low",           tx_filter_low);
  GetPropI0("transmitter.eq_ctfmode",              transmitter->eq_ctfmode);
  t_print("%s: Load file %s\n", __func__, filename);
  copy_mode_settings(i);
  tx_set_filter(transmitter);
  tx_set_compressor(transmitter);
  tx_set_dexp(transmitter);
  // rx_set_agc(rx);
  // update_noise();
  update_eq();
  tx_set_eq_ctfmode(transmitter);
  g_idle_add(ext_vfo_update, NULL);
}

// erwartet: void audioSaveProfile(const char *filename);
// erwartet: char workdir[];  (oder kompatibel)

static char *ensure_prop_extension(const char *path) {
  if (!path) { return NULL; }

  // Falls schon .prop
  if (g_str_has_suffix(path, ".prop")) {
    return g_strdup(path);
  }

  // Sonst .prop anhängen
  return g_strconcat(path, ".prop", NULL);
}

static void save_native_response_cb(GtkNativeDialog *ndlg, gint response_id, gpointer user_data) {
  if (response_id == GTK_RESPONSE_ACCEPT) {
    char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(ndlg));

    if (filename && *filename) {
      char *fixed = ensure_prop_extension(filename);

      if (fixed) {
        audioSaveProfile(fixed);
        g_free(fixed);
      }
    }

    g_free(filename);
  }

  g_object_unref(ndlg);
}

static void audio_profile_save_cb(GtkWidget *widget, gpointer user_data) {
  GtkFileChooserNative *native;
  native = gtk_file_chooser_native_new(
             "Export deskHPSDR Audio Profile",
             NULL,                           // bewusst ohne Parent
             GTK_FILE_CHOOSER_ACTION_SAVE,
             "_Save",
             "_Cancel");

  // Start im workdir
  if (*workdir) {
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(native), workdir);
  }

  // Default-Dateiname vorschlagen
  {
    char defname[64];
    snprintf(defname, sizeof(defname), "audio_profile_%d", mic_prof.nr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native), defname);
  }
  // Optional: Filter
  {
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Audio Profile (*.prop)");
    gtk_file_filter_add_pattern(filter, "*.prop");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);
  }
  // Optional: Überschreiben bestätigen
  gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native), TRUE);
  // Async: kein run(), kein Block
  g_signal_connect(native, "response", G_CALLBACK(save_native_response_cb), NULL);
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
}

static void load_native_response_cb(GtkNativeDialog *ndlg, gint response_id, gpointer user_data) {
  if (response_id == GTK_RESPONSE_ACCEPT) {
    char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(ndlg));

    if (filename && *filename) {
      audioLoadProfile(filename);
    }

    g_free(filename);
  }

  g_object_unref(ndlg);
}

static void audio_profile_load_cb(GtkWidget *widget, gpointer user_data) {
  GtkFileChooserNative *native;
  native = gtk_file_chooser_native_new(
             "Import deskHPSDR Audio Profile",
             NULL,                           // kein Parent
             GTK_FILE_CHOOSER_ACTION_OPEN,
             "_Open",
             "_Cancel");

  // Start im workdir
  if (*workdir) {
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(native), workdir);
  }

  // Filter *.prop
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "Audio Profile (*.prop)");
  gtk_file_filter_add_pattern(filter, "*.prop");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);
  // Async: kein run(), kein Block
  g_signal_connect(native, "response", G_CALLBACK(load_native_response_cb), NULL);
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
}

static void load_button_clicked_cb(GtkWidget *widget, gpointer data) {
  GtkWidget *load_button = GTK_WIDGET(data);

  if (!check_file(mic_prof.nr)) {
    gtk_widget_set_sensitive(load_button, FALSE);
    return;
  } else {
    gtk_widget_set_sensitive(load_button, TRUE);
  }

  // Force the GUI to update
  gtk_widget_queue_draw(widget);  // Request a redraw of the button (optional but can help)
  // Flush the GDK event queue to force GUI updates
  gdk_flush();  // Ensure that all updates are processed
  int _mode = vfo_get_tx_mode();
  char DateiName[64];
  snprintf(DateiName, sizeof(DateiName), "audio_profile_%d.prop", mic_prof.nr);

  if (_mode < 3) {
    if (!access(DateiName, F_OK)) {
      audioLoadProfile(DateiName);
      start_tx();
      t_print("%s: Mode %d accepted, file %s exist...loaded Mic Profile %d successful.\n", __func__, _mode, DateiName,
              mic_prof.nr);
    } else {
      t_print("%s: Mode %d accepted, but file %s don't exist.\n", __func__, _mode, DateiName);
    }
  } else {
    t_print("%s: Mic Profile %d not loaded, Mode %d not supported.\n", __func__, mic_prof.nr, _mode);
  }
}

static void save_button_clicked_cb(GtkWidget *widget, gpointer data) {
  GtkWidget *load_button = GTK_WIDGET(data);
  int _mode = vfo_get_tx_mode();
#if !defined (__APPLE__)
  int _audioindex = 0;

  if (n_input_devices > 0) {
    for (int i = 0; i < n_input_devices; i++) {
      if (strcmp(transmitter->microphone_name, input_devices[i].name) == 0) {
        _audioindex = i;
      }
    }
  }

#endif

  if (_mode < 3) {
#if !defined (__APPLE__)
    g_strlcpy(mic_prof.desc[mic_prof.nr], input_devices[_audioindex].description, sizeof(mic_prof.desc[mic_prof.nr]));
    t_print("%s: Mic: %s / %s\n", __func__, input_devices[_audioindex].description, mic_prof.desc[mic_prof.nr]);
#else
    g_strlcpy(mic_prof.desc[mic_prof.nr], transmitter->microphone_name, sizeof(mic_prof.desc[mic_prof.nr]));
    t_print("%s: Mic: %s / %s\n", __func__, transmitter->microphone_name, mic_prof.desc[mic_prof.nr]);
#endif
    char DateiName[64];
    snprintf(DateiName, sizeof(DateiName), "audio_profile_%d.prop", mic_prof.nr);
    audioSaveProfile(DateiName);
    start_tx();
    t_print("%s: Mic Profile %d saved, Mode %d supported.\n", __func__, mic_prof.nr, _mode);
    gtk_widget_set_sensitive(load_button, TRUE);
    // Force the GUI to update
    gtk_widget_queue_draw(widget);  // Request a redraw of the button (optional but can help)
    // Flush the GDK event queue to force GUI updates
    gdk_flush();  // Ensure that all updates are processed
  } else {
    t_print("%s: Mic Profile %d not saved, Mode %d not supported.\n", __func__, mic_prof.nr, _mode);
    return;
  }
}

static void audioprofile_changed_cb(GtkWidget *widget, gpointer data) {
  GtkWidget *load_button = GTK_WIDGET(data);
  int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget)); // Rückgabe als Index

  if (i != mic_prof.nr) {
    mic_prof.nr = i;
  }

  if (!check_file(mic_prof.nr)) {
    gtk_widget_set_sensitive(load_button, FALSE);
    return;
  } else {
    gtk_widget_set_sensitive(load_button, TRUE);
  }

  // Force the GUI to update
  gtk_widget_queue_draw(widget);  // Request a redraw of the button (optional but can help)
  // Flush the GDK event queue to force GUI updates
  gdk_flush();  // Ensure that all updates are processed
  // const gchar *selected_label = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget)); // Rückgabe als Label
  // const gchar *selected_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(widget)); // Rückgabe als Id
  g_idle_add(ext_vfo_update, NULL);
}

// Callback für den "Yes"-Button
static void aprof_save_btn_clicked(GtkWidget *widget, gpointer data) {
  // gtk_main_quit();  // Beendet das GTK-Fenster
  gtk_widget_destroy(GTK_WIDGET(data));  // Schließt nur das Fenster, ohne das Programm zu beenden
  // gtk_widget_destroy(aprof_dialog_win);
  char DateiName[64];
  snprintf(DateiName, sizeof(DateiName), "audio_profile_%d.prop", mic_prof.nr);
  audioSaveProfile(DateiName);
}

// Callback für den "No"-Button
static void aprof_nosave_btn_clicked(GtkWidget *widget, gpointer data) {
  // gtk_main_quit();  // Beendet das GTK-Fenster
  gtk_widget_destroy(GTK_WIDGET(data));  // Schließt nur das Fenster, ohne das Programm zu beenden
}

// Funktion, die auf die Enter-Taste reagiert
gboolean aprof_enter_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
  if (event->keyval == GDK_KEY_Return) {
    // Falls Enter gedrückt wurde, den Load-Button klicken
    gtk_button_clicked(GTK_BUTTON(data));  // data ist hier der Load-Button
    return TRUE;
  }

  return FALSE;
}

void showAudioProfileSaveDialog(void) {
  GtkWidget *aprof_dialog_win;
  GtkWidget *grid;
  GtkWidget *label;
  GtkWidget *aprof_save_button;
  GtkWidget *aprof_nosave_button;
  GdkRGBA color_for_dialog;  // Hier deklarierst du die GdkRGBA-Struktur
  char _title[64];
  char _dialog[64];
  // Fenster erstellen
  int window_width = 600;  // Fensterbreite
  int window_height = 200;  // Fensterhöhe
  aprof_dialog_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  snprintf(_title, 64, "%s", PGNAME);
  gtk_window_set_title(GTK_WINDOW(aprof_dialog_win), _title);
  gtk_window_set_default_size(GTK_WINDOW(aprof_dialog_win), window_width, window_height);
  g_signal_connect(aprof_dialog_win, "destroy", G_CALLBACK(aprof_nosave_btn_clicked), aprof_dialog_win);
  g_signal_connect(aprof_dialog_win, "delete_event", G_CALLBACK (aprof_nosave_btn_clicked), aprof_dialog_win);
  // Entferne die Fensterdekorationen (Schließen-Button, etc.)
  gtk_window_set_decorated(GTK_WINDOW(aprof_dialog_win), FALSE);
  // Berechne die mittige Position auf dem Bildschirm
  GdkScreen *screen = gdk_screen_get_default();  // Hole den Standardbildschirm
  int screen_width = gdk_screen_get_width(screen);  // Bildschirmbreite
  int screen_height = gdk_screen_get_height(screen);  // Bildschirmhöhe
  // Berechne die Position, um das Fenster mittig zu setzen
  int x_position = (screen_width - window_width) / 2;
  int y_position = (screen_height - window_height) / 2;
  // Definiere die Position des Fensters auf dem Bildschirm
  gtk_window_move(GTK_WINDOW(aprof_dialog_win), x_position, y_position);
  // Grid-Layout für das Fenster
  grid = gtk_grid_new();
  gtk_container_add(GTK_CONTAINER(aprof_dialog_win), grid);
  // Setze die Ränder des Fensters mit Container-Setup (falls nötig)
  gtk_container_set_border_width(GTK_CONTAINER(aprof_dialog_win), 20);  // 20px Abstand vom Fensterrand
  // Label hinzufügen
  snprintf(_dialog, 64, "Save now your TX-EQ settings to current Mic Profile [%d] ?", mic_prof.nr);
  label = gtk_label_new(_dialog);
  // Pango-Layout für Textgröße anpassen
  PangoFontDescription *font_desc = pango_font_description_from_string("Arial 18"); // Schriftart und Größe
  gtk_widget_override_font(label, font_desc);  // Wendet die Schriftart auf das Label an
  // Definiere die Textfarbe
  gdk_rgba_parse(&color_for_dialog, "blue");
  // Setze die Textfarbe für das Label
  gtk_widget_override_color(label, GTK_STATE_FLAG_NORMAL, &color_for_dialog);
  gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 2, 1); // Label in Zeile 0, Spalte 0 (über 2 Spalten)
  // "Yes"-Button hinzufügen
  aprof_save_button = gtk_button_new_with_label("Yes");
  gtk_widget_override_font(aprof_save_button, font_desc);
  g_signal_connect(aprof_save_button, "clicked", G_CALLBACK(aprof_save_btn_clicked), aprof_dialog_win);
  gtk_grid_attach(GTK_GRID(grid), aprof_save_button, 0, 1, 1, 1); // Button in Zeile 1, Spalte 0
  // "No"-Button hinzufügen
  aprof_nosave_button = gtk_button_new_with_label("No");
  gtk_widget_override_font(aprof_nosave_button, font_desc);
  g_signal_connect(aprof_nosave_button, "clicked", G_CALLBACK(aprof_nosave_btn_clicked), aprof_dialog_win);
  gtk_grid_attach(GTK_GRID(grid), aprof_nosave_button, 1, 1, 1, 1); // Button in Zeile 1, Spalte 1
  // Widgets in den mittleren Bereich des Grids verschieben
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  // Widgets mittig in ihren Zellen ausrichten
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);  // Horizontale Ausrichtung des Textes
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);  // Vertikale Ausrichtung des Textes
  gtk_widget_set_halign(aprof_save_button, GTK_ALIGN_CENTER);  // Horizontale Ausrichtung des Buttons
  gtk_widget_set_valign(aprof_save_button, GTK_ALIGN_CENTER);  // Vertikale Ausrichtung des Buttons
  gtk_widget_set_halign(aprof_nosave_button, GTK_ALIGN_CENTER);  // Horizontale Ausrichtung des Buttons
  gtk_widget_set_valign(aprof_nosave_button, GTK_ALIGN_CENTER);  // Vertikale Ausrichtung des Buttons
  // Pango-Objekt freigeben
  pango_font_description_free(font_desc);
  // Den NO-Button als Standard-Button festlegen (bei Enter)
  gtk_widget_grab_focus(aprof_nosave_button);  // Fokussiere den Button explizit
  // gtk_window_set_default(GTK_WINDOW(aprof_dialog_win), aprof_nosave_button); // FUNKTIONIERT NICHT
  // Manuelles Abfangen der Enter-Taste, um das gleiche Verhalten wie mit gtk_window_set_default() zu erreichen
  g_signal_connect(aprof_dialog_win, "key-press-event", G_CALLBACK(aprof_enter_key_press), aprof_nosave_button);
  // Fenster anzeigen
  gtk_widget_show_all(aprof_dialog_win);
}

static void cleanup(void) {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
    int _mode = vfo_get_tx_mode();

    if (_mode < 3 && can_transmit) {
      //  char fn[64];
      //  snprintf(fn, sizeof(fn), "audio_profile_%d.prop", mic_prof.nr);
      //  audioSaveProfile(fn);
    }
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static void tx_panadapter_peaks_in_passband_filled_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_peaks_in_passband_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void tx_panadapter_hide_noise_filled_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_hide_noise_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void tx_panadapter_peaks_on_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_peaks_on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void tx_panadapter_num_peaks_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_num_peaks = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  printf("New peaks no %d", transmitter->panadapter_num_peaks);
}

static void tx_panadapter_ignore_range_divider_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_ignore_range_divider = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void tx_panadapter_ignore_noise_percentile_value_changed_cb(GtkWidget *widget, gpointer data) {
  transmitter->panadapter_ignore_noise_percentile = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void sel_cb(GtkWidget *widget, gpointer data) {
  //
  // Handle radio button in the top row, this selects
  // which sub-menu is active
  //
  int c = GPOINTER_TO_INT(data);
  GtkWidget *my_container;

  switch (c) {
  case TX_CONTAINER:
    my_container = tx_container;
    break;

  case CFC_CONTAINER:
    my_container = cfc_container;
    break;

  case DEXP_CONTAINER:
    my_container = dexp_container;
    break;

  case PEAKS_CONTAINER:
    my_container = peaks_container;
    break;

  default:
    // We should never come here
    my_container = NULL;
    break;
  }

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(my_container);
    which_container = c;
  } else {
    gtk_widget_hide(my_container);
  }
}

static void spinbtn_cb(GtkWidget *widget, gpointer data) {
  //
  // Handle ALL spin-buttons in this menu
  //
  int mode = vfo_get_tx_mode();
  int c = GPOINTER_TO_INT(data);
  double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  int    vi = (v >= 0.0) ? (int) (v + 0.5) : (int) (v - 0.5);
  int d = c & 0xFF00;   // The command class, TX/CFCFREQ/CFCLVL/CFCPOST/DEXP
  int e = c & 0x00FF;   // channel No. for CRCFREQ/CFCLVL/CFCPOST

  if (d == TX) {
    switch (c) {
    case TX_LINEIN:
      linein_gain = v;
      break;

    case TX_FPS:
      transmitter->fps = vi;
      tx_set_framerate(transmitter);
      break;

    case TX_COMP:
      transmitter->compressor_level = vi;

      if (can_transmit && display_sliders) {
        update_slider_bbcompr_scale(TRUE);
        update_slider_bbcompr_button(TRUE);
      }

      mode_settings[mode].compressor_level = vi;
      copy_mode_settings(mode);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_FILTER_LOW:
      tx_filter_low = vi;
      tx_set_filter(transmitter);
      break;

    case TX_FILTER_HIGH:
      tx_filter_high = vi;
      tx_set_filter(transmitter);
      break;

    case TX_PAN_LOW:
      transmitter->panadapter_low = vi;
      break;

    case TX_PAN_HIGH:
      transmitter->panadapter_high = vi;
      break;

    case TX_PAN_STEP:
      transmitter->panadapter_step = vi;
      break;

    case TX_AM_CARRIER:
      transmitter->am_carrier_level = v;
      tx_set_am_carrier_level(transmitter);
      break;

    case TX_TUNE_DRIVE:
      transmitter->tune_drive = vi;

      if (can_transmit && display_sliders) {
        if (transmitter->tune_use_drive) {
          transmitter->tune_use_drive = 0;
        }

        update_slider_tune_drive_scale(TRUE);
      }

      break;

    case TX_DIGI_DRIVE:
      drive_digi_max = v;

      if ((mode == modeDIGL || mode == modeDIGU) && transmitter->drive > v + 0.5) {
        set_drive(v);
      }

      break;

    case TX_LEVELER_GAIN:
      transmitter->lev_gain = v;

      if (can_transmit && display_sliders) {
        update_slider_lev_scale(TRUE);
        update_slider_lev_button(TRUE);
      }

      mode_settings[mode].lev_gain = v;
      copy_mode_settings(mode);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_LEVELER_DECAY:
      transmitter->lev_decay = (int) v;
      // mode_settings[mode].lev_decay = (int) v;
      // copy_mode_settings(mode);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_PHROT_STAGE:
      transmitter->phrot_stage = (int) v;
      // mode_settings[mode].phrot_stage = (int) v;
      // copy_mode_settings(mode);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_PHROT_FREQ:
      transmitter->phrot_freq = v;
      // mode_settings[mode].phrot_freq = v;
      // copy_mode_settings(mode);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_ADDGAIN_GAIN:
      transmitter->addgain_gain = v;
      tx_set_mic_gain(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      update_slider_preamp_button(TRUE);
      break;

    case TX_SWR_ALARM:
      transmitter->swr_alarm = v;
      break;
    }
  } else if (d == CFCFREQ) {
    // The CFC frequency spin buttons
    transmitter->cfc_freq[e] = v;
    mode_settings[mode].cfc_freq[e] = v;
    copy_mode_settings(mode);
    tx_set_compressor(transmitter);
  } else if (d == CFCLVL) {
    // The CFC compression-level spin buttons
    transmitter->cfc_lvl[e] = v;
    mode_settings[mode].cfc_lvl[e] = v;
    copy_mode_settings(mode);
    tx_set_compressor(transmitter);
  } else if (d == CFCPOST) {
    // The CFC Post-equalizer gain spin buttons
    transmitter->cfc_post[e] = v;
    mode_settings[mode].cfc_post[e] = v;
    copy_mode_settings(mode);
    tx_set_compressor(transmitter);
  } else if (d == DEXP) {
    // The DEXP spin buttons. Note that the spin buttons for the
    // "time" values are in milli-seconds
    switch (c) {
    case DEXP_TAU:
      transmitter->dexp_tau = 0.001 * v;
      mode_settings[mode].dexp_tau = 0.001 * v;
      copy_mode_settings(mode);
      break;

    case DEXP_ATTACK:
      transmitter->dexp_attack = 0.001 * v;
      mode_settings[mode].dexp_attack = 0.001 * v;
      copy_mode_settings(mode);
      break;

    case DEXP_RELEASE:
      transmitter->dexp_release = 0.001 * v;
      mode_settings[mode].dexp_release = 0.001 * v;
      copy_mode_settings(mode);
      break;

    case DEXP_HOLD:
      transmitter->dexp_hold = 0.001 * v;
      mode_settings[mode].dexp_hold = 0.001 * v;
      copy_mode_settings(mode);
      break;

    case DEXP_HYST:
      transmitter->dexp_hyst = v;
      mode_settings[mode].dexp_hyst = v;
      copy_mode_settings(mode);
      break;

    case DEXP_TRIGGER:
      transmitter->dexp_trigger = vi;
      mode_settings[mode].dexp_trigger = vi;
      copy_mode_settings(mode);
      break;

    case DEXP_FILTER_LOW:
      transmitter->dexp_filter_low = vi;
      mode_settings[mode].dexp_filter_low = vi;
      copy_mode_settings(mode);
      break;

    case DEXP_FILTER_HIGH:
      transmitter->dexp_filter_high = vi;
      mode_settings[mode].dexp_filter_high = vi;
      copy_mode_settings(mode);
      break;

    case DEXP_EXP:
      // Note this is in dB
      transmitter->dexp_exp = vi;
      mode_settings[mode].dexp_exp = vi;
      copy_mode_settings(mode);
      break;
    }

    tx_set_dexp(transmitter);
  }
}

static void chkbtn_cb(GtkWidget *widget, gpointer data) {
  //
  // Handle ALL the check buttons in this menu
  //
  int mode = vfo_get_tx_mode();
  int c = GPOINTER_TO_INT(data);
  int v = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  int d = c & 0xFF00;

  if (d == TX) {
    // TX menu check buttons
    switch (c) {
    case TX_DISPLAY_FILLED:
      transmitter->display_filled = v;
      break;

    case TX_COMP_ENABLE:
      transmitter->compressor = v;
      mode_settings[mode].compressor = transmitter->compressor;
      copy_mode_settings(mode);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_LEVELER_ENABLE:
      transmitter->lev_enable = v;
      mode_settings[mode].lev_enable = transmitter->lev_enable;
      copy_mode_settings(mode);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_PHROT_ENABLE:
      transmitter->phrot_enable = v;
      mode_settings[mode].phrot_enable = transmitter->phrot_enable;
      copy_mode_settings(mode);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_CESSB_ENABLE:
      transmitter->cessb_enable = v;

      // mode_settings[mode].cessb_enable = transmitter->cessb_enable;
      // copy_mode_settings(mode);
      if (transmitter->low_latency) {
        transmitter->low_latency = 0;
        tx_set_latency(transmitter);
      }

      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_CTFMODE:
      transmitter->eq_ctfmode = v;
      tx_set_eq_ctfmode(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_ADDGAIN_ENABLE:
      transmitter->addgain_enable = (int) v;
      tx_set_mic_gain(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_CTCSS_ENABLE:
      transmitter->ctcss_enabled = v;
      tx_set_ctcss(transmitter);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_TUNE_USE_DRIVE:
      transmitter->tune_use_drive = v;
      update_slider_tune_drive_scale(TRUE);
      break;

    case TX_SWR_PROTECTION:
      transmitter->swr_protection = v;
      break;

    case TX_USE_RX_FILTER:
      transmitter->use_rx_filter = v;
      tx_set_filter(transmitter);

      if (v) {
        gtk_widget_set_sensitive (tx_spin_low, FALSE);
        gtk_widget_set_sensitive (tx_spin_high, FALSE);
      } else {
        gtk_widget_set_sensitive (tx_spin_low, TRUE);
        gtk_widget_set_sensitive (tx_spin_high, TRUE);
      }

#if defined (__CPYMODE__)
      mode_settings[mode].use_rx_filter = v;
      copy_mode_settings(mode);
#endif
      // vfo_update();
      g_idle_add(ext_vfo_update, NULL);
      break;

    case TX_LOCAL_MIC:
      if (v) {
        if (audio_open_input() == 0) {
          transmitter->local_microphone = 1;
        } else {
          transmitter->local_microphone = 0;
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);
        }
      } else {
        if (transmitter->local_microphone) {
          transmitter->local_microphone = 0;
          audio_close_input();
        }
      }

#if defined (__CPYMODE__)

      if (transmitter->local_microphone) {
        mode_settings[mode].local_microphone = 1;
      } else {
        mode_settings[mode].local_microphone = 0;
      }

      t_print("DL1BZ: mode: %d transmitter->local_microphone: %d mode_settings[%d].local_microphone %d\n",
              mode, transmitter->local_microphone, mode, mode_settings[mode].local_microphone);
      copy_mode_settings(mode);
      g_idle_add(ext_vfo_update, NULL);
#endif
      update_slider_local_mic_button();
      break;

    case TX_FM_EMP:
      transmitter->pre_emphasize = !v;
      tx_set_pre_emphasize(transmitter);
      break;
    }
  } else if (d == CFC) {
    // CFC menu check buttons
    switch (c) {
    case CFC_ONOFF:
      transmitter->cfc = v;
      mode_settings[mode].cfc = v;
      copy_mode_settings(mode);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case CFC_EQ:
      transmitter->cfc_eq = v;
      mode_settings[mode].cfc_eq = v;
      copy_mode_settings(mode);
      g_idle_add(ext_vfo_update, NULL);
      break;
    }

    tx_set_compressor(transmitter);
  } else if (d == DEXP) {
    // DEXP menu check buttons
    switch (c) {
    case DEXP_ONOFF:
      transmitter->dexp = v;
      mode_settings[mode].dexp = v;
      copy_mode_settings(mode);
      g_idle_add(ext_vfo_update, NULL);
      break;

    case DEXP_FILTER:
      transmitter->dexp_filter = v;
      mode_settings[mode].dexp_filter = v;
      copy_mode_settings(mode);
      break;
    }

    tx_set_dexp(transmitter);
  }
}

//
// For the combo-boxes we do not "fuse" the callbacks
//
static void mic_in_cb(GtkWidget *widget, gpointer data) {
  int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  switch (i) {
  case 0: // Mic In, but no boost
    mic_boost = 0;
    mic_linein = 0;
    break;

  case 1: // Mic In with boost
    mic_boost = 1;
    mic_linein = 0;
    break;

  case 2: // Line in
    mic_boost = 0;
    mic_linein = 1;
    break;
  }

  schedule_transmit_specific();
}

static void ctcss_frequency_cb(GtkWidget *widget, gpointer data) {
  transmitter->ctcss = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  tx_set_ctcss(transmitter);
  g_idle_add(ext_vfo_update, NULL);
}

void local_input_changed_cb(GtkWidget *widget, gpointer data) {
  int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  gboolean flag = GPOINTER_TO_INT(data);
  t_print("%s: Flag: %d\n", __func__, flag);
#if defined (__CPYMODE__)
  int _mode = vfo_get_tx_mode();
#endif
  t_print("local_input_changed_cb: %d %s\n", i, input_devices[i].name);

  if (transmitter->local_microphone) {
    audio_close_input();
  }

  g_mutex_lock(&copy_string_mutex);
  g_strlcpy(transmitter->microphone_name, input_devices[i].name, sizeof(transmitter->microphone_name));
#if defined (__CPYMODE__)
  g_strlcpy(mode_settings[_mode].microphone_name, input_devices[i].name, sizeof(mode_settings[_mode].microphone_name));
#endif
  g_mutex_unlock(&copy_string_mutex);

  if (transmitter->local_microphone) {
    if (audio_open_input() < 0) {
      transmitter->local_microphone = 0;
#if defined (__CPYMODE__)
      mode_settings[_mode].local_microphone = 0;
#endif
    }
  }

#if defined (__CPYMODE__)
  t_print("%s: mode: %d, mode_settings %s size: %d\n", __func__, _mode, mode_settings[_mode].microphone_name,
          sizeof(mode_settings[_mode].microphone_name));
  copy_mode_settings(_mode);
#endif

  if (n_input_devices > 0) {
    if (flag) {
      update_slider_local_mic_input(i);
    }

    update_slider_local_mic_button();
  }
}

void tx_menu(GtkWidget *parent) {
  char temp[32];
  GtkWidget *btn;
  GtkWidget *mbtn;  // main button for radio buttons
  GtkWidget *label;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[64];
  snprintf(_title, 64, "%s TX Menu (Mic profile:%d)", PGNAME, mic_prof.nr);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 0);
  gtk_grid_set_column_homogeneous (GTK_GRID(grid), TRUE);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  gtk_container_add(GTK_CONTAINER(content), grid);
  int row = 0;
  int col = 0;
  btn = gtk_button_new_with_label("Close");
  gtk_widget_set_name(btn, "close_button");
  g_signal_connect (btn, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  //
  // Must init the containers here since setting the buttons emits
  // a signal leading to show/hide commands
  //
  tx_container = gtk_fixed_new();
  cfc_container = gtk_fixed_new();
  dexp_container = gtk_fixed_new();
  peaks_container = gtk_fixed_new();
  col++;
  mbtn = gtk_radio_button_new_with_label_from_widget(NULL, "TX Basic Settings");
  gtk_widget_set_name(mbtn, "smalllabel_blue_bold");
  gtk_widget_set_tooltip_text(mbtn, "Adjust general TX settings");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mbtn), (which_container == TX_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), mbtn, col, row, 1, 1);
  g_signal_connect(mbtn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(TX_CONTAINER));
  col++;
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "TX ProAudio Tools");
  gtk_widget_set_name(btn, "smalllabel_blue_bold");
  gtk_widget_set_tooltip_text(btn, "Adjust CFC, Leveler, Phase Rotator & Speech Processor\n"
                                   "Enable/Disable CESSB function");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), (which_container == CFC_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(CFC_CONTAINER));
  col++;
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "DEXP Adjustment");
  gtk_widget_set_name(btn, "smalllabel_bold");
  gtk_widget_set_tooltip_text(btn, "Adjust Downward Expander");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), (which_container == DEXP_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(DEXP_CONTAINER));
  col++;
  btn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mbtn), "Peak Labels");
  gtk_widget_set_name(btn, "smalllabel_bold");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), (which_container == PEAKS_CONTAINER));
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(sel_cb), GINT_TO_POINTER(PEAKS_CONTAINER));
  //
  // TX container and controls therein
  //
  gtk_grid_attach(GTK_GRID(grid), tx_container, 0, 1, 5, 1);
  GtkWidget *tx_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(tx_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(tx_grid), 5);
  gtk_container_add(GTK_CONTAINER(tx_container), tx_grid);
  row = 0;

  //------------------------------------------------------------------------------------------------
  if (can_transmit) {
    row++;
    col = 0;
    GtkWidget *trennline = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_size_request(trennline, -1, 3);
    gtk_grid_attach(GTK_GRID(tx_grid), trennline, col, row, 5, 1);
    row++;
    col += 3;
    load_button = gtk_button_new_with_label("Activate");
    gtk_widget_set_name(load_button, "boldlabel_blue");
    gtk_grid_attach(GTK_GRID(tx_grid), load_button, col, row, 1, 1);
    g_signal_connect(load_button, "clicked", G_CALLBACK(load_button_clicked_cb), load_button);

    if (!check_file(mic_prof.nr)) {
      gtk_widget_set_sensitive(load_button, FALSE);
    } else {
      gtk_widget_set_sensitive(load_button, TRUE);
    }

    col++;
    save_button = gtk_button_new_with_label("Save");
    gtk_widget_set_name(save_button, "boldlabel_blue");
    gtk_grid_attach(GTK_GRID(tx_grid), save_button, col, row, 1, 1);
    g_signal_connect(save_button, "clicked", G_CALLBACK(save_button_clicked_cb), save_button);
    col = 0;
    audio_profile = gtk_combo_box_text_new();

    // gtk_grid_set_column_homogeneous (GTK_GRID(tx_grid), TRUE);
    for (int i = 0; i < 3; i++) {
      gchar *id = g_strdup_printf("audio_profile_%d.prop", i);  // Einträge-ID erstellen
      gchar *label = g_strdup_printf("Mic Profile %d (%s)", i, mic_prof.desc[i]);      // Beschriftung erstellen
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(audio_profile), id, label);
      g_free(id);    // Speicher freigeben
      g_free(label); // Speicher freigeben
    }

    if (mic_prof.nr > 0 && mic_prof.nr < 3) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(audio_profile), mic_prof.nr);
    } else {
      mic_prof.nr = 0;
      gtk_combo_box_set_active(GTK_COMBO_BOX(audio_profile), 0);
    }

    my_combo_attach(GTK_GRID(tx_grid), audio_profile, col, row, 3, 1);
    gtk_widget_set_can_focus(audio_profile, TRUE);
    g_signal_connect(audio_profile, "changed", G_CALLBACK(audioprofile_changed_cb), load_button);
    row++;
    col = 0;
    col += 3;
    GtkWidget *loadfile_btn = gtk_button_new_with_label("Import file");
    gtk_widget_set_name(loadfile_btn, "boldlabel_blue");
    gtk_grid_attach(GTK_GRID(tx_grid), loadfile_btn, col, row, 1, 1);
    g_signal_connect(loadfile_btn, "clicked", G_CALLBACK(audio_profile_load_cb),
                     dialog);   // <-- wichtig: TX-Menu-Dialog als Parent
    col++;
    GtkWidget *savefile_btn = gtk_button_new_with_label("Export file");
    gtk_widget_set_name(savefile_btn, "boldlabel_blue");
    gtk_grid_attach(GTK_GRID(tx_grid), savefile_btn, col, row, 1, 1);
    g_signal_connect(savefile_btn, "clicked", G_CALLBACK(audio_profile_save_cb),
                     dialog);
  }

  row++;
  GtkWidget *trennline = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(trennline, -1, 3);
  gtk_grid_attach(GTK_GRID(tx_grid), trennline, 0, row, 5, 1);

  //------------------------------------------------------------------------------------------------
  if (n_input_devices > 0) {
    row++;
    col = 0;
    btn = gtk_check_button_new_with_label("Use computer\naudio input");
    gtk_widget_set_tooltip_text(btn, "Enable computer audio as mic input\n"
                                     "(if your SDR-TRX hasn't an own mic input)");
    gtk_widget_set_halign(btn, GTK_ALIGN_CENTER);
    gtk_widget_set_name(btn, "smallabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->local_microphone);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col++, row, 1, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_LOCAL_MIC));
    input = gtk_combo_box_text_new();
    gtk_widget_set_tooltip_text(input, "Select one of your local audio devices as mic input,\n"
                                       "which is connected to your computer.");

    for (int i = 0; i < n_input_devices; i++) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(input), NULL, input_devices[i].description);

      if (strcmp(transmitter->microphone_name, input_devices[i].name) == 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(input), i);
      }
    }

    // If the combo box shows no device, take the first one
    // AND set the mic.name to that device name.
    // This situation occurs if the local microphone device in the props
    // file is no longer present

    if (gtk_combo_box_get_active(GTK_COMBO_BOX(input))  < 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(input), 0);
      g_strlcpy(transmitter->microphone_name, input_devices[0].name, sizeof(transmitter->microphone_name));
    }

    my_combo_attach(GTK_GRID(tx_grid), input, col, row, 4, 1);
    gtk_widget_set_can_focus(input, TRUE);
    gboolean flag = TRUE;
    g_signal_connect(input, "changed", G_CALLBACK(local_input_changed_cb), GINT_TO_POINTER(flag));
  }

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    row++;
    col = 0;
    label = gtk_label_new("SDR Radio Input");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(tx_grid), label, col++, row, 1, 1);
    //
    // Mic Boost, Mic In, and Line In can the handled mutually exclusive
    //
    btn = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Mic In");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Mic Boost");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Line In");
    int pos = 0;

    if (mic_linein) {
      pos = 2;
    } else if (mic_boost) {
      pos = 1;
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(btn), pos);
    my_combo_attach(GTK_GRID(tx_grid), btn, col++, row, 1, 1);
    g_signal_connect(btn, "changed", G_CALLBACK(mic_in_cb), NULL);

    if (transmitter->local_microphone) {
      gtk_widget_set_sensitive (btn, FALSE);
    }

    col++;
    label = gtk_label_new("SDR LineIn (dB)");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
    col++;
    btn = gtk_spin_button_new_with_range(-34.5, 12.0, 1.5);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(btn), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), linein_gain);
    gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
    g_signal_connect(G_OBJECT(btn), "value_changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_LINEIN));

    if (transmitter->local_microphone) {
      gtk_widget_set_sensitive (btn, FALSE);
    }

    row++;
    GtkWidget *trennline = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_size_request(trennline, -1, 3);
    gtk_grid_attach(GTK_GRID(tx_grid), trennline, 0, row, 5, 1);
  }

  row++;
  col = 0;
  label = gtk_label_new("TX Filter Low");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  tx_spin_low = gtk_spin_button_new_with_range(0.0, 8000.0, 50.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_spin_low), (double)tx_filter_low);
  gtk_grid_attach(GTK_GRID(tx_grid), tx_spin_low, col, row, 1, 1);
  g_signal_connect(tx_spin_low, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_FILTER_LOW));
  col++;
  btn = gtk_check_button_new_with_label("TX uses RX Filter");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->use_rx_filter);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_USE_RX_FILTER));
  col++;
  btn = gtk_check_button_new_with_label("Local Mic PreAmp Gain");
  gtk_widget_set_name(btn, "boldlabel_blue");
  gtk_widget_set_halign(btn, GTK_ALIGN_END);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->addgain_enable);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_ADDGAIN_ENABLE));
  col++;
  btn = gtk_spin_button_new_with_range(1.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->addgain_gain);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col++, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_ADDGAIN_GAIN));
  row++;
  col = 0;
  label = gtk_label_new("TX Filter High");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  tx_spin_high = gtk_spin_button_new_with_range(0.0, 8000.0, 50.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_spin_high), (double)tx_filter_high);
  gtk_grid_attach(GTK_GRID(tx_grid), tx_spin_high, col, row, 1, 1);
  g_signal_connect(tx_spin_high, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_FILTER_HIGH));
  col++;

  if (transmitter->use_rx_filter) {
    gtk_widget_set_sensitive (tx_spin_low, FALSE);
    gtk_widget_set_sensitive (tx_spin_high, FALSE);
  }

  btn = gtk_check_button_new_with_label("Tune use drive");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->tune_use_drive);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_TUNE_USE_DRIVE));
  col++;
  label = gtk_label_new("Tune Drive level");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->tune_drive);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_TUNE_DRIVE));
  row++;
  col = 0;
  label = gtk_label_new("Panadapter High");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_high);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_PAN_HIGH));
  col++;
  btn = gtk_check_button_new_with_label("SWR Protection");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->swr_protection);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_SWR_PROTECTION));
  col++;
  label = gtk_label_new("SWR alarm at");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(1.0, 10.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->swr_alarm);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_SWR_ALARM));
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Low");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(-400.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_low);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_PAN_LOW));
  col++;
  btn = gtk_check_button_new_with_label("CTCSS Enable");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->ctcss_enabled);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_CTCSS_ENABLE));
  col++;
  label = gtk_label_new("CTCSS Frequency");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_combo_box_text_new();

  for (int i = 0; i < CTCSS_FREQUENCIES; i++) {
    snprintf(temp, 32, "%0.1f", ctcss_frequencies[i]);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, temp);
  }

  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), transmitter->ctcss);
  my_combo_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(ctcss_frequency_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Step");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(5.0, 25.0, 5.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->panadapter_step);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_PAN_STEP));
  col++;
  btn = gtk_check_button_new_with_label("FM PreEmp/ALC");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), !transmitter->pre_emphasize);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_FM_EMP));
  col++;
  label = gtk_label_new("AM Carrier Level");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(0.0, 1.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->am_carrier_level);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value_changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_AM_CARRIER));
  row++;
  col = 0;
  label = gtk_label_new("Frames Per Second");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->fps);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_FPS));
  col++;
  btn = gtk_check_button_new_with_label("Fill Panadapter");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->display_filled);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_DISPLAY_FILLED));
  col++;
  label = gtk_label_new("Max Digi Drv");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(tx_grid), label, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(1.0, drive_max, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), drive_digi_max);
  gtk_grid_attach(GTK_GRID(tx_grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_DIGI_DRIVE));
  //
  // CFC container and controls therein
  //
  gtk_grid_attach(GTK_GRID(grid), cfc_container, 0, 1, 5, 1);
  GtkWidget *cfc_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(cfc_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(cfc_grid), 5);
  gtk_container_add(GTK_CONTAINER(cfc_container), cfc_grid);
  row = 0;
  btn = gtk_check_button_new_with_label("Use Pre-CFC");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->cfc);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 0, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(CFC_ONOFF));
  btn = gtk_check_button_new_with_label("Use Post-CFC");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->cfc_eq);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(CFC_EQ));
  btn = gtk_check_button_new_with_label("Phase Rotator");
  gtk_widget_set_name(btn, "boldlabel_blue");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->phrot_enable);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 3, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_PHROT_ENABLE));
  btn = gtk_spin_button_new_with_range(1.0, 15.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->phrot_stage);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 4, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_PHROT_STAGE));
  btn = gtk_spin_button_new_with_range(1.0, 500.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->phrot_freq);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 5, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_PHROT_FREQ));
  row++;
  label = gtk_label_new("Pre Compression:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_lvl[0]);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCLVL));
  btn = gtk_check_button_new_with_label("Leveler");
  gtk_widget_set_name(btn, "boldlabel_blue");
  gtk_widget_set_halign(btn, GTK_ALIGN_START);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->lev_enable);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 3, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_LEVELER_ENABLE));
  btn = gtk_spin_button_new_with_range(0.0, 15.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->lev_gain);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 4, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_LEVELER_GAIN));
  btn = gtk_spin_button_new_with_range(0.0, 500.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->lev_decay);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 5, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_LEVELER_DECAY));
  row++;
  label = gtk_label_new("Post Gain:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(-20.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_post[0]);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCPOST));
  btn = gtk_check_button_new_with_label("Speech Proc");
  gtk_widget_set_tooltip_text(btn, "Speech Processor:\n"
                                   "Type: Baseband Compressor — Optimize your voice transmission\n"
                                   "Requires ENABLE(>1) when using the CESSB function");
  gtk_widget_set_name(btn, "boldlabel_blue");
  gtk_widget_set_halign(btn, GTK_ALIGN_START);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->compressor);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 3, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_COMP_ENABLE));
  btn = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double)transmitter->compressor_level);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 4, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(TX_COMP));
  btn = gtk_check_button_new_with_label("Auto CESSB");
  gtk_widget_set_tooltip_text(btn, "Controlled-Envelope SSB\nWorks only if:\n"
                                   "- Speech Processor is ENABLED\n"
                                   "- TX-DSP is NOT set to Low Latency");
  gtk_widget_set_name(btn, "boldlabel_blue");
  gtk_widget_set_halign(btn, GTK_ALIGN_END);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->cessb_enable);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 5, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_CESSB_ENABLE));
  row++;
  btn = gtk_check_button_new_with_label("TX-EQ Ctfmode");
  gtk_widget_set_tooltip_text(btn, "The cutoff-mode used in the generation\nof the EQ filter can be selected.\n\n"
                                   "If ON:\nNo roll-off outside the specified passband (default is OFF).");
  gtk_widget_set_name(btn, "boldlabel_blue");
  gtk_widget_set_halign(btn, GTK_ALIGN_START);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->eq_ctfmode);
  gtk_grid_attach(GTK_GRID(cfc_grid), btn, 3, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(TX_CTFMODE));
  row++;
  GtkWidget *line = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(line, -1, 3);
  gtk_grid_attach(GTK_GRID(cfc_grid), line, 0, row, 6, 1);
  row++;
  char cfc_label_txt[256];
  snprintf(cfc_label_txt, sizeof(cfc_label_txt),
           "CFC: This is a Multiband Compressor, not an EQ. Adjust and shape your audio with the TX-EQ first.");
  GtkWidget *cfc_label = gtk_label_new(cfc_label_txt);
  gtk_widget_set_name(cfc_label, "smalllabel_blue_bold");
  gtk_grid_attach(GTK_GRID(cfc_grid), cfc_label, 0, row, 6, 1);
  // Frequency, Level, Post-Gain
  row++;
  label = gtk_label_new("Frequency");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 0, row, 1, 1);
  label = gtk_label_new("Frequency");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 3, row, 1, 1);
  label = gtk_label_new("Pre Comp Level");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 1, row, 1, 1);
  label = gtk_label_new("Pre Comp Level");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 4, row, 1, 1);
  label = gtk_label_new("Post Gain");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 2, row, 1, 1);
  label = gtk_label_new("Post Gain");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(cfc_grid), label, 5, row, 1, 1);

  if (can_transmit) {
    sort_cfc(transmitter);
  }

#if defined (__EQ12__)
  const int max_cfc_zeilen = 6;
#else
  const int max_cfc_zeilen = 5;
#endif

  for (int i = 1; i <= max_cfc_zeilen; i++) {
    row++; // neue Zeile
    //------------------------------------------------------------------------------------------------------------------
    btn = gtk_spin_button_new_with_range(10.0, 16000.0, 10.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_freq[i]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 0, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCFREQ + i));
    //------------------------------------------------------------------------------------------------------------------
    btn = gtk_spin_button_new_with_range(10.0, 16000.0, 10.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_freq[i + max_cfc_zeilen]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 3, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCFREQ + i + max_cfc_zeilen));
    //------------------------------------------------------------------------------------------------------------------
    btn = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_lvl[i]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 1, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCLVL + i));
    //------------------------------------------------------------------------------------------------------------------
    btn = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_lvl[i + max_cfc_zeilen]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 4, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCLVL + i + max_cfc_zeilen));
    //------------------------------------------------------------------------------------------------------------------
    btn = gtk_spin_button_new_with_range(-20.0, 20.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_post[i]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 2, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCPOST + i));
    //------------------------------------------------------------------------------------------------------------------
    btn = gtk_spin_button_new_with_range(-20.0, 20.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->cfc_post[i + max_cfc_zeilen]);
    gtk_grid_attach(GTK_GRID(cfc_grid), btn, 5, row, 1, 1);
    g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(CFCPOST + i + max_cfc_zeilen));
    //------------------------------------------------------------------------------------------------------------------
  }

  //
  // DEXP container and controls therein
  //
  gtk_grid_attach(GTK_GRID(grid), dexp_container, 0, 1, 5, 1);
  GtkWidget *dexp_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(dexp_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(dexp_grid), 5);
  gtk_container_add(GTK_CONTAINER(dexp_container), dexp_grid);
  row = 0;
  btn = gtk_check_button_new_with_label("Use DEXP");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->dexp);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 0, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(DEXP_ONOFF));
  btn = gtk_check_button_new_with_label("Use Side Channel Filter");
  gtk_widget_set_name(btn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->dexp_filter);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 2, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(chkbtn_cb), GINT_TO_POINTER(DEXP_FILTER));
  row++;
  label = gtk_label_new("Expansion Factor (dB)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(0.00, 30.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->dexp_exp);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_EXP));
  label = gtk_label_new("Filter Low-Cut (Hz)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 2, row, 1, 1);
  btn = gtk_spin_button_new_with_range(100.00, 10000.0, 10.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->dexp_filter_low);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 3, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_FILTER_LOW));
  row++;
  label = gtk_label_new("Hysteresis Ratio");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(0.05, 0.95, 0.01);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->dexp_hyst);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_HYST));
  label = gtk_label_new("Filter High-Cut (Hz)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 2, row, 1, 1);
  btn = gtk_spin_button_new_with_range(100.00, 10000.0, 10.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->dexp_filter_high);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 3, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_FILTER_HIGH));
  row++;
  label = gtk_label_new("Trigger Level (dB)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(-40.0, -10.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), transmitter->dexp_trigger);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_TRIGGER));
  row++;
  label = gtk_label_new("Trigger Attack tau (ms)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(1.0, 250.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), 1000.0 * transmitter->dexp_tau);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_TAU));
  row++;
  label = gtk_label_new("Trigger Attack Time (ms)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(1.0, 250.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), 1000.0 * transmitter->dexp_attack);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_ATTACK));
  row++;
  label = gtk_label_new("Trigger Release Time (ms)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(1.0, 500.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), 1000.0 * transmitter->dexp_release);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_RELEASE));
  row++;
  label = gtk_label_new("Trigger Hold Time (ms)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(dexp_grid), label, 0, row, 1, 1);
  btn = gtk_spin_button_new_with_range(10.0, 1500.0, 10.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), 1000.0 * transmitter->dexp_hold);
  gtk_grid_attach(GTK_GRID(dexp_grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(spinbtn_cb), GINT_TO_POINTER(DEXP_HOLD));
  //
  // Peaks container and controls therein
  //
  gtk_grid_attach(GTK_GRID(grid), peaks_container, 0, 1, 5, 1);
  GtkWidget *peaks_grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(peaks_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(peaks_grid), 5);
  gtk_container_add(GTK_CONTAINER(peaks_container), peaks_grid);
  col = 0;
  row = 0;
  GtkWidget *b_panadapter_peaks_on = gtk_check_button_new_with_label("Show Peak Numbers on Panadapter");
  gtk_widget_set_name(b_panadapter_peaks_on, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_panadapter_peaks_on), transmitter->panadapter_peaks_on);
  gtk_widget_show(b_panadapter_peaks_on);
  gtk_grid_attach(GTK_GRID(peaks_grid), b_panadapter_peaks_on, col, row, 1, 1);
  g_signal_connect(b_panadapter_peaks_on, "toggled", G_CALLBACK(tx_panadapter_peaks_on_cb), NULL);
  row++;
  GtkWidget *b_pan_peaks_in_passband = gtk_check_button_new_with_label("Show Peaks in Passband Only");
  gtk_widget_set_name(b_pan_peaks_in_passband, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_pan_peaks_in_passband),
                               transmitter->panadapter_peaks_in_passband_filled);
  gtk_widget_show(b_pan_peaks_in_passband);
  gtk_grid_attach(GTK_GRID(peaks_grid), b_pan_peaks_in_passband, col, row, 1, 1);
  g_signal_connect(b_pan_peaks_in_passband, "toggled", G_CALLBACK(tx_panadapter_peaks_in_passband_filled_cb), NULL);
  GtkWidget *b_pan_hide_noise = gtk_check_button_new_with_label("Hide Peaks Below Noise Floor");
  gtk_widget_set_name(b_pan_hide_noise, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_pan_hide_noise), transmitter->panadapter_hide_noise_filled);
  gtk_widget_show(b_pan_hide_noise);
  gtk_grid_attach(GTK_GRID(peaks_grid), b_pan_hide_noise, col, ++row, 1, 1);
  g_signal_connect(b_pan_hide_noise, "toggled", G_CALLBACK(tx_panadapter_hide_noise_filled_cb), NULL);
  label = gtk_label_new("Number of Peaks to label:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(peaks_grid), label, col, ++row, 1, 1);
  col++;
  GtkWidget *panadapter_num_peaks_r = gtk_spin_button_new_with_range(1.0, 10.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_num_peaks_r), (double)transmitter->panadapter_num_peaks);
  gtk_widget_show(panadapter_num_peaks_r);
  gtk_grid_attach(GTK_GRID(peaks_grid), panadapter_num_peaks_r, col, row, 1, 1);
  g_signal_connect(panadapter_num_peaks_r, "value_changed", G_CALLBACK(tx_panadapter_num_peaks_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Ignore Adjacent Peaks:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(peaks_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_ignore_range_divider_r = gtk_spin_button_new_with_range(1.0, 150.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_ignore_range_divider_r),
                            (double)transmitter->panadapter_ignore_range_divider);
  gtk_widget_show(panadapter_ignore_range_divider_r);
  gtk_grid_attach(GTK_GRID(peaks_grid), panadapter_ignore_range_divider_r, col, row, 1, 1);
  g_signal_connect(panadapter_ignore_range_divider_r, "value_changed",
                   G_CALLBACK(tx_panadapter_ignore_range_divider_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Noise Floor Percentile:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(peaks_grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_ignore_noise_percentile_r = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_ignore_noise_percentile_r),
                            (double)transmitter->panadapter_ignore_noise_percentile);
  gtk_widget_show(panadapter_ignore_noise_percentile_r);
  gtk_grid_attach(GTK_GRID(peaks_grid), panadapter_ignore_noise_percentile_r, col, row, 1, 1);
  g_signal_connect(panadapter_ignore_noise_percentile_r, "value_changed",
                   G_CALLBACK(tx_panadapter_ignore_noise_percentile_value_changed_cb), NULL);
  row++;
  sub_menu = dialog;
  gtk_widget_show_all(dialog);

  //
  // Only show one of the TX, CFC, DEXP containers
  // This is the TX container upon first invocation of the TX menu,
  // but subsequent TX menu openings will show the container that
  // was active when leaving this menu before.
  //
  switch (which_container) {
  case TX_CONTAINER:
    gtk_widget_hide(cfc_container);
    gtk_widget_hide(dexp_container);
    gtk_widget_hide(peaks_container);
    break;

  case CFC_CONTAINER:
    gtk_widget_hide(tx_container);
    gtk_widget_hide(dexp_container);
    gtk_widget_hide(peaks_container);
    break;

  case DEXP_CONTAINER:
    gtk_widget_hide(tx_container);
    gtk_widget_hide(cfc_container);
    gtk_widget_hide(peaks_container);
    break;

  case PEAKS_CONTAINER:
    gtk_widget_hide(tx_container);
    gtk_widget_hide(cfc_container);
    gtk_widget_hide(dexp_container);
    break;
  }
}
