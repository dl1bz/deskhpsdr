/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#include "new_menu.h"
#include "ant_menu.h"
#include "band.h"
#include "radio.h"
#include "new_protocol.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "message.h"

static GtkWidget *dialog = NULL;
static GtkWidget *grid = NULL;
static GtkWidget *hf_container = NULL;
static GtkWidget *xvtr_container = NULL;
#ifdef SOAPYSDR
  static GtkWidget *adc0_antenna_combo_box;
  static GtkWidget *dac0_antenna_combo_box;
#endif

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

static void rx_ant_cb(GtkToggleButton *widget, gpointer data) {
  int b = GPOINTER_TO_INT(data);
  int ant = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  BAND *band = band_get_band(b);
  band->alexRxAntenna = ant;
  radio_set_alex_antennas();
}

static void tx_ant_cb(GtkToggleButton *widget, gpointer data) {
  int b = GPOINTER_TO_INT(data);
  int ant = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  BAND *band = band_get_band(b);
  band->alexTxAntenna = ant;
  radio_set_alex_antennas();
}

#ifdef SOAPYSDR
static void adc0_antenna_cb(GtkComboBox *widget, gpointer data) {
  ADC *myadc = (ADC *)data;
  myadc->antenna = gtk_combo_box_get_active(widget);
  schedule_high_priority();

  if (device == SOAPYSDR_USB_DEVICE) {
    soapy_protocol_set_rx_antenna(receiver[0], adc[0].antenna);
  }
}

static void dac0_antenna_cb(GtkComboBox *widget, gpointer data) {
  DAC *mydac = (DAC *)data;
  mydac->antenna = gtk_combo_box_get_active(widget);
  schedule_high_priority();

  if (device == SOAPYSDR_USB_DEVICE && can_transmit) {
    soapy_protocol_set_tx_antenna(transmitter, mydac->antenna);
  }
}

#endif

static void show_hf() {
  GtkWidget *label;
  int bands = radio_max_band();
  hf_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), hf_container, 0, 1, 6, 1);
  GtkWidget *mygrid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(mygrid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(mygrid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(mygrid), 5);
  label = gtk_label_new("Band");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 0, 0, 1, 1);
  label = gtk_label_new("RX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 1, 0, 1, 1);
  label = gtk_label_new("TX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 2, 0, 1, 1);
  label = gtk_label_new("   ");
  gtk_grid_attach(GTK_GRID(mygrid), label, 3, 0, 1, 1);
  label = gtk_label_new("Band");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 4, 0, 1, 1);
  label = gtk_label_new("RX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 5, 0, 1, 1);
  label = gtk_label_new("TX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 6, 0, 1, 1);
  int col = 0;
  int row = 1;

  for (int i = 0; i <= bands; i++) {
    const BAND *band = band_get_band(i);

    if (strlen(band->title) > 0) {
      if (col > 6) {
        row++;
        col = 0;
      }

      label = gtk_label_new(band->title);
      gtk_widget_set_name(label, "boldlabel");
      gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
      col++;
      GtkWidget *rxcombo = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant3");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ext1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ext2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Xvtr");
      gtk_combo_box_set_active(GTK_COMBO_BOX(rxcombo), band->alexRxAntenna);
      my_combo_attach(GTK_GRID(mygrid), rxcombo, col, row, 1, 1);
      g_signal_connect(rxcombo, "changed", G_CALLBACK(rx_ant_cb), GINT_TO_POINTER(i));
      col++;
      GtkWidget *txcombo = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant3");
      gtk_combo_box_set_active(GTK_COMBO_BOX(txcombo), band->alexTxAntenna);
      my_combo_attach(GTK_GRID(mygrid), txcombo, col, row, 1, 1);
      g_signal_connect(txcombo, "changed", G_CALLBACK(tx_ant_cb), GINT_TO_POINTER(i));
      col++;
      col++;
    }
  }

  gtk_container_add(GTK_CONTAINER(hf_container), mygrid);
}

static void show_xvtr() {
  GtkWidget *label; // re-used for all labels
  int num = 0;

  for (int i = BANDS; i < BANDS + XVTRS; i++) {
    const BAND *band = band_get_band(i);

    if (strlen(band->title) > 0) { num++; }
  }

  // If there are no transverters, there is nothing to do
  if (num == 0) {
    xvtr_container = NULL;
    return;
  }

  xvtr_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), xvtr_container, 0, 1, 6, 1);
  GtkWidget *mygrid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(mygrid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(mygrid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(mygrid), 5);
  label = gtk_label_new("Band");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 0, 0, 1, 1);
  label = gtk_label_new("RX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 1, 0, 1, 1);
  label = gtk_label_new("TX Ant");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(mygrid), label, 2, 0, 1, 1);

  if (num > 1) {
    // Only if there is more than one xvtr band, the
    // second column is used
    label = gtk_label_new("   ");
    gtk_grid_attach(GTK_GRID(mygrid), label, 3, 0, 1, 1);
    label = gtk_label_new("Band");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), label, 4, 0, 1, 1);
    label = gtk_label_new("RX Ant");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), label, 5, 0, 1, 1);
    label = gtk_label_new("TX Ant");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(mygrid), label, 6, 0, 1, 1);
  }

  int col = 0;
  int row = 1;

  for (int i = BANDS; i < BANDS + XVTRS; i++) {
    const BAND *band = band_get_band(i);

    if (strlen(band->title) > 0) {
      if (col > 6) {
        row++;
        col = 0;
      }

      label = gtk_label_new(band->title);
      gtk_widget_set_name(label, "boldlabel");
      gtk_grid_attach(GTK_GRID(mygrid), label, col, row, 1, 1);
      col++;
      GtkWidget *rxcombo = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ant3");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ext1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Ext2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rxcombo), NULL, "Xvtr");
      gtk_combo_box_set_active(GTK_COMBO_BOX(rxcombo), band->alexRxAntenna);
      my_combo_attach(GTK_GRID(mygrid), rxcombo, col, row, 1, 1);
      g_signal_connect(rxcombo, "changed", G_CALLBACK(rx_ant_cb), GINT_TO_POINTER(i));
      col++;
      GtkWidget *txcombo = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant1");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant2");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(txcombo), NULL, "Ant3");
      gtk_combo_box_set_active(GTK_COMBO_BOX(txcombo), band->alexTxAntenna);
      my_combo_attach(GTK_GRID(mygrid), txcombo, col, row, 1, 1);
      g_signal_connect(txcombo, "changed", G_CALLBACK(tx_ant_cb), GINT_TO_POINTER(i));
      col++;
      col++;
    }
  }

  gtk_container_add(GTK_CONTAINER(xvtr_container), mygrid);
}

static void hf_rb_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (xvtr_container != NULL) { gtk_widget_hide(xvtr_container); }

  if (hf_container   != NULL) { gtk_widget_show(hf_container  ); }
}

static void xvtr_rb_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (xvtr_container != NULL) { gtk_widget_show(xvtr_container); }

  if (hf_container   != NULL) { gtk_widget_hide(hf_container  ); }
}

static void newpa_cb(GtkWidget *widget, gpointer data) {
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    new_pa_board = 1;
  } else {
    new_pa_board = 0;
  }

  schedule_high_priority();
}

void ant_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  #if defined (__LDESK__)
  char _title[32];
  snprintf(_title, 32, "%s - ANT", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  #else
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - ANT");
  #endif
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);

  if (device != SOAPYSDR_USB_DEVICE) {
    GtkWidget *hf_rb = gtk_radio_button_new_with_label(NULL, "HF");
    gtk_widget_set_name(hf_rb, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(hf_rb), TRUE);
    g_signal_connect(hf_rb, "toggled", G_CALLBACK(hf_rb_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), hf_rb, 1, 0, 1, 1);
    GtkWidget *xvtr_rb = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(hf_rb), "XVTR");
    gtk_widget_set_name(xvtr_rb, "boldlabel");
    g_signal_connect(xvtr_rb, "toggled", G_CALLBACK(xvtr_rb_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), xvtr_rb, 2, 0, 1, 1);
  }

  if (device == NEW_DEVICE_HERMES || device == NEW_DEVICE_ANGELIA || device == NEW_DEVICE_ORION ||
  device == DEVICE_HERMES     || device == DEVICE_ANGELIA     || device == DEVICE_ORION) {
    //
    // ANAN-100/200: There is an "old" (Rev. 15/16) and "new" (Rev. 24) PA board
    //               around which differs in relay settings for using EXT1,2 and
    //               differs in how to do PS feedback.
    //
    GtkWidget *new_pa_b = gtk_check_button_new_with_label("ANAN 100/200 new PA board");
    gtk_widget_set_name(new_pa_b, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(new_pa_b), new_pa_board);
    gtk_grid_attach(GTK_GRID(grid), new_pa_b, 3, 0, 5, 1);
    g_signal_connect(new_pa_b, "toggled", G_CALLBACK(newpa_cb), NULL);
  }

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    show_hf();
    show_xvtr();
  }

#ifdef SOAPYSDR

  if (device == SOAPYSDR_USB_DEVICE) {
    t_print("rx_antennas=%ld\n", radio->info.soapy.rx_antennas);

    if (radio->info.soapy.rx_antennas > 0) {
      GtkWidget *antenna_label = gtk_label_new("RX Antenna:");
      gtk_widget_set_name(antenna_label, "boldlabel");
      gtk_grid_attach(GTK_GRID(grid), antenna_label, 0, 1, 1, 1);
      adc0_antenna_combo_box = gtk_combo_box_text_new();

      for (size_t i = 0; i < radio->info.soapy.rx_antennas; i++) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(adc0_antenna_combo_box), NULL, radio->info.soapy.rx_antenna[i]);
      }

      gtk_combo_box_set_active(GTK_COMBO_BOX(adc0_antenna_combo_box), adc[0].antenna);
      g_signal_connect(adc0_antenna_combo_box, "changed", G_CALLBACK(adc0_antenna_cb), &adc[0]);
      my_combo_attach(GTK_GRID(grid), adc0_antenna_combo_box, 1, 1, 1, 1);
    }

    if (can_transmit) {
      t_print("tx_antennas=%ld\n", radio->info.soapy.tx_antennas);

      if (radio->info.soapy.tx_antennas > 0) {
        GtkWidget *antenna_label = gtk_label_new("TX Antenna:");
        gtk_widget_set_name(antenna_label, "boldlabel");
        gtk_grid_attach(GTK_GRID(grid), antenna_label, 0, 2, 1, 1);
        dac0_antenna_combo_box = gtk_combo_box_text_new();

        for (size_t i = 0; i < radio->info.soapy.tx_antennas; i++) {
          gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(dac0_antenna_combo_box), NULL, radio->info.soapy.tx_antenna[i]);
        }

        gtk_combo_box_set_active(GTK_COMBO_BOX(dac0_antenna_combo_box), dac[0].antenna);
        g_signal_connect(dac0_antenna_combo_box, "changed", G_CALLBACK(dac0_antenna_cb), &dac[0]);
        my_combo_attach(GTK_GRID(grid), dac0_antenna_combo_box, 1, 2, 1, 1);
      }
    }
  }

#endif
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);

  if (xvtr_container != NULL) { gtk_widget_hide(xvtr_container); }
}
