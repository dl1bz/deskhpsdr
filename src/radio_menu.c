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

#include "main.h"
#include "discovered.h"
#include "new_menu.h"
#include "radio_menu.h"
#include "adc.h"
#include "band.h"
#include "filter.h"
#include "radio.h"
#include "receiver.h"
#include "sliders.h"
#include "new_protocol.h"
#include "old_protocol.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "actions.h"
#ifdef GPIO
  #include "gpio.h"
#endif
#include "vfo.h"
#include "ext.h"
#ifdef CLIENT_SERVER
  #include "client_server.h"
#endif

static GtkWidget *dialog = NULL;

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

#ifdef SOAPYSDR
static void rx_gain_element_changed_cb(GtkWidget *widget, gpointer data) {
  ADC *myadc = (ADC *)data;

  if (device == SOAPYSDR_USB_DEVICE) {
    myadc->gain = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
    soapy_protocol_set_gain_element(receiver[0], (char *)gtk_widget_get_name(widget), myadc->gain);
  }
}

static void tx_gain_element_changed_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit && device == SOAPYSDR_USB_DEVICE) {
    int gain = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
    soapy_protocol_set_tx_gain_element(transmitter, (char *)gtk_widget_get_name(widget), gain);
  }
}

static void agc_changed_cb(GtkWidget *widget, gpointer data) {
  gboolean agc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  soapy_protocol_set_automatic_gain(active_receiver, agc);

  if (!agc) {
    soapy_protocol_set_gain(active_receiver);
  }
}

#endif

static void calibration_value_changed_cb(GtkWidget *widget, gpointer data) {
  frequency_calibration = (long long)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void rx_gain_calibration_value_changed_cb(GtkWidget *widget, gpointer data) {
  rx_gain_calibration = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void vfo_divisor_value_changed_cb(GtkWidget *widget, gpointer data) {
  vfo_encoder_divisor = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void ptt_ring_cb(GtkWidget *widget, gpointer data) {
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    mic_ptt_tip_bias_ring = 0;
  }

  schedule_transmit_specific();
}

static void ptt_tip_cb(GtkWidget *widget, gpointer data) {
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    mic_ptt_tip_bias_ring = 1;
  }

  schedule_transmit_specific();
}

static void toggle_cb(GtkWidget *widget, gpointer data) {
  int *value = (int *) data;
  *value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_general();
  schedule_transmit_specific();
  schedule_high_priority();
}

static void anan10e_cb(GtkWidget *widget, gpointer data) {
  radio_protocol_stop();
  usleep(200000);
  anan10E = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_protocol_run();
}

static void split_cb(GtkWidget *widget, gpointer data) {
  int new = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_set_split(new);
}

//
// call-able from outside, e.g. toolbar or MIDI, through g_idle_add
//
void setDuplex() {
  if (!can_transmit) { return; }

  if (duplex) {
    // TX is in separate window, also in full-screen mode
    gtk_container_remove(GTK_CONTAINER(fixed), transmitter->panel);
    tx_reconfigure(transmitter, display_width / 4, display_height / 2);
    tx_create_dialog(transmitter);
  } else {
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(transmitter->dialog));
    gtk_container_remove(GTK_CONTAINER(content), transmitter->panel);
    gtk_widget_destroy(transmitter->dialog);
    transmitter->dialog = NULL;
    int width = full_screen ? screen_width : display_width;
    tx_reconfigure(transmitter, width, rx_height);
  }

  g_idle_add(ext_vfo_update, NULL);
}

static void duplex_cb(GtkWidget *widget, gpointer data) {
  if (radio_is_transmitting()) {
    //
    // While transmitting, ignore the click
    //
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), duplex);
    return;
  }

  duplex = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  setDuplex();
}

static void sat_cb(GtkWidget *widget, gpointer data) {
  sat_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  g_idle_add(ext_vfo_update, NULL);
}

void n2adr_oc_settings() {
  //
  // set OC outputs for each band according to the N2ADR board requirements
  // unlike load_filters(), this can be executed outside the GTK queue
  //
  BAND *band;
  band = band_get_band(band160);
  band->OCrx = band->OCtx = 1;
  band = band_get_band(band80);
  band->OCrx = band->OCtx = 66;
  band = band_get_band(band60);
  band->OCrx = band->OCtx = 68;
  band = band_get_band(band40);
  band->OCrx = band->OCtx = 68;
  band = band_get_band(band30);
  band->OCrx = band->OCtx = 72;
  band = band_get_band(band20);
  band->OCrx = band->OCtx = 72;
  band = band_get_band(band17);
  band->OCrx = band->OCtx = 80;
  band = band_get_band(band15);
  band->OCrx = band->OCtx = 80;
  band = band_get_band(band12);
  band->OCrx = band->OCtx = 96;
  band = band_get_band(band10);
  band->OCrx = band->OCtx = 96;
  schedule_high_priority();
}

void load_filters() {
  switch (filter_board) {
  case N2ADR:
    n2adr_oc_settings();
    break;

  case ALEX:
  case APOLLO:
  case CHARLY25:
    // This is most likely not necessary here, but can do no harm
    radio_set_alex_antennas();
    break;

  case NO_FILTER_BOARD:
    break;

  default:
    break;
  }

  //
  // This switches between StepAttenuator slider and CHARLY25 ATT/Preamp checkboxes
  // when the filter board is switched to/from CHARLY25
  //
  att_type_changed();
}

static void filter_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
  default:
    filter_board = NO_FILTER_BOARD;
    break;

  case 1:
    filter_board = ALEX;
    break;

  case 2:
    filter_board = APOLLO;
    break;

  case 3:
    filter_board = CHARLY25;
    break;

  case 4:
    filter_board = N2ADR;
    break;
  }

  load_filters();
}

static void mic_input_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
  default:
    mic_input_xlr = MIC3P55MM;
    break;

  case 1:
    mic_input_xlr = MICXLR;
    break;
  }

  schedule_transmit_specific();
}

static void sample_rate_cb(GtkToggleButton *widget, gpointer data) {
  const char *p = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
  int samplerate;

  //
  // There are so many different possibilities for sample rates, so
  // we just "scanf" from the combobox text entry
  //
  if (sscanf(p, "%d", &samplerate) != 1) { return; }

  if (radio_is_remote) {
#ifdef CLIENT_SERVER
    send_sample_rate(client_socket, -1, samplerate);
#endif
  } else {
    radio_change_sample_rate(samplerate);
  }
}

static void receivers_cb(GtkToggleButton *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget)) + 1;

  //
  // reconfigure_radio requires that the RX panels are active
  // (segfault otherwise), therefore ignore this while TXing
  //
  if (radio_is_transmitting()) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), receivers - 1);
    return;
  }

  if (radio_is_remote) {
#ifdef CLIENT_SERVER
    send_receivers(client_socket, val);
#endif
  } else {
    radio_change_receivers(val);
  }
}

static void region_cb(GtkWidget *widget, gpointer data) {
  #if !defined (__LDESK__)
  radio_change_region(gtk_combo_box_get_active (GTK_COMBO_BOX(widget)));
  #endif
}

static void rit_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
  default:
    rit_increment = 1;
    break;

  case 1:
    rit_increment = 10;
    break;

  case 2:
    rit_increment = 100;
    break;
  }
}

static void ck10mhz_cb(GtkWidget *widget, gpointer data) {
  atlas_clock_source_10mhz = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
}

static void ck128mhz_cb(GtkWidget *widget, gpointer data) {
  atlas_clock_source_128mhz = SET(gtk_combo_box_get_active (GTK_COMBO_BOX(widget)));
}

static void micsource_cb(GtkWidget *widget, gpointer data) {
  atlas_mic_source = SET(gtk_combo_box_get_active (GTK_COMBO_BOX(widget)));
}

static void tx_cb(GtkWidget *widget, gpointer data) {
  atlas_penelope = SET(gtk_combo_box_get_active (GTK_COMBO_BOX(widget)));
}

void radio_menu(GtkWidget *parent) {
  int col;
  GtkWidget *label;
  GtkWidget *ChkBtn;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  #if defined (__LDESK__)
  char _title[32];
  snprintf(_title, 32, "%s - Radio", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  #else
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Radio");
  #endif
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_column_homogeneous (GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous (GTK_GRID(grid), FALSE);
  int row;
  int max_row;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button_press_event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  row = 1;
  label = gtk_label_new("Receivers:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  row++;
  GtkWidget *receivers_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(receivers_combo), NULL, "1");

  if (radio->supported_receivers > 1) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(receivers_combo), NULL, "2");
  }

  gtk_combo_box_set_active(GTK_COMBO_BOX(receivers_combo), receivers - 1);
  my_combo_attach(GTK_GRID(grid), receivers_combo, 0, row, 1, 1);
  g_signal_connect(receivers_combo, "changed", G_CALLBACK(receivers_cb), NULL);
  row++;

  switch (protocol) {
  case NEW_PROTOCOL:
    // Sample rate changes handled in the RX menu
    break;

  case ORIGINAL_PROTOCOL: {
    label = gtk_label_new("Sample Rate:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    row++;
    GtkWidget *sample_rate_combo_box = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "48000");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "96000");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "192000");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "384000");

    switch (active_receiver->sample_rate) {
    case 48000:
      gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 0);
      break;

    case 96000:
      gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 1);
      break;

    case 192000:
      gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 2);
      break;

    case 384000:
      gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 3);
      break;
    }

    my_combo_attach(GTK_GRID(grid), sample_rate_combo_box, 0, row, 1, 1);
    g_signal_connect(sample_rate_combo_box, "changed", G_CALLBACK(sample_rate_cb), NULL);
    row++;
  }
  break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL: {
    label = gtk_label_new("Sample Rate:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    row++;
    char rate_string[16];
    GtkWidget *sample_rate_combo_box = gtk_combo_box_text_new();
    int rate = radio->info.soapy.sample_rate;
    int pos = 0;

    while (rate >= 48000) {
      snprintf(rate_string, 16, "%d", rate);
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, rate_string);

      if (rate == active_receiver->sample_rate) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), pos);
      }

      rate = rate / 2;
      pos++;
    }

    my_combo_attach(GTK_GRID(grid), sample_rate_combo_box, 0, row, 1, 1);
    g_signal_connect(sample_rate_combo_box, "changed", G_CALLBACK(sample_rate_cb), NULL);
  }

  row++;
  break;
#endif
  }

  max_row = row;
  row = 1;
  label = gtk_label_new("RIT/XIT step (Hz):");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 1, row, 1, 1);
  row++;
  GtkWidget *rit_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rit_combo), NULL, "1");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rit_combo), NULL, "10");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rit_combo), NULL, "100");

  switch (rit_increment) {
  default:
    // we should not arrive here, but just in case ...
    rit_increment = 1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(rit_combo), 0);
    break;

  case 1:
    gtk_combo_box_set_active(GTK_COMBO_BOX(rit_combo), 0);
    break;

  case 10:
    gtk_combo_box_set_active(GTK_COMBO_BOX(rit_combo), 1);
    break;

  case 100:
    gtk_combo_box_set_active(GTK_COMBO_BOX(rit_combo), 2);
    break;
  }

  my_combo_attach(GTK_GRID(grid), rit_combo, 1, row, 1, 1);
  g_signal_connect(rit_combo, "changed", G_CALLBACK(rit_cb), NULL);
  row++;
  label = gtk_label_new("SAT mode:");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, 1, row, 1, 1);
  row++;
  GtkWidget *sat_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sat_combo), NULL, "SAT Off");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sat_combo), NULL, "SAT");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sat_combo), NULL, "RSAT");
  gtk_combo_box_set_active(GTK_COMBO_BOX(sat_combo), sat_mode);
  my_combo_attach(GTK_GRID(grid), sat_combo, 1, row, 1, 1);
  g_signal_connect(sat_combo, "changed", G_CALLBACK(sat_cb), NULL);
  row++;

  if (row > max_row) { max_row = row; }

  row = 1;
  label = gtk_label_new("Region for 60m:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 2, row, 1, 1);
  #if defined (__LDESK__)
  gtk_widget_set_sensitive (label, FALSE);
  #endif
  row++;
  GtkWidget *region_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region_combo), NULL, "Other");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region_combo), NULL, "UK");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region_combo), NULL, "WRC15");
  gtk_combo_box_set_active(GTK_COMBO_BOX(region_combo), region);
  my_combo_attach(GTK_GRID(grid), region_combo, 2, row, 1, 1);
  g_signal_connect(region_combo, "changed", G_CALLBACK(region_cb), NULL);
  #if defined (__LDESK__)
  gtk_widget_set_sensitive (region_combo, FALSE);
  #endif
  row++;

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    label = gtk_label_new("Filter Board:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 2, row, 1, 1);
    row++;
    GtkWidget *filter_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "NONE");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "ALEX");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "APOLLO");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "CHARLY25");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "N2ADR");

    switch (filter_board) {
    case NO_FILTER_BOARD:
      gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 0);
      break;

    case ALEX:
      gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 1);
      break;

    case APOLLO:
      gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 2);
      break;

    case CHARLY25:
      gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 3);
      break;

    case N2ADR:
      gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 4);
      break;
    }

    my_combo_attach(GTK_GRID(grid), filter_combo, 2, row, 1, 1);
    g_signal_connect(filter_combo, "changed", G_CALLBACK(filter_cb), NULL);
    row++;
  }

  if (row > max_row) { max_row = row; }

  row = max_row;
  label = gtk_label_new("VFO Encoder Divisor:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 2, 1);
  GtkWidget *vfo_divisor = gtk_spin_button_new_with_range(1.0, 60.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(vfo_divisor), (double)vfo_encoder_divisor);
  gtk_grid_attach(GTK_GRID(grid), vfo_divisor, 2, row, 1, 1);
  g_signal_connect(vfo_divisor, "value_changed", G_CALLBACK(vfo_divisor_value_changed_cb), NULL);
  row++;

  // cppcheck-suppress knownConditionTrueFalse
  if (row > max_row) { max_row = row; }

  //
  // The HPSDR machine-specific stuff is now put in columns 3+4
  // either the ATLAS bits (METIS) or the ORION microphone settings
  //
  if (device == DEVICE_OZY || device == DEVICE_METIS) {
    row = 1;
    label = gtk_label_new("ATLAS bus settings:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 2, 1);
    row++;
    label = gtk_label_new("10 MHz source:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 1, 1);
    GtkWidget *ck10mhz_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ck10mhz_combo), NULL, "Atlas");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ck10mhz_combo), NULL, "Penelope");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ck10mhz_combo), NULL, "Mercury");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ck10mhz_combo), atlas_clock_source_10mhz);
    my_combo_attach(GTK_GRID(grid), ck10mhz_combo, 4, row, 1, 1);
    g_signal_connect(ck10mhz_combo, "changed", G_CALLBACK(ck10mhz_cb), NULL);
    row++;
    label = gtk_label_new("122.88 MHz source:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 1, 1);
    GtkWidget *ck128mhz_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ck128mhz_combo), NULL, "Penelope");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ck128mhz_combo), NULL, "Mercury");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ck128mhz_combo), SET(atlas_clock_source_128mhz));
    my_combo_attach(GTK_GRID(grid), ck128mhz_combo, 4, row, 1, 1);
    g_signal_connect(ck128mhz_combo, "changed", G_CALLBACK(ck128mhz_cb), NULL);
    row++;
    label = gtk_label_new("Mic source:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 1, 1);
    GtkWidget *micsource_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(micsource_combo), NULL, "Janus");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(micsource_combo), NULL, "Penelope");
    gtk_combo_box_set_active(GTK_COMBO_BOX(micsource_combo), SET(atlas_mic_source));
    my_combo_attach(GTK_GRID(grid), micsource_combo, 4, row, 1, 1);
    g_signal_connect(micsource_combo, "changed", G_CALLBACK(micsource_cb), NULL);
    row++;
    label = gtk_label_new("TX config:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 1, 1);
    GtkWidget *tx_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(tx_combo), NULL, "No TX");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(tx_combo), NULL, "Penelope");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(tx_combo), NULL, "Pennylane");
    gtk_combo_box_set_active(GTK_COMBO_BOX(tx_combo), atlas_penelope);
    my_combo_attach(GTK_GRID(grid), tx_combo, 4, row, 1, 1);
    g_signal_connect(tx_combo, "changed", G_CALLBACK(tx_cb), NULL);
    row++;

    //
    // This option is for ATLAS systems which *only* have an OZY
    // and a JANUS board (the RF front end then is either SDR-1000 or SoftRock)
    //
    // It is assumed that the SDR-1000 is controlled outside piHPSDR
    //
    if (device == DEVICE_OZY) {
      ChkBtn = gtk_check_button_new_with_label("Janus Only");
      gtk_widget_set_name(ChkBtn, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), atlas_janus);
      gtk_grid_attach(GTK_GRID(grid), ChkBtn, 4, row, 1, 1);
      g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &atlas_janus);
      row++;
    }

    if (row > max_row) { max_row = row; }
  }

  if (device == NEW_DEVICE_ORION || device == NEW_DEVICE_ORION2 || device == NEW_DEVICE_SATURN ||
  device == DEVICE_ORION || device == DEVICE_ORION2) {
    row = 1;
    label = gtk_label_new("ORION/SATURN Mic jack:");
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), label, 3, row, 2, 1);
    row++;
    GtkWidget *ptt_ring_b = gtk_radio_button_new_with_label(NULL, "PTT On Ring, Mic and Bias on Tip");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ptt_ring_b), mic_ptt_tip_bias_ring == 0);
    gtk_grid_attach(GTK_GRID(grid), ptt_ring_b, 3, row, 2, 1);
    g_signal_connect(ptt_ring_b, "toggled", G_CALLBACK(ptt_ring_cb), NULL);
    row++;
    GtkWidget *ptt_tip_b = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(ptt_ring_b),
                           "PTT On Tip, Mic and Bias on Ring");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ptt_tip_b), mic_ptt_tip_bias_ring == 1);
    gtk_grid_attach(GTK_GRID(grid), ptt_tip_b, 3, row, 2, 1);
    g_signal_connect(ptt_tip_b, "toggled", G_CALLBACK(ptt_tip_cb), NULL);
    row++;

    if (device == NEW_DEVICE_SATURN) {
      label = gtk_label_new("Mic Input:");
      gtk_widget_set_name(label, "boldlabel");
      gtk_grid_attach(GTK_GRID(grid), label, 4, row, 1, 1);
      GtkWidget *mic_input_combo = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mic_input_combo), NULL, "3.5mm");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mic_input_combo), NULL, "XLR");

      switch (mic_input_xlr) {
      case MIC3P55MM:
        gtk_combo_box_set_active(GTK_COMBO_BOX(mic_input_combo), 0);
        break;

      case MICXLR:
        gtk_combo_box_set_active(GTK_COMBO_BOX(mic_input_combo), 1);
        break;
      }

      my_combo_attach(GTK_GRID(grid), mic_input_combo, 4, row + 1, 1, 1);
      g_signal_connect(mic_input_combo, "changed", G_CALLBACK(mic_input_cb), NULL);
    }

    ChkBtn = gtk_check_button_new_with_label("PTT Enabled");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), mic_ptt_enabled);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, 3, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &mic_ptt_enabled);
    row++;
    ChkBtn = gtk_check_button_new_with_label("BIAS Enabled");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), mic_bias_enabled);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, 3, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &mic_bias_enabled);
    row++;

    if (row > max_row) { max_row = row; }
  }

  row = max_row;
  col = 0;
  //
  // Insert small separation between top columns and bottom rows
  //
  GtkWidget *Separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(Separator, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), Separator, col, row, 5, 1);
  row++;

  if (can_transmit) {
    ChkBtn = gtk_check_button_new_with_label("Split");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), split);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(split_cb), NULL);
    col++;
    ChkBtn = gtk_check_button_new_with_label("Duplex");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), duplex);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(duplex_cb), NULL);
    col++;
    ChkBtn = gtk_check_button_new_with_label("Mute RX when TX");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), mute_rx_while_transmitting);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &mute_rx_while_transmitting);
    col++;
    ChkBtn = gtk_check_button_new_with_label("PA enable");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), pa_enabled);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &pa_enabled);
    row++;
  }

  col = 0;
  ChkBtn = gtk_check_button_new_with_label("Optimize for TouchScreen");
  gtk_widget_set_name(ChkBtn, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), optimize_for_touchscreen);
  gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 2, 1);
  g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &optimize_for_touchscreen);
  col += 2;

  switch (device) {
  case NEW_DEVICE_ORION2:
  case NEW_DEVICE_SATURN: {
    ChkBtn = gtk_check_button_new_with_label("Mute Spkr Amp");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), mute_spkr_amp);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &mute_spkr_amp);
    col++;
  }
  break;

  case DEVICE_HERMES_LITE2: {
    ChkBtn = gtk_check_button_new_with_label("HL2 audio codec");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ChkBtn), hl2_audio_codec);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &hl2_audio_codec);
    col++;
  }
  break;

  case DEVICE_HERMES:
  case NEW_DEVICE_HERMES: {
    ChkBtn = gtk_check_button_new_with_label("Anan-10E/100B");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ChkBtn), anan10E);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(anan10e_cb), NULL);
    col++;
  }
  break;
#ifdef SOAPYSDR

  case SOAPYSDR_USB_DEVICE: {
    ChkBtn = gtk_check_button_new_with_label("Swap IQ");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), iqswap);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &iqswap);
    col++;

    if (radio->info.soapy.rx_has_automatic_gain) {
      ChkBtn = gtk_check_button_new_with_label("Hardware AGC");
      gtk_widget_set_name(ChkBtn, "boldlabel");
      gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ChkBtn), adc[0].agc);
      g_signal_connect(ChkBtn, "toggled", G_CALLBACK(agc_changed_cb), &adc[0]);
      col++;
    }
  }
  break;
#endif
  }

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    row++;
    ChkBtn = gtk_check_button_new_with_label("Enable TxInhibit Input");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), enable_tx_inhibit);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, 0, row, 2, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &enable_tx_inhibit);
    ChkBtn = gtk_check_button_new_with_label("Enable AutoTune Input");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), enable_auto_tune);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, 2, row, 2, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &enable_auto_tune);
  }

  row++;
  // cppcheck-suppress redundantAssignment
  col = 0;
  label = gtk_label_new("Frequency\nCalibration (Hz):");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *calibration_b = gtk_spin_button_new_with_range(-9999.0, 9999.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(calibration_b), (double)frequency_calibration);
  gtk_grid_attach(GTK_GRID(grid), calibration_b, col, row, 1, 1);
  g_signal_connect(calibration_b, "value_changed", G_CALLBACK(calibration_value_changed_cb), NULL);
  //
  // Calibration of the RF front end
  //
  col++;
  #if defined (__LDESK__)
  label = gtk_label_new("ADC Gain\nCalibration (dB):");
  #else
  label = gtk_label_new("RX Gain\nCalibration (dB):");
  #endif
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *rx_gain_calibration_b = gtk_spin_button_new_with_range(-50.0, 50.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_gain_calibration_b), (double)rx_gain_calibration);
  gtk_grid_attach(GTK_GRID(grid), rx_gain_calibration_b, col, row, 1, 1);
  g_signal_connect(rx_gain_calibration_b, "value_changed", G_CALLBACK(rx_gain_calibration_value_changed_cb), NULL);
#ifdef SOAPYSDR
  row++;
  col = 0;

  if (device == SOAPYSDR_USB_DEVICE) {
    //
    // If there is only a single RX or TX gain element, then we need not display it
    // since it is better done via the main "Drive" or "RF gain" slider.
    // At the moment, we present spin-buttons for the individual gain ranges
    // for SDRplay, RTLsdr, and whenever there is more than on RX or TX gain element.
    //
    int display_gains = 0;

    //
    // For RTL sticks, there is a single gain element named "TUNER", and this is very well
    // controlled by the main RF gain slider. This perhaps also true for sdrplay, but
    // I could not test this because I do not have the hardware
    //
    //if (strcmp(radio->name, "sdrplay") == 0 || strcmp(radio->name, "rtlsdr") == 0) { display_gains = 1; }
    if (strcmp(radio->name, "sdrplay") == 0 )  { display_gains = 1; }

    if (radio->info.soapy.rx_gains > 1) { display_gains = 1; }

    if (radio->info.soapy.tx_gains > 1) { display_gains = 1; }

    if (display_gains) {
      //
      // Select RX gain from one out of a list of ranges (represented by spin buttons)
      //
      label = gtk_label_new("RX Gains:");
      gtk_widget_set_name(label, "boldlabel");
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

      if (can_transmit) {
        label = gtk_label_new("TX Gains:");
        gtk_widget_set_name(label, "boldlabel");
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), label, 2, row, 1, 1);
      }
    }

    row++;
    max_row = row;

    if (display_gains) {
      //
      // Draw a spin-button for each range
      //
      for (size_t i = 0; i < radio->info.soapy.rx_gains; i++) {
        label = gtk_label_new(radio->info.soapy.rx_gain[i]);
        gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
        col++;
        SoapySDRRange range = radio->info.soapy.rx_range[i];

        if (range.step == 0.0) {
          range.step = 1.0;
        }

        GtkWidget *rx_gain = gtk_spin_button_new_with_range(range.minimum, range.maximum, range.step);
        gtk_widget_set_name (rx_gain, radio->info.soapy.rx_gain[i]);
        int value = soapy_protocol_get_gain_element(active_receiver, radio->info.soapy.rx_gain[i]);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_gain), (double)value);
        gtk_grid_attach(GTK_GRID(grid), rx_gain, 1, row, 1, 1);
        g_signal_connect(rx_gain, "value_changed", G_CALLBACK(rx_gain_element_changed_cb), &adc[0]);
        row++;
      }
    }

    row = max_row;

    if (can_transmit && display_gains) {
      //
      // Select TX gain out of a list of discrete ranges
      //
      for (size_t i = 0; i < radio->info.soapy.tx_gains; i++) {
        label = gtk_label_new(radio->info.soapy.tx_gain[i]);
        gtk_grid_attach(GTK_GRID(grid), label, 2, row, 1, 1);
        SoapySDRRange range = radio->info.soapy.tx_range[i];

        if (range.step == 0.0) {
          range.step = 1.0;
        }

        GtkWidget *tx_gain = gtk_spin_button_new_with_range(range.minimum, range.maximum, range.step);
        gtk_widget_set_name (tx_gain, radio->info.soapy.tx_gain[i]);
        int value = soapy_protocol_get_tx_gain_element(transmitter, radio->info.soapy.tx_gain[i]);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_gain), (double)value);
        gtk_grid_attach(GTK_GRID(grid), tx_gain, 3, row, 1, 1);
        g_signal_connect(tx_gain, "value_changed", G_CALLBACK(tx_gain_element_changed_cb), &dac[0]);
        row++;
      }
    }
  }

#endif
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
