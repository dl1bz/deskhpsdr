/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT
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
#include <semaphore.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "new_menu.h"
#include "equalizer_menu.h"
#include "radio.h"
#include "ext.h"
#include "vfo.h"
#include "transmitter.h"
#include "message.h"
#if defined (__LDESK__)
  #include "tx_menu.h"
#endif
static GtkWidget *dialog = NULL;

static GtkWidget *rx1_container;
static GtkWidget *rx2_container;
static GtkWidget *tx_container;

static int eqid = 0;      // 0: RX1, 1: RX2, 2: TX

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
#if defined (__LDESK__)
    int _mode = vfo_get_tx_mode();

    if (_mode < 3 && can_transmit) {
      showAudioProfileSaveDialog();
      // audioSaveProfile();
    }

#endif
  }
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}


void update_eq() {
  if (can_transmit) {
    tx_set_equalizer(transmitter);
  }

  for (int id = 0; id < receivers; id++) {
    rx_set_equalizer(receiver[id]);
  }
}

static void enable_cb (GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      receiver[eqid]->eq_enable = val;
    }

    if (eqid == 0) {
      int mode = vfo[eqid].mode;
      mode_settings[mode].en_rxeq = val;
      copy_mode_settings(mode);
    }

    break;

  case 2:
    if (can_transmit) {
      int mode = vfo[vfo_get_tx_vfo()].mode;
      transmitter->eq_enable = val;
      mode_settings[mode].en_txeq = val;
      copy_mode_settings(mode);
    }

    break;
  }

  update_eq();
  g_idle_add(ext_vfo_update, NULL);
}

static void freq_changed_cb (GtkWidget *widget, gpointer data) {
  int mode;
  int i = GPOINTER_TO_INT(data);
  double val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  switch (eqid) {
  case 0:
  case 1:
    mode = vfo[eqid].mode;
    receiver[eqid]->eq_freq[i] = val;

    if (eqid == 0) {
      mode_settings[mode].rx_eq_freq[i] = val;
      copy_mode_settings(mode);
    }

    break;

  case 2:
    mode = vfo[vfo_get_tx_vfo()].mode;
    transmitter->eq_freq[i] = val;
    mode_settings[mode].tx_eq_freq[i] = val;
    copy_mode_settings(mode);
    break;
  }

  update_eq();
  g_idle_add(ext_vfo_update, NULL);
}


static void gain_changed_cb (GtkWidget *widget, gpointer data) {
  int i = GPOINTER_TO_INT(data);
  double val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  switch (eqid) {
  case 0:
  case 1:
    receiver[eqid]->eq_gain[i] = val;

    if (eqid == 0) {
      int mode = vfo[eqid].mode;
      mode_settings[mode].rx_eq_gain[i] = val;
      copy_mode_settings(mode);
    }

    break;

  case 2:
    if (can_transmit) {
      int mode = vfo[vfo_get_tx_vfo()].mode;
      transmitter->eq_gain[i] = val;
      mode_settings[mode].tx_eq_gain[i] = val;
      copy_mode_settings(mode);
    }

    break;
  }

  update_eq();
  g_idle_add(ext_vfo_update, NULL);
}

static void eqid_changed_cb(GtkWidget *widget, gpointer data) {
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    eqid = GPOINTER_TO_INT(data);
    gtk_widget_hide(rx1_container);
    gtk_widget_hide(rx2_container);
    gtk_widget_hide(tx_container);

    switch (eqid) {
    case 0:
      gtk_widget_show(rx1_container);
      break;

    case 1:
      gtk_widget_show(rx2_container);
      break;

    case 2:
      gtk_widget_show(tx_container);
      break;
    }
  }
}

void equalizer_menu(GtkWidget *parent) {
  GtkWidget *mbtn;
  GtkWidget *label;
  int row = 0;
  int col = 0;

  //
  // Start the menu with the "old" eqid, but if it refers to RX2 and
  // this one is no longer running, set it to RX1
  //
  if ((eqid == 1 && receivers == 1) || (eqid == 2 && !can_transmit)) {
    eqid = 0;
  }

  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
#if defined (__LDESK__)
  char _title[64];
  snprintf(_title, 64, "%s - RX/TX Equalizer (Mic Profile %d)", PGNAME, mic_prof.nr);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
#else
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Equalizer");
#endif
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, col, row, 1, 1);
  col++;
  GtkWidget *rx1_sel = gtk_radio_button_new_with_label_from_widget(NULL, "RX1-EQ Settings");
  gtk_widget_set_name(rx1_sel, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rx1_sel), (eqid == 0));
  gtk_grid_attach(GTK_GRID(grid), rx1_sel, col, row, 1, 1);
  g_signal_connect(rx1_sel, "toggled", G_CALLBACK(eqid_changed_cb), GINT_TO_POINTER(0));

  if (receivers > 1) {
    col++;
    mbtn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rx1_sel), "RX2-EQ Settings");
    gtk_widget_set_name(mbtn, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mbtn), (eqid == 1));
    gtk_grid_attach(GTK_GRID(grid), mbtn, col, row, 1, 1);
    g_signal_connect(mbtn, "toggled", G_CALLBACK(eqid_changed_cb), GINT_TO_POINTER(1));
  }

  if (can_transmit) {
    col++;
    mbtn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rx1_sel), "TX-EQ Settings");
    gtk_widget_set_name(mbtn, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mbtn), (eqid == 2));
    gtk_grid_attach(GTK_GRID(grid), mbtn, col, row, 1, 1);
    g_signal_connect(mbtn, "toggled", G_CALLBACK(eqid_changed_cb), GINT_TO_POINTER(2));
  }

  row++;
  col = 0;
  rx1_container = gtk_fixed_new();
  rx2_container = gtk_fixed_new();
  tx_container = gtk_fixed_new();

  for (int myeq = 0; myeq < 3; myeq++) {
    //
    // Depending on the value of eqid, set spinbuttons, scales, etc.
    // and connect the signals
    //
    GtkWidget *mycontainer;
    const double *freqs;
    const double *gains;
    int     en;
    int     myrow, mycol;

    if (myeq == 1 && receivers < 2)  { continue; }

    if (myeq == 2 && !can_transmit)  { continue; }

    switch (myeq) {
    case 0:
      freqs = receiver[0]->eq_freq;
      gains = receiver[0]->eq_gain;
      en = receiver[0]->eq_enable;
      mycontainer = rx1_container;
      break;

    case 1:
      freqs = receiver[myeq]->eq_freq;
      gains = receiver[myeq]->eq_gain;
      en = receiver[myeq]->eq_enable;
      mycontainer = rx2_container;
      break;

    case 2:
      freqs = transmitter->eq_freq;
      gains = transmitter->eq_gain;
      en = transmitter->eq_enable;
      mycontainer = tx_container;
    }

    gtk_grid_attach(GTK_GRID(grid), mycontainer, col, row, 4, 1);
    myrow = 0;
    mycol = 0;
    GtkWidget *mygrid = gtk_grid_new();
    gtk_grid_set_column_spacing (GTK_GRID(mygrid), 5);
    gtk_grid_set_row_spacing (GTK_GRID(mygrid), 5);
    gtk_container_add(GTK_CONTAINER(mycontainer), mygrid);
    mbtn = gtk_check_button_new_with_label("Enable");
    gtk_widget_set_name(mbtn, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), mbtn, mycol, myrow, 1, 1);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mbtn), en);
    g_signal_connect(mbtn, "toggled", G_CALLBACK(enable_cb), GINT_TO_POINTER(myeq));
    mycol++;
    label = gtk_label_new("Added Frequency-Independent Gain:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), label, mycol, myrow, 2, 1);
    mycol += 2;
    mbtn = gtk_spin_button_new_with_range(-20.0, 20.0, 1.0);
    gtk_grid_attach(GTK_GRID(mygrid), mbtn, mycol, myrow, 1, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(mbtn), gains[0]);
    g_signal_connect(mbtn, "value-changed", G_CALLBACK(gain_changed_cb), GINT_TO_POINTER(0));
    myrow++;
    GtkWidget *line = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_size_request(line, -1, 3);
    gtk_grid_attach(GTK_GRID(mygrid), line, 0, myrow, 4, 1);
    myrow++;
    label = gtk_label_new("Frequency");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), label, 0, myrow, 1, 1);
    label = gtk_label_new("Gain");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), label, 1, myrow, 1, 1);
    label = gtk_label_new("Frequency");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), label, 2, myrow, 1, 1);
    label = gtk_label_new("Gain");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), label, 3, myrow, 1, 1);
#if defined (__EQ12__)
    const int max_eq_zeilen = 6; // if use 12-band EQ 6 rows
#else
    const int max_eq_zeilen = 5; // if use 10-band EQ 5 rows
#endif

    for (int i = 1; i <= max_eq_zeilen; i++) { // index 0 must be excluded
      myrow++;
      mbtn = gtk_spin_button_new_with_range(10.0, 16000.0, 10.0);
      gtk_grid_attach(GTK_GRID(mygrid), mbtn, 0, myrow, 1, 1);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(mbtn), freqs[i]);
      g_signal_connect(mbtn, "value-changed", G_CALLBACK(freq_changed_cb), GINT_TO_POINTER(i));
      mbtn = gtk_spin_button_new_with_range(-20.0, 20.0, 1.0);
      gtk_grid_attach(GTK_GRID(mygrid), mbtn, 1, myrow, 1, 1);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(mbtn), gains[i]);
      g_signal_connect(mbtn, "value-changed", G_CALLBACK(gain_changed_cb), GINT_TO_POINTER(i));
      mbtn = gtk_spin_button_new_with_range(10.0, 16000.0, 10.0);
      gtk_grid_attach(GTK_GRID(mygrid), mbtn, 2, myrow, 1, 1);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(mbtn), freqs[i + max_eq_zeilen]);
      g_signal_connect(mbtn, "value-changed", G_CALLBACK(freq_changed_cb), GINT_TO_POINTER(i + max_eq_zeilen));
      mbtn = gtk_spin_button_new_with_range(-20.0, 20.0, 1.0);
      gtk_grid_attach(GTK_GRID(mygrid), mbtn, 3, myrow, 1, 1);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(mbtn), gains[i + max_eq_zeilen]);
      g_signal_connect(mbtn, "value-changed", G_CALLBACK(gain_changed_cb), GINT_TO_POINTER(i + max_eq_zeilen));
    }
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);

  //
  // Only show one of the three containers
  // This is the TX container upon first invocation of the TX menu,
  // but subsequent TX menu openings will show the container that
  // was active when leaving this menu before.
  //
  switch (eqid) {
  case 0:
    gtk_widget_hide(rx2_container);
    gtk_widget_hide(tx_container);
    break;

  case 1:
    gtk_widget_hide(rx1_container);
    gtk_widget_hide(tx_container);
    break;

  case 2:
    gtk_widget_hide(rx1_container);
    gtk_widget_hide(rx2_container);
    break;
  }
}
