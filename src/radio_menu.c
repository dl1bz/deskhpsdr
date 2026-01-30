/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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
#include "screen_menu.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "actions.h"
#ifdef GPIO
  #include "gpio.h"
#endif
#include "vfo.h"
#include "ext.h"
#include "message.h"

static GtkWidget *dialog = NULL;
static GtkWidget *n2adr_hpf_btn = NULL;
static gulong callsign_box_signal_id;
static gulong locator_box_signal_id;

static void cleanup(void) {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

#ifdef SOAPYSDR
static void rx_gain_element_changed_cb(GtkWidget *widget, gpointer data) {
  if (device == SOAPYSDR_USB_DEVICE) {
    double gain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

    if (strcmp(radio->name, "sdrplay") != 0) {
      adc[active_receiver->adc].gain = gain;
    }

    soapy_protocol_set_gain_element(active_receiver, (char *)gtk_widget_get_name(widget), (int) gain);
    t_print("%s: %s gain = %d\n", __func__, (char *)gtk_widget_get_name(widget), gain);
    update_rf_gain_scale_soapy(index_rf_gain());
    update_ifgr_scale_soapy(index_if_gain());
  }
}

static void tx_gain_element_changed_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit && device == SOAPYSDR_USB_DEVICE) {
    double gain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
    dac[0].gain = gain;
    soapy_protocol_set_tx_gain_element(transmitter, (char *)gtk_widget_get_name(widget), (int) gain);
  }
}

static void agc_changed_cb(GtkWidget *widget, gpointer data) {
  if (device == SOAPYSDR_USB_DEVICE) {
    int agc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    adc[active_receiver->adc].agc = agc;
    soapy_protocol_set_automatic_gain(active_receiver, agc);

    if (!agc && strcmp(radio->name, "sdrplay") != 0) {
      soapy_protocol_set_gain(active_receiver);
    }

    update_slider_hwagc_btn();
  }
}

#endif

static void ppm_value_changed_cb(GtkWidget *widget, gpointer data) {
  ppm_factor = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_frequency_changed(active_receiver);
  g_idle_add(ext_vfo_update, NULL);
}

static void rx_gain_calibration_value_changed_cb(GtkWidget *widget, gpointer data) {
  rx_gain_calibration = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void vfo_divisor_value_changed_cb(GtkWidget *widget, gpointer data) {
  vfo_encoder_divisor = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

//-------------------------------------------------------------------------------------
static void capture_time_changed_cb(GtkWidget *widget, gpointer data) {
  int t = 0;
  t = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  t = (t < 10) ? 10 : (t > 120) ? 120 : t;

  if (radio_is_transmitting()) {
    radio_end_playback();
  } else {
    radio_end_capture();
  }

  capture_max = t * 48000;

  if (capture_data != NULL) {
    g_free(capture_data);
    capture_data = NULL;
  }

  capture_data = g_new(double, capture_max);
  capture_record_pointer = 0;
  capture_replay_pointer = 0;
  capture_state = CAP_INIT;
  t_print("%s: time: %d capture_max: %d\n", __func__, t, capture_max);
}
//-------------------------------------------------------------------------------------

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
  g_idle_add(ext_vfo_update, NULL);
  radio_reconfigure_screen();
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
void setDuplex(void) {
  if (!can_transmit) { return; }

  if (duplex) {
    // TX is in separate window, also in full-screen mode
    gtk_container_remove(GTK_CONTAINER(fixed), transmitter->panel);
    tx_reconfigure(transmitter, 4 * tx_dialog_width, tx_dialog_width,  tx_dialog_height);
    tx_create_dialog(transmitter);
  } else {
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(transmitter->dialog));
    gtk_container_remove(GTK_CONTAINER(content), transmitter->panel);
    gtk_widget_destroy(transmitter->dialog);
    transmitter->dialog = NULL;
    int width = full_screen ? screen_width : display_width;
    tx_reconfigure(transmitter, width, width, rx_height);
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

void n2adr_oc_settings(void) {
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

void n2adr_oc_settings_tx(void) {
  //
  // set OC outputs for each band according to the N2ADR board requirements
  // unlike load_filters(), this can be executed outside the GTK queue
  //
  // Bit 6 is the 3 Mhz HPF and should be set from 80m - 10m except 160m
  // to set Bit 6 (shown as 7 in OC Menu) OCrx must be decimal 64 (01000000)
  // hpf_flag = 0 -> HPF OFF, hpf_flag = 1 -> HPF ON
  int rx_val = (n2adr_hpf_enable == 1) ? 64 : 0;
  BAND *band;
  band = band_get_band(band160);
  band->OCrx = 0; // 160m HPF already OFF
  band->OCtx = 1;
  band = band_get_band(band80);
  band->OCrx = rx_val;
  band->OCtx = 66;
  band = band_get_band(band60);
  band->OCrx = rx_val;
  band->OCtx = 68;
  band = band_get_band(band40);
  band->OCrx = rx_val;
  band->OCtx = 68;
  band = band_get_band(band30);
  band->OCrx = rx_val;
  band->OCtx = 72;
  band = band_get_band(band20);
  band->OCrx = rx_val;
  band->OCtx = 72;
  band = band_get_band(band17);
  band->OCrx = rx_val;
  band->OCtx = 80;
  band = band_get_band(band15);
  band->OCrx = rx_val;
  band->OCtx = 80;
  band = band_get_band(band12);
  band->OCrx = rx_val;
  band->OCtx = 96;
  band = band_get_band(band10);
  band->OCrx = rx_val;
  band->OCtx = 96;
  schedule_high_priority();
}

void load_filters(void) {
  switch (filter_board) {
  case N2ADR_TX:
    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      gtk_widget_set_sensitive(n2adr_hpf_btn, TRUE);
    }

    n2adr_oc_settings_tx();
    break;

  case N2ADR:
    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      gtk_widget_set_sensitive(n2adr_hpf_btn, FALSE);
    }

    n2adr_oc_settings();
    break;

  case ALEX:
  case APOLLO:
  case CHARLY25:
    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      gtk_widget_set_sensitive(n2adr_hpf_btn, FALSE);
    }

    // This is most likely not necessary here, but can do no harm
    radio_set_alex_antennas();
    break;

  case NO_FILTER_BOARD:
    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      gtk_widget_set_sensitive(n2adr_hpf_btn, FALSE);
    }

    break;

  default:
    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      gtk_widget_set_sensitive(n2adr_hpf_btn, FALSE);
    }

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

    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      gtk_widget_set_sensitive(n2adr_hpf_btn, FALSE);
    }

    break;

  case 1:
    filter_board = ALEX;

    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      gtk_widget_set_sensitive(n2adr_hpf_btn, FALSE);
    }

    break;

  case 2:
    filter_board = APOLLO;

    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      gtk_widget_set_sensitive(n2adr_hpf_btn, FALSE);
    }

    break;

  case 3:
    filter_board = CHARLY25;

    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      gtk_widget_set_sensitive(n2adr_hpf_btn, FALSE);
    }

    break;

  case 4:
    filter_board = N2ADR;

    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      gtk_widget_set_sensitive(n2adr_hpf_btn, FALSE);
    }

    break;

  case 5:
    filter_board = N2ADR_TX;

    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      gtk_widget_set_sensitive(n2adr_hpf_btn, TRUE);
    }

    break;
  }

  load_filters();
}

static void n2adr_hpf_btn_cb(GtkWidget *widget, gpointer data) {
  if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
      && !have_radioberry3) {
    n2adr_hpf_enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    load_filters();
  }
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

  radio_change_sample_rate(samplerate);
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

  radio_change_receivers(val);
}

static void region_cb(GtkWidget *widget, gpointer data) {
  radio_change_region(gtk_combo_box_get_active (GTK_COMBO_BOX(widget)));
}

static void rit_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
  default:
    vfo_set_rit_step(1);
    break;

  case 1:
    vfo_set_rit_step(10);
    break;

  case 2:
    vfo_set_rit_step(100);
    break;
  }
}

static void ck10mhz_cb(GtkWidget *widget, gpointer data) {
  atlas_clock_source_10mhz = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
}

static void ck128mhz_cb(GtkWidget *widget, gpointer data) {
  // atlas_clock_source_128mhz = SET(gtk_combo_box_get_active (GTK_COMBO_BOX(widget)));
  atlas_clock_source_128mhz = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
}

static void micsource_cb(GtkWidget *widget, gpointer data) {
  // atlas_mic_source = SET(gtk_combo_box_get_active (GTK_COMBO_BOX(widget)));
  atlas_mic_source = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
}

static void tx_cb(GtkWidget *widget, gpointer data) {
  // atlas_penelope = SET(gtk_combo_box_get_active (GTK_COMBO_BOX(widget)));
  atlas_penelope = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
}

static void callsign_button_clicked(GtkWidget *widget, gpointer data) {
  GtkEntry *callsign_box = GTK_ENTRY(data);
  const gchar *text = gtk_entry_get_text(callsign_box);
  gsize len = text ? strlen(text) : 0;

  if (text && len >= 3 && len < sizeof(own_callsign)) {
    gchar *upper = g_ascii_strup(text, -1);  // oder g_strup(text)
    g_strlcpy(own_callsign, upper, sizeof(own_callsign));
    g_free(upper);
  } else {
    g_signal_handler_block(callsign_box, callsign_box_signal_id);
    gtk_entry_set_text(callsign_box, own_callsign);
    g_signal_handler_unblock(callsign_box, callsign_box_signal_id);
  }

  gtk_widget_queue_draw(GTK_WIDGET(callsign_box));
  // optional: cleanup() / close_cb() wenn Save + Close gewünscht ist
  cleanup();
}

static void locator_button_clicked(GtkWidget *widget, gpointer data) {
  GtkEntry *locator_box = GTK_ENTRY(data);
  const gchar *text = gtk_entry_get_text(locator_box);
  gsize len = text ? strlen(text) : 0;

  if (text && len >= 3 && len < sizeof(own_locator)) {
    gchar *upper = g_ascii_strup(text, -1);  // oder g_strup(text)
    g_strlcpy(own_locator, upper, sizeof(own_locator));
    g_free(upper);
  } else {
    g_signal_handler_block(locator_box, locator_box_signal_id);
    gtk_entry_set_text(locator_box, own_locator);
    g_signal_handler_unblock(locator_box, locator_box_signal_id);
  }

  gtk_widget_queue_draw(GTK_WIDGET(locator_box));
  // optional: cleanup() / close_cb() wenn Save + Close gewünscht ist
  cleanup();
}

void radio_menu(GtkWidget *parent) {
  int col;
  GtkWidget *label;
  GtkWidget *ChkBtn;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[32];
  snprintf(_title, 32, "%s - Radio", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
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

  if (can_transmit) {
    //--------------------------------------------------------------------------------------------------------
    label = gtk_label_new("Audio Capture Time (sec):");
    gtk_widget_set_name(label, "boldlabel_blue");
    gtk_grid_attach(GTK_GRID(grid), label, 1, 0, 1, 1);
    //--------------------------------------------------------------------------------------------------------
    GtkWidget *capture_time_btn = gtk_spin_button_new_with_range(10, 120, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(capture_time_btn), (int)capture_max / 48000);
    gtk_widget_set_hexpand(capture_time_btn, FALSE);
    gtk_widget_set_halign(capture_time_btn, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), capture_time_btn, 2, 0, 1, 1);
    g_signal_connect(capture_time_btn, "value_changed", G_CALLBACK(capture_time_changed_cb), NULL);
    //--------------------------------------------------------------------------------------------------------
  }

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

  switch (vfo[active_receiver->id].rit_step) {
  default:
    // we should not arrive here, but just in case ...
    vfo_set_rit_step(1);
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
  label = gtk_label_new("Channel Marker for 60m:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 2, row, 1, 1);
  row++;
  GtkWidget *region_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region_combo), NULL, "NONE (VFO)");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region_combo), NULL, "UK Channels");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region_combo), NULL, "US Channels");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region_combo), NULL, "WRC15");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region_combo), NULL, "CA Channels");
  gtk_combo_box_set_active(GTK_COMBO_BOX(region_combo), region);
  gtk_widget_set_tooltip_text(region_combo,
                              "Select an option if want see colored channel marker\nin the RX panadapter.\n\n"
                              "UK - UK Channelizing 60m\n"
                              "US - US Channelizing 60m\n"
                              "WRC15 - ONE Channel according to the WRC15 recommendation for 60m\n"
                              "CA - CA Channelizing 60m\n"
                              "NONE - No channel markers");
  my_combo_attach(GTK_GRID(grid), region_combo, 2, row, 1, 1);
  g_signal_connect(region_combo, "changed", G_CALLBACK(region_cb), NULL);
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
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "N2ADR (LPF RX + TX + HPF)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(filter_combo), NULL, "N2ADR (LPF TX only)");

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

    case N2ADR_TX:
      gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 5);
      break;
    }

    my_combo_attach(GTK_GRID(grid), filter_combo, 2, row, 1, 1);
    g_signal_connect(filter_combo, "changed", G_CALLBACK(filter_cb), NULL);

    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      n2adr_hpf_btn = gtk_check_button_new_with_label("+RX: N2ADR HPF 3MHz");
      gtk_widget_set_name(n2adr_hpf_btn, "boldlabel_blue");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (n2adr_hpf_btn), n2adr_hpf_enable);
      gtk_grid_attach(GTK_GRID(grid), n2adr_hpf_btn, 3, row, 1, 1);
      g_signal_connect(n2adr_hpf_btn, "toggled", G_CALLBACK(n2adr_hpf_btn_cb), NULL);
      gtk_widget_set_sensitive(n2adr_hpf_btn, filter_board == N2ADR_TX);
    }

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
  gtk_widget_set_hexpand(vfo_divisor, FALSE);
  gtk_widget_set_halign(vfo_divisor, GTK_ALIGN_START);
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
    // It is assumed that the SDR-1000 is controlled outside deskHPSDR
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
    ChkBtn = gtk_check_button_new_with_label("Split operation");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), split);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(split_cb), NULL);
    col += 2;
    ChkBtn = gtk_check_button_new_with_label("Mute RX when TX");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), mute_rx_while_transmitting);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &mute_rx_while_transmitting);
    col++;

    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && !have_radioberry1 && !have_radioberry2
        && !have_radioberry3) {
      ChkBtn = gtk_check_button_new_with_label("HL2 5W PA enable");
      gtk_widget_set_tooltip_text(ChkBtn,
                                  "Don't forget to check/adjust in the PA MENU\n"
                                  "if all bands set to a value of <38.8>\n"
                                  "for full 5W output !");
    } else {
      ChkBtn = gtk_check_button_new_with_label("PA enable");
      gtk_widget_set_tooltip_text(ChkBtn,
                                  "Don't forget to check\n"
                                  "in the PA MENU your correct\n"
                                  "device-depend and SDR-specific\n"
                                  "TX power settings for correct output !");
    }

    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), pa_enabled);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &pa_enabled);
    row++;
    col = 0;
    ChkBtn = gtk_check_button_new_with_label("Duplex operation");
    gtk_widget_set_name(ChkBtn, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), duplex);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(duplex_cb), NULL);
  }

  col = 2;

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

  case NEW_DEVICE_HERMES_LITE2:
  case DEVICE_HERMES_LITE2: {
    if (!have_radioberry1 && !have_radioberry2 && !have_radioberry3) {
      ChkBtn = gtk_check_button_new_with_label("HL2+ audio codec");
      gtk_widget_set_name(ChkBtn, "boldlabel");
      gtk_widget_set_tooltip_text(ChkBtn,
                                  "Activate only if using a Hermes Lite 2\nwith the AK4951 Companion Board,\ncalled Hermes Lite 2 Plus");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ChkBtn), hl2_audio_codec);
      gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
      g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &hl2_audio_codec);
      col++;
      ChkBtn = gtk_check_button_new_with_label("HL2 CL1 10Mhz Ref Clock");
      gtk_widget_set_name(ChkBtn, "boldlabel");
      gtk_widget_set_tooltip_text(ChkBtn,
                                  "Switch ON if using a Hermes Lite 2 with a 10MHz\nGPS disciplined oscillator connected at CL1 input.");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ChkBtn), hl2_cl1_input);
      gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
      g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &hl2_cl1_input);
      row++;
      ChkBtn = gtk_check_button_new_with_label("HL2 ATU TUNE support");
      gtk_widget_set_name(ChkBtn, "boldlabel_blue");
      gtk_widget_set_tooltip_text(ChkBtn,
                                  "Enable ATU TUNE support in the HL2 gateware.\n\n"
                                  "Note: If enabled, the ATU detection process results\n"
                                  "in a short delay before the RF carrier is generated.\n\n"
                                  "If you use an ATU controlled by the IO board, leave this\n"
                                  "option OFF !");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ChkBtn), enable_hl2_atu_gateware);
      gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
      g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &enable_hl2_atu_gateware);
      col++;
    } else {
      hl2_audio_codec = 0;
      hl2_cl1_input = 0;
    }
  }
  break;

  case DEVICE_HERMES:
  case NEW_DEVICE_HERMES:
  case NEW_DEVICE_HERMES2: {
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
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn), soapy_iqswap);
    gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
    g_signal_connect(ChkBtn, "toggled", G_CALLBACK(toggle_cb), &soapy_iqswap);
    col++;

    if (radio->info.soapy.rx_has_automatic_gain) {
      ChkBtn = gtk_check_button_new_with_label("Hardware AGC");
      gtk_widget_set_name(ChkBtn, "boldlabel");
      gtk_grid_attach(GTK_GRID(grid), ChkBtn, col, row, 1, 1);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ChkBtn), adc[active_receiver->adc].agc);
      g_signal_connect(ChkBtn, "toggled", G_CALLBACK(agc_changed_cb), NULL);
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
  label = gtk_label_new("Freq. Calibration\n(ppm factor):");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *ppm_btn = gtk_spin_button_new_with_range(-100.0, 100.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(ppm_btn), ppm_factor);
  gtk_widget_set_tooltip_text(ppm_btn,
                              "Frequency calibration as pure ppm factor\n"
                              "with range -100.0..+100.0 in 0.1 steps,\n"
                              "because in this case we don't need\n"
                              "the exact calibration frequency.\n\n"
                              "Use a high-precision RF generator and\n"
                              "adjust a possible frequency inaccuracy.");
  gtk_widget_set_hexpand(ppm_btn, FALSE);  // fülle Box nicht nach rechts
  gtk_widget_set_halign(ppm_btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), ppm_btn, col, row, 1, 1);
  g_signal_connect(ppm_btn, "value_changed", G_CALLBACK(ppm_value_changed_cb), NULL);
  //
  // Calibration of the RF front end
  //
  col++;
  label = gtk_label_new("ADC Gain\nCalibration (dB):");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *rx_gain_calibration_b = gtk_spin_button_new_with_range(-50.0, 50.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_gain_calibration_b), (double)rx_gain_calibration);
  gtk_widget_set_tooltip_text(rx_gain_calibration_b,
                              "Adjust this value for calibrating the S-Meter.\nUse a RF generator, set output (waveform sine) level\nto -73dbm at 15MHz and connect the RF generator output\nwith the RX antenna input of the SDR.\nAdjust the value until you get exact S9 at the S-Meter.");
  gtk_widget_set_hexpand(rx_gain_calibration_b, FALSE);  // fülle Box nicht nach rechts
  gtk_widget_set_halign(rx_gain_calibration_b, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), rx_gain_calibration_b, col, row, 1, 1);
  g_signal_connect(rx_gain_calibration_b, "value_changed", G_CALLBACK(rx_gain_calibration_value_changed_cb), NULL);
  //
  // Insert small separation between top columns and bottom rows
  //
  row++;
  col = 0;
  GtkWidget *Sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top(Sep, 10);    // 10px Abstand oben
  gtk_widget_set_margin_bottom(Sep, 10); // 10px Abstand unten
  gtk_widget_set_size_request(Sep, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), Sep, col, row, 5, 1);
  row++;
  //---------------------------------------------------------------------------------------------------
  // Zeile als Box ab Spalte 0
  GtkWidget *Data_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  col = 0;
  label = gtk_label_new("Your Callsign:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_widget_set_margin_start(label, 5);
  gtk_widget_set_margin_end(label, 5);
  gtk_box_pack_start(GTK_BOX(Data_box), label, FALSE, FALSE, 0);
  GtkWidget *callsign_box = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(callsign_box), sizeof(own_callsign) - 1);
  gtk_entry_set_text(GTK_ENTRY(callsign_box), own_callsign);
  gtk_box_pack_start(GTK_BOX(Data_box), callsign_box, FALSE, FALSE, 0);
  callsign_box_signal_id = g_signal_connect(callsign_box, "activate", G_CALLBACK(callsign_button_clicked), callsign_box);
  GtkWidget *callsign_box_btn = gtk_button_new_with_label("Set");
  gtk_widget_set_halign(callsign_box_btn, GTK_ALIGN_START);
  gtk_box_pack_start(GTK_BOX(Data_box), callsign_box_btn, FALSE, FALSE, 0);
  g_signal_connect(callsign_box_btn, "clicked", G_CALLBACK(callsign_button_clicked), callsign_box);
  //---------------------------------------------------------------------------------------------------
  label = gtk_label_new("Your Locator:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_widget_set_margin_start(label, 25);
  gtk_widget_set_margin_end(label, 5);
  gtk_box_pack_start(GTK_BOX(Data_box), label, FALSE, FALSE, 0);
  GtkWidget *locator_box = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(locator_box), sizeof(own_locator) - 1);
  gtk_entry_set_text(GTK_ENTRY(locator_box), own_locator);
  gtk_box_pack_start(GTK_BOX(Data_box), locator_box, FALSE, FALSE, 0);
  locator_box_signal_id = g_signal_connect(locator_box, "activate", G_CALLBACK(locator_button_clicked), locator_box);
  GtkWidget *locator_box_btn = gtk_button_new_with_label("Set");
  gtk_widget_set_halign(locator_box_btn, GTK_ALIGN_START);
  gtk_box_pack_start(GTK_BOX(Data_box), locator_box_btn, FALSE, FALSE, 0);
  g_signal_connect(locator_box_btn, "clicked", G_CALLBACK(locator_button_clicked), locator_box);
  gtk_grid_attach(GTK_GRID(grid), Data_box, col, row, 4, 1);
  //---------------------------------------------------------------------------------------------------
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
        if (strcmp(radio->info.soapy.rx_gain[i], "CURRENT") != 0) {
          label = gtk_label_new(radio->info.soapy.rx_gain[i]);
          gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
          col++;
          SoapySDRRange range = radio->info.soapy.rx_range[i];

          if (range.step == 0.0) {
            range.step = 1.0;
          }

          GtkWidget *rx_gain = gtk_spin_button_new_with_range(range.minimum, range.maximum, range.step);
          gtk_widget_set_name (rx_gain, radio->info.soapy.rx_gain[i]);
          double value = (double)soapy_protocol_get_gain_element(active_receiver, radio->info.soapy.rx_gain[i]);
          t_print("%s: gain[%d] value = %f\n", __func__, i, value);
          gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_gain), value);
          gtk_grid_attach(GTK_GRID(grid), rx_gain, 1, row, 1, 1);
          g_signal_connect(rx_gain, "value_changed", G_CALLBACK(rx_gain_element_changed_cb), NULL);
          row++;
        }
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
        g_signal_connect(tx_gain, "value_changed", G_CALLBACK(tx_gain_element_changed_cb), NULL);
        row++;
      }
    }
  }

#endif
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
