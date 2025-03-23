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
#include <stdlib.h>
#include <math.h>

#include "appearance.h"
#include "receiver.h"
#include "sliders.h"
#include "mode.h"
#include "filter.h"
#include "bandstack.h"
#include "band.h"
#include "discovered.h"
#include "new_protocol.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "vfo.h"
#include "agc.h"
#include "channel.h"
#include "radio.h"
#include "transmitter.h"
#include "property.h"
#include "main.h"
#include "ext.h"
#include "rigctl.h"
#include "actions.h"
#include "message.h"
#include "audio.h"
#include "tx_menu.h"

static int width;
static int height;

static GtkWidget *sliders;

static guint scale_timer;
static enum ACTION scale_status = NO_ACTION;
static GtkWidget *scale_dialog;

static GtkWidget *af_gain_label;
static GtkWidget *af_gain_scale;
static GtkWidget *rf_gain_label = NULL;
static GtkWidget *rf_gain_scale = NULL;
static GtkWidget *agc_gain_label;
static GtkWidget *agc_scale;
static GtkWidget *attenuation_label = NULL;
static GtkWidget *attenuation_scale = NULL;
static GtkWidget *c25_container = NULL;
static GtkWidget *c25_att_combobox = NULL;
static GtkWidget *c25_att_label = NULL;
static GtkWidget *mic_gain_label;
static GtkWidget *mic_gain_scale;
static GtkWidget *drive_label;
static GtkWidget *drive_scale;
static GtkWidget *squelch_label;
static GtkWidget *squelch_scale;
static gulong     squelch_signal_id;
static GtkWidget *squelch_enable;
#if defined (__LDESK__)
  static GtkWidget *tune_drive_label;
  GtkWidget *tune_drive_scale;
  GtkWidget *local_mic_button;
  GtkWidget *local_mic_input;
  static GtkWidget *local_mic_label;
  gulong local_mic_toggle_signal_id;
  gulong tune_drive_scale_signal_id;
  gulong local_mic_input_signal_id;
  GtkWidget *autogain_btn;
  gulong autogain_btn_signal_id;
  GtkWidget *bbcompr_scale;
  gulong bbcompr_scale_signal_id;
  static GtkWidget *bbcompr_label;
  static GtkWidget *lev_label;
  GtkWidget *lev_scale;
  gulong lev_scale_signal_id;
  static GtkWidget *preamp_label;
  GtkWidget *preamp_scale;
  gulong preamp_scale_signal_id;
#endif

//
// general tool for displaying a pop-up slider. This can also be used for a value for which there
// is no GTK slider. Make the slider "insensitive" so one cannot operate on it.
// Putting this into a separate function avoids much code repetition.
//

int scale_timeout_cb(gpointer data) {
  gtk_widget_destroy(scale_dialog);
  scale_status = NO_ACTION;
  return FALSE;
}

void show_popup_slider(enum ACTION action, int rx, double min, double max, double delta, double value,
                       const char *title) {
  //
  // general function for displaying a pop-up slider. This can also be used for a value for which there
  // is no GTK slider. Make the slider "insensitive" so one cannot operate on it.
  // Putting this into a separate function avoids much code repetition.
  //
  static GtkWidget *popup_scale = NULL;
  static int scale_rx;
  static double scale_min;
  static double scale_max;
  static double scale_wid;

  if (suppress_popup_sliders) {
    return;
  }

  //
  // a) if there is still a pop-up slider on the screen for a different action, destroy it
  //
  if (scale_status != action || scale_rx != rx) {
    if (scale_status != NO_ACTION) {
      g_source_remove(scale_timer);
      gtk_widget_destroy(scale_dialog);
      scale_status = NO_ACTION;
    }
  }

  if (scale_status == NO_ACTION) {
    //
    // b) if a pop-up slider for THIS action is not on display, create one
    //    (only in this case input parameters min and max will be used)
    //
    scale_status = action;
    scale_rx = rx;
    scale_min = min;
    scale_max = max;
    scale_wid = max - min;
    scale_dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(top_window), GTK_DIALOG_DESTROY_WITH_PARENT, NULL, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(scale_dialog));
    popup_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, delta);
    gtk_widget_set_name(popup_scale, "popup_scale");
    gtk_widget_set_size_request (popup_scale, 400, 30);
    gtk_range_set_value (GTK_RANGE(popup_scale), value),
                        gtk_widget_show(popup_scale);
    gtk_widget_set_sensitive(popup_scale, FALSE);
    gtk_container_add(GTK_CONTAINER(content), popup_scale);
    scale_timer = g_timeout_add(2000, scale_timeout_cb, NULL);
    gtk_dialog_run(GTK_DIALOG(scale_dialog));
  } else {
    //
    // c) if a pop-up slider for THIS action is still on display, adjust value and reset timeout
    //
    g_source_remove(scale_timer);

    if (value > scale_min + 1.01 * scale_wid) {
      scale_min = scale_min + 0.5 * scale_wid;
      scale_max = scale_max + 0.5 * scale_wid;
      gtk_range_set_range(GTK_RANGE(popup_scale), scale_min, scale_max);
    }

    if (value < scale_max - 1.01 * scale_wid) {
      scale_min = scale_min - 0.5 * scale_wid;
      scale_max = scale_max - 0.5 * scale_wid;
      gtk_range_set_range(GTK_RANGE(popup_scale), scale_min, scale_max);
    }

    gtk_range_set_value (GTK_RANGE(popup_scale), value),
                        scale_timer = g_timeout_add(2000, scale_timeout_cb, NULL);
  }
}

int sliders_active_receiver_changed(void *data) {
  if (display_sliders) {
    //
    // Change sliders and check-boxes to reflect the state of the
    // new active receiver
    //
    gtk_range_set_value(GTK_RANGE(af_gain_scale), active_receiver->volume);
    gtk_range_set_value (GTK_RANGE(agc_scale), active_receiver->agc_gain);
    //
    // need block/unblock so setting the value of the receivers does not
    // enable/disable squelch
    //
    g_signal_handler_block(G_OBJECT(squelch_scale), squelch_signal_id);
    gtk_range_set_value (GTK_RANGE(squelch_scale), active_receiver->squelch);
    g_signal_handler_unblock(G_OBJECT(squelch_scale), squelch_signal_id);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(squelch_enable), active_receiver->squelch_enable);

    if (filter_board == CHARLY25) {
      update_c25_att();
    } else {
      if (attenuation_scale != NULL) { gtk_range_set_value (GTK_RANGE(attenuation_scale), (double)adc[active_receiver->adc].attenuation); }

      if (rf_gain_scale != NULL) { gtk_range_set_value (GTK_RANGE(rf_gain_scale), adc[active_receiver->adc].gain); }
    }
  }

  return FALSE;
}

void set_attenuation_value(double value) {
  //t_print("%s value=%f\n",__FUNCTION__,value);
  if (!have_rx_att) { return; }

  adc[active_receiver->adc].attenuation = (int)value;
  schedule_high_priority();

  if (display_sliders) {
    gtk_range_set_value (GTK_RANGE(attenuation_scale), (double)adc[active_receiver->adc].attenuation);
  } else {
    char title[64];
    snprintf(title, 64, "Attenuation - ADC-%d (dB)", active_receiver->adc);
    show_popup_slider(ATTENUATION, active_receiver->adc, 0.0, 31.0, 1.0, (double)adc[active_receiver->adc].attenuation,
                      title);
  }
}

static void attenuation_value_changed_cb(GtkWidget *widget, gpointer data) {
  if (!have_rx_att) { return; }

  adc[active_receiver->adc].attenuation = gtk_range_get_value(GTK_RANGE(attenuation_scale));
  schedule_high_priority();
}

void att_type_changed() {
  //
  // This function manages a transition from/to a CHARLY25 filter board
  // Note all sliders might be non-existent, e.g. if sliders are not
  // displayed at all. So verify all widgets are non-NULL
  //
  //t_print("%s\n",__FUNCTION__);
  if (filter_board == CHARLY25) {
    if (attenuation_label != NULL) { gtk_widget_hide(attenuation_label); }

    if (rf_gain_label != NULL) { gtk_widget_hide(rf_gain_label); }

    if (attenuation_scale != NULL) { gtk_widget_hide(attenuation_scale); }

    if (c25_container != NULL) { gtk_widget_show(c25_container); }

    if (c25_att_label != NULL) { gtk_widget_show(c25_att_label); }

    //
    // There is no step attenuator visible any more. Set to zero
    //
    set_attenuation_value(0.0);
    set_rf_gain(active_receiver->id, 0.0); // this will be a no-op
  } else {
    if (attenuation_label != NULL) { gtk_widget_show(attenuation_label); }

    if (rf_gain_label != NULL) { gtk_widget_show(rf_gain_label); }

    if (attenuation_scale != NULL) { gtk_widget_show(attenuation_scale); }

    if (c25_container != NULL) { gtk_widget_hide(c25_container); }

    if (c25_att_label != NULL) { gtk_widget_hide(c25_att_label); }
  }

  sliders_active_receiver_changed(NULL);
}

static void c25_att_combobox_changed(GtkWidget *widget, gpointer data) {
  int val = atoi(gtk_combo_box_get_active_id(GTK_COMBO_BOX(widget)));

  if (active_receiver->adc == 0) {
    //
    // this button is only valid for the first ADC
    // store attenuation, such that in meter.c the correct level is displayed
    // There is no adjustable preamp or attenuator, so nail these values to zero
    //
    switch (val) {
    case -36:
      active_receiver->alex_attenuation = 3;
      active_receiver->preamp = 0;
      active_receiver->dither = 0;
      break;

    case -24:
      active_receiver->alex_attenuation = 2;
      active_receiver->preamp = 0;
      active_receiver->dither = 0;
      break;

    case -12:
      active_receiver->alex_attenuation = 1;
      active_receiver->preamp = 0;
      active_receiver->dither = 0;
      break;

    case 0:
      active_receiver->alex_attenuation = 0;
      active_receiver->preamp = 0;
      active_receiver->dither = 0;
      break;

    case 18:
      active_receiver->alex_attenuation = 0;
      active_receiver->preamp = 1;
      active_receiver->dither = 0;
      break;

    case 36:
      active_receiver->alex_attenuation = 0;
      active_receiver->preamp = 1;
      active_receiver->dither = 1;
      break;
    }
  } else {
    //
    // For second ADC, always show "0 dB" on the button
    //
    active_receiver->alex_attenuation = 0;
    active_receiver->preamp = 0;
    active_receiver->dither = 0;

    if (val != 0) {
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(c25_att_combobox), "0");
    }
  }
}

void update_c25_att() {
  //
  // Only effective with the CHARLY25 filter board.
  // Change the Att/Preamp combo-box to the current attenuation status
  //
  if (filter_board == CHARLY25) {
    char id[16];

    if (active_receiver->adc != 0) {
      active_receiver->alex_attenuation = 0;
      active_receiver->preamp = 0;
      active_receiver->dither = 0;
    }

    //
    // This is to recover from an "illegal" props file
    //
    if (active_receiver->preamp || active_receiver->dither) {
      active_receiver->alex_attenuation = 0;
    }

    int att = -12 * active_receiver->alex_attenuation + 18 * active_receiver->dither + 18 * active_receiver->preamp;
    snprintf(id, 16, "%d", att);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(c25_att_combobox), id);
  }
}

static void agcgain_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->agc_gain = gtk_range_get_value(GTK_RANGE(agc_scale));
  rx_set_agc(active_receiver);
}

void set_agc_gain(int rx, double value) {
  //t_print("%s value=%f\n",__FUNCTION__, value);
  if (rx >= receivers) { return; }

  receiver[rx]->agc_gain = value;
  rx_set_agc(receiver[rx]);

  if (display_sliders && active_receiver->id == rx) {
    gtk_range_set_value (GTK_RANGE(agc_scale), receiver[rx]->agc_gain);
  } else {
    char title[64];
    snprintf(title, 64, "AGC Gain RX%d", rx + 1);
    show_popup_slider(AGC_GAIN, rx, -20.0, 120.0, 1.0, receiver[rx]->agc_gain, title);
  }
}

static void afgain_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->volume = gtk_range_get_value(GTK_RANGE(af_gain_scale));
  rx_set_af_gain(active_receiver);
}

void set_af_gain(int rx, double value) {
  if (rx >= receivers) { return; }

  receiver[rx]->volume = value;
  rx_set_af_gain(receiver[rx]);

  if (display_sliders && rx == active_receiver->id) {
    gtk_range_set_value (GTK_RANGE(af_gain_scale), value);
  } else {
    char title[64];
    snprintf(title, 64, "AF Gain RX%d", rx + 1);
    show_popup_slider(AF_GAIN, rx, -40.0, 0.0, 1.0, value, title);
  }
}

static void rf_gain_value_changed_cb(GtkWidget *widget, gpointer data) {
  adc[active_receiver->adc].gain = gtk_range_get_value(GTK_RANGE(rf_gain_scale));

  switch (protocol) {
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    soapy_protocol_set_gain(active_receiver);
    break;
#endif

  default:
    break;
  }
}

void set_rf_gain(int rx, double value) {
  if (!have_rx_gain) { return; }

  if (rx >= receivers) { return; }

  int rxadc = receiver[rx]->adc;
  //t_print("%s rx=%d adc=%d val=%f\n",__FUNCTION__, rx, rxadc, value);
  adc[rxadc].gain = value;
#ifdef SOAPYSDR

  if (protocol == SOAPYSDR_PROTOCOL) {
    soapy_protocol_set_gain(receiver[rx]);
  }

#endif

  if (display_sliders && active_receiver->id == rx) {
    gtk_range_set_value (GTK_RANGE(rf_gain_scale), adc[rxadc].gain);
  } else {
    char title[64];
    snprintf(title, 64, "RF Gain ADC %d", rxadc);
    show_popup_slider(RF_GAIN, rxadc, adc[rxadc].min_gain, adc[rxadc].max_gain, 1.0, adc[rxadc].gain, title);
  }
}

void show_filter_width(int rx, int width) {
  //t_print("%s width=%d\n",__FUNCTION__, width);
  char title[64];
  int min, max;
  snprintf(title, 64, "Filter Width RX%d (Hz)", rx + 1);
  min = 0;
  max = 2 * width;

  if (max < 200) { max = 200; }

  if (width > 1000) {
    max = width + 1000;
    min = width - 1000;
  }

  if (width > 3000) {
    max = width + 2000;
    min = width - 2000;
  }

  show_popup_slider(IF_WIDTH, rx, (double)(min), (double)(max), 1.0, (double) width, title);
}

void show_filter_shift(int rx, int shift) {
  //t_print("%s shift=%d\n",__FUNCTION__, shift);
  char title[64];
  int min, max;
  snprintf(title, 64, "Filter SHIFT RX%d (Hz)", rx + 1);
  min = shift - 500;
  max = shift + 500;
  show_popup_slider(IF_SHIFT, rx, (double)(min), (double) (max), 1.0, (double) shift, title);
}

static void micgain_value_changed_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit) {
    transmitter->mic_gain = gtk_range_get_value(GTK_RANGE(widget));
#if defined (__LDESK__) && defined (__USELESS__)
    int mode = vfo_get_tx_mode();
    mode_settings[mode].mic_gain = transmitter->mic_gain;
    copy_mode_settings(mode);
#endif
    tx_set_mic_gain(transmitter);
  }
}

void set_linein_gain(double value) {
  //t_print("%s value=%f\n",__FUNCTION__, value);
  linein_gain = value;
  show_popup_slider(LINEIN_GAIN, 0, -34.0, 12.0, 1.0, linein_gain, "LineIn Gain");
}

void set_mic_gain(double value) {
  //t_print("%s value=%f\n",__FUNCTION__, value);
  if (can_transmit) {
    transmitter->mic_gain = value;
#if defined (__LDESK__) && defined (__USELESS__)
    int mode = vfo_get_tx_mode();
    mode_settings[mode].mic_gain = transmitter->mic_gain;
    copy_mode_settings(mode);
#endif
    tx_set_mic_gain(transmitter);

    if (display_sliders) {
      gtk_range_set_value (GTK_RANGE(mic_gain_scale), value);
    } else {
      show_popup_slider(MIC_GAIN, 0, -12.0, 50.0, 1.0, value, "Mic Gain");
    }
  }
}

void set_drive(double value) {
  //t_print("%s value=%f\n",__FUNCTION__,value);
  int txmode = vfo_get_tx_mode();

  if (txmode == modeDIGU || txmode == modeDIGL) {
    if (value > drive_digi_max) { value = drive_digi_max; }
  }

  radio_set_drive(value);

  if (display_sliders) {
    if (device == DEVICE_HERMES_LITE2 && pa_enabled) {
      value /= 20;
    }

    gtk_range_set_value (GTK_RANGE(drive_scale), value);
  } else {
    show_popup_slider(DRIVE, 0, 0.0, drive_max, 1.0, value, "TX Drive");
  }
}

static void drive_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(drive_scale));

  if (device == DEVICE_HERMES_LITE2 && pa_enabled) {
    value *= 20;
  }

  t_print("%s: value=%f at device %d\n", __FUNCTION__, value, device);
  int txmode = vfo_get_tx_mode();

  if (txmode == modeDIGU || txmode == modeDIGL) {
    if (value > drive_digi_max) { value = drive_digi_max; }
  }

  radio_set_drive(value);

  if (device == DEVICE_HERMES_LITE2 && pa_enabled) {
    value /= 20;
  }

  gtk_range_set_value (GTK_RANGE(drive_scale), value);
}

void show_filter_high(int rx, int var) {
  //t_print("%s var=%d\n",__FUNCTION__,var);
  char title[64];
  int min, max;
  snprintf(title, 64, "Filter Cut High RX%d (Hz)", rx + 1);
  //
  // The hi-cut is always non-negative
  //
  min = 0;
  max = 2 * var;

  if (max <  200) { max = 200; }

  if (var > 1000) {
    max = var + 1000;
    min = var - 1000;
  }

  show_popup_slider(FILTER_CUT_HIGH, rx, (double)(min), (double)(max), 1.00, (double) var, title);
}

void show_filter_low(int rx, int var) {
  char title[64];
  int min, max;
  snprintf(title, 64, "Filter Cut Low RX%d (Hz)", rx + 1);

  //
  // The low-cut is either always positive, or always negative for a given mode
  //
  if (var > 0) {
    min = 0;
    max = 2 * var;

    if (max <  200) { max = 200; }

    if (var > 1000) {
      max = var + 1000;
      min = var - 1000;
    }
  } else {
    max = 0;
    min = 2 * var;

    if (min >  -200) { min = -200; }

    if (var < -1000) {
      max = var + 1000;
      min = var - 1000;
    }
  }

  show_popup_slider(FILTER_CUT_LOW, rx, (double)(min), (double)(max), 1.00, (double) var, title);
}

static void squelch_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->squelch = gtk_range_get_value(GTK_RANGE(widget));
  active_receiver->squelch_enable = (active_receiver->squelch > 0.5);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(squelch_enable), active_receiver->squelch_enable);
  rx_set_squelch(active_receiver);
}

static void squelch_enable_cb(GtkWidget *widget, gpointer data) {
  active_receiver->squelch_enable = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  rx_set_squelch(active_receiver);
}

static void local_mic_toggle_cb(GtkWidget *widget, gpointer data) {
  int v = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

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

#if defined (__LDESK__) && defined (__CPYMODE__)
  int mode = vfo_get_tx_mode();

  if (transmitter->local_microphone) {
    mode_settings[mode].local_microphone = 1;
  } else {
    mode_settings[mode].local_microphone = 0;
  }

  t_print("%s: mode: %d transmitter->local_microphone: %d mode_settings[%d].local_microphone %d\n",
          __FUNCTION__, mode, transmitter->local_microphone, mode, mode_settings[mode].local_microphone);
  copy_mode_settings(mode);
  g_idle_add(ext_vfo_update, NULL);
#endif
}

#if defined (__LDESK__)
static void tune_drive_changed_cb(GtkWidget *widget, gpointer data) {
  int value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  if (value < 1) { value = 1; }

  if (value > 100) { value = 100; }

  transmitter->tune_drive = value;
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), transmitter->tune_drive);

  if (can_transmit && transmitter->tune_use_drive) {
    transmitter->tune_use_drive = 0;
  }

  g_idle_add(ext_vfo_update, NULL);
}

static void bbcompr_scale_changed_cb(GtkWidget *widget, gpointer data) {
  int mode = vfo_get_tx_mode();
  double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  int    vi = (v >= 0.0) ? (int) (v + 0.5) : (int) (v - 0.5);
  transmitter->compressor_level = vi;
  mode_settings[mode].compressor_level = vi;
  copy_mode_settings(mode);
  tx_set_compressor(transmitter);
  g_idle_add(ext_vfo_update, NULL);
}

static void lev_scale_changed_cb(GtkWidget *widget, gpointer data) {
  int mode = vfo_get_tx_mode();
  double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  transmitter->lev_gain = v;
  mode_settings[mode].lev_gain = v;
  copy_mode_settings(mode);
  tx_set_compressor(transmitter);
  g_idle_add(ext_vfo_update, NULL);
}

static void preamp_scale_changed_cb(GtkWidget *widget, gpointer data) {
  double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  transmitter->addgain_gain = v;
  tx_set_mic_gain(transmitter);
  g_idle_add(ext_vfo_update, NULL);
}



void update_slider_local_mic_input() {
  if (display_sliders) {
    g_signal_handler_block(G_OBJECT(local_mic_input), local_mic_input_signal_id);

    for (int i = 0; i < n_input_devices; i++) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(local_mic_input), NULL, input_devices[i].description);

      if (strcmp(transmitter->microphone_name, input_devices[i].name) == 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(local_mic_input), i);
      }
    }

    // If the combo box shows no device, take the first one
    // AND set the mic.name to that device name.
    // This situation occurs if the local microphone device in the props
    // file is no longer present

    if (gtk_combo_box_get_active(GTK_COMBO_BOX(local_mic_input))  < 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(local_mic_input), 0);
      g_strlcpy(transmitter->microphone_name, input_devices[0].name, sizeof(transmitter->microphone_name));
    }

    g_signal_handler_unblock(G_OBJECT(local_mic_input), local_mic_input_signal_id);
    gtk_widget_queue_draw(local_mic_input);
  }
}

void update_slider_local_mic_button() {
  if (display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (local_mic_button), local_mic_toggle_signal_id);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (local_mic_button), transmitter->local_microphone);
    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (local_mic_button), local_mic_toggle_signal_id);
    gtk_widget_queue_draw(local_mic_button);
  }
}

void update_slider_tune_drive_scale(gboolean show_widget) {
  if (display_sliders) {
    g_signal_handler_block(G_OBJECT(tune_drive_scale), tune_drive_scale_signal_id);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tune_drive_scale), transmitter->tune_drive);

    if (show_widget && !transmitter->tune_use_drive) {
      gtk_widget_set_sensitive(tune_drive_scale, TRUE);
      gtk_label_set_text(GTK_LABEL(tune_drive_label), "TUNE\nDrv");
      gtk_widget_set_name(tune_drive_label, "slider2_blue");
      gtk_widget_show(tune_drive_scale);
    } else {
      gtk_widget_set_sensitive(tune_drive_scale, FALSE);
      gtk_widget_hide(tune_drive_scale);
      gtk_label_set_text(GTK_LABEL(tune_drive_label), "TUNE =\nTX Pwr");
      gtk_widget_set_name(tune_drive_label, "slider2_red");
    }

    g_signal_handler_unblock(G_OBJECT(tune_drive_scale), tune_drive_scale_signal_id);
    gtk_widget_queue_draw(tune_drive_scale);
    gtk_widget_queue_draw(tune_drive_label);
  }
}

void update_slider_bbcompr_scale(gboolean show_widget) {
  if (display_sliders) {
    g_signal_handler_block(G_OBJECT(bbcompr_scale), bbcompr_scale_signal_id);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(bbcompr_scale), transmitter->compressor_level);

    if (show_widget && transmitter->compressor) {
      gtk_widget_set_sensitive(bbcompr_scale, TRUE);
      gtk_widget_set_name(bbcompr_label, "slider2_blue");
    } else {
      gtk_widget_set_sensitive(bbcompr_scale, FALSE);
      gtk_widget_set_name(bbcompr_label, "slider2_red");
    }

    g_signal_handler_unblock(G_OBJECT(bbcompr_scale), bbcompr_scale_signal_id);
    gtk_widget_queue_draw(bbcompr_scale);
    gtk_widget_queue_draw(bbcompr_label);
  }
}

void update_slider_lev_scale(gboolean show_widget) {
  if (display_sliders) {
    g_signal_handler_block(G_OBJECT(lev_scale), lev_scale_signal_id);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(lev_scale), transmitter->lev_gain);

    if (show_widget && transmitter->lev_enable) {
      gtk_widget_set_sensitive(lev_scale, TRUE);
      gtk_widget_set_name(lev_label, "slider2_blue");
    } else {
      gtk_widget_set_sensitive(lev_scale, FALSE);
      gtk_widget_set_name(lev_label, "slider2_red");
    }

    g_signal_handler_unblock(G_OBJECT(lev_scale), lev_scale_signal_id);
    gtk_widget_queue_draw(lev_scale);
    gtk_widget_queue_draw(lev_label);
  }
}

void update_slider_preamp_scale(gboolean show_widget) {
  if (display_sliders) {
    g_signal_handler_block(G_OBJECT(preamp_scale), preamp_scale_signal_id);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(preamp_scale), transmitter->addgain_gain);

    if (show_widget && transmitter->addgain_enable) {
      gtk_widget_set_sensitive(preamp_scale, TRUE);
      gtk_widget_set_name(preamp_label, "slider2_blue");
    } else {
      gtk_widget_set_sensitive(preamp_scale, FALSE);
      gtk_widget_set_name(preamp_label, "slider2_red");
    }

    g_signal_handler_unblock(G_OBJECT(preamp_scale), preamp_scale_signal_id);
    gtk_widget_queue_draw(preamp_scale);
    gtk_widget_queue_draw(preamp_label);
  }
}

void update_slider_autogain_btn() {
  if (display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (autogain_btn), autogain_btn_signal_id);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autogain_btn), autogain_enabled);
    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (autogain_btn), autogain_btn_signal_id);
    gtk_widget_queue_draw(autogain_btn);
  }
}

#if defined (__AUTOG__)
static void autogain_enable_cb(GtkWidget *widget, gpointer data) {
  autogain_enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  launch_autogain_hl2();
  g_idle_add(ext_vfo_update, NULL);
}

#endif

#endif

void set_squelch(RECEIVER *rx) {
  //t_print("%s\n",__FUNCTION__);
  //
  // automatically enable/disable squelch if squelch value changed
  // you can still enable/disable squelch via the check-box, but
  // as soon the slider is moved squelch is enabled/disabled
  // depending on the "new" squelch value
  //
  rx->squelch_enable = (rx->squelch > 0.5);
  rx_set_squelch(rx);

  if (display_sliders && rx->id == active_receiver->id) {
    gtk_range_set_value (GTK_RANGE(squelch_scale), rx->squelch);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(squelch_enable), rx->squelch_enable);
  } else {
    char title[64];
    snprintf(title, 64, "Squelch RX%d (Hz)", rx->id + 1);
    show_popup_slider(SQUELCH, rx->id, 0.0, 100.0, 1.0, rx->squelch, title);
  }
}

void show_diversity_gain() {
  show_popup_slider(DIV_GAIN, 0, -27.0, 27.0, 0.01, div_gain, "Diversity Gain");
}

void show_diversity_phase() {
  show_popup_slider(DIV_PHASE, 0, -180.0, 180.0, 0.1, div_phase, "Diversity Phase");
}

// will ce called from radio.c and initializing the slider surface depend from the selected screen size
GtkWidget *sliders_init(int my_width, int my_height) {
#if defined (__LDESK__)
  width = my_width - 50;
  // GdkRGBA bgcolor;  // Deklaration der GdkRGBA-Struktur
  int selected_mode = vfo[active_receiver->id].mode;
#else
  width = my_width;
#endif
  height = my_height;
  int widget_height = height / 2;

  if (can_transmit) {
    widget_height = height / 3;
  }

  t_print("sliders_init: width=%d height=%d widget_height=%d\n", width, height, widget_height);
  //
  // The larger the width, the smaller the fraction used for the label can be
  // font size.
  //
  int twidth, swidth, bwidth, tpix;
  int t1pos, t2pos, t3pos;
#if defined (__LDESK__)
  int s1pos, s2pos, s3pos;
#else
  int s1pos, s2pos, s3pos, sqpos;
#endif
  int b1pos, b2pos, b3pos;
  const char *csslabel;
  const char *csslabel_smaller;

  if (width < 1024) {
    tpix   =  width / 9;
#if defined (__LDESK__)
  } else if (width < 1441) {
#else
  } else if (width < 1280) {
#endif
    tpix   =  width / 12;
  } else {
    tpix   =  width / 15;
  }

  //
  // Depending on the width for the Label, we can increase the
  // font size. Note the minimum value for tpix is 71
  // (for a 640-pix-screen)
  //
  if (tpix < 75 ) {
    csslabel = "slider1";
  } else if (tpix < 85) {
    csslabel = "slider2";
  } else {
    csslabel = "slider3";
  }

  if (!strcmp(csslabel, "slider1")) { csslabel_smaller = "slider0"; }

  if (!strcmp(csslabel, "slider2")) { csslabel_smaller = "slider1"; }

  if (!strcmp(csslabel, "slider3")) { csslabel_smaller = "slider2"; }

  int lbl_w_fix = width / 23;  // Label_width fixed now
  int sl_w_fix = width / slider_surface_scale; // slider_width fixed now, default 5.0 if Linux and 4.1 if macOS
  // DL1BZ, 19.3.2025:
  // from now we use the new slider surface resize factor in Screen Menu
  // for adjust the whole slider surface design better
  //
  // the old "dynamic" calculation had a lot of issues depend from the screen size
  // I had redesign the sliders GRID complete new from scratch. GTK has some limitations
  // if you use a GTK_GRID positioning only with pixels
  twidth = 2; // 2 Spalten,  Label
  swidth = 4; // 4 Spalten,  Slider
  bwidth = 1; // 1 Spalte,   Klick_Button if used
  t1pos  =  0;
  b1pos  =  t1pos + twidth;
  s1pos  =  b1pos + twidth;
  t2pos  =  s1pos + swidth;
  b2pos  =  t2pos + twidth;
  s2pos  =  b2pos + twidth;
  t3pos  =  s2pos + swidth;
  b3pos  =  t3pos + twidth;
  s3pos  =  b3pos + twidth;
  // some debug output for info
  t_print("%s: t1pos=%d s1pos=%d t2pos=%d s2pos=%d t3pos=%d s3pos=%d\n",
          __FUNCTION__, t1pos, s1pos, t2pos, s2pos, t3pos, s3pos);
  t_print("%s: max. slider surface column: %d\n", __FUNCTION__, s3pos + swidth);
  t_print("%s: twidth=%d swidth=%d bwidth=%d lbl_w_fix=%d sl_w_fix=%d\n",
          __FUNCTION__, twidth, swidth, bwidth, lbl_w_fix, sl_w_fix);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  sliders = gtk_grid_new();
  gtk_widget_set_size_request (sliders, width, height);
  gtk_grid_set_row_homogeneous(GTK_GRID(sliders), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(sliders), FALSE);
  gtk_widget_set_margin_top(sliders, 0);    // Kein Abstand oben
  gtk_widget_set_margin_bottom(sliders, 0); // Kein Abstand unten
  gtk_widget_set_margin_start(sliders, 15);  // Kein Abstand am Anfang
  gtk_widget_set_margin_end(sliders, 15);    // Kein Abstand am Ende
  // Definiere die Hintergrundfarbe (grün)
  // gdk_rgba_parse(&bgcolor, "white");
  // gtk_widget_override_background_color(sliders, GTK_STATE_FLAG_NORMAL, &bgcolor);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#if defined (__LDESK__)
  af_gain_label = gtk_label_new("Vol");
#else
  af_gain_label = gtk_label_new("AF");
#endif
  gtk_widget_set_size_request(af_gain_label, lbl_w_fix, widget_height);
  gtk_widget_set_name(af_gain_label, csslabel);
  gtk_widget_set_halign(af_gain_label, GTK_ALIGN_CENTER);
  gtk_label_set_justify(GTK_LABEL(af_gain_label), GTK_JUSTIFY_CENTER);
  gtk_widget_show(af_gain_label);
  gtk_grid_attach(GTK_GRID(sliders), af_gain_label, t1pos, 0, twidth, 1);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  af_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -40.0, 0.0, 1.00);
  gtk_widget_set_size_request(af_gain_scale, sl_w_fix, widget_height);
  gtk_widget_set_valign(af_gain_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(af_gain_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(af_gain_scale), active_receiver->volume);

  for (float i = -40.0; i <= 0.0; i += 5.0) {
    gtk_scale_add_mark(GTK_SCALE(af_gain_scale), i, GTK_POS_TOP, NULL);
  }

  gtk_widget_show(af_gain_scale);
  gtk_grid_attach(GTK_GRID(sliders), af_gain_scale, s1pos, 0, swidth, 1);
  g_signal_connect(G_OBJECT(af_gain_scale), "value_changed", G_CALLBACK(afgain_value_changed_cb), NULL);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  agc_gain_label = gtk_label_new("AGC");
  gtk_widget_set_size_request(agc_gain_label, lbl_w_fix, widget_height);
  gtk_widget_set_name(agc_gain_label, csslabel);
  gtk_widget_set_halign(agc_gain_label, GTK_ALIGN_CENTER);
  gtk_label_set_justify(GTK_LABEL(agc_gain_label), GTK_JUSTIFY_CENTER);
  gtk_widget_show(agc_gain_label);
  gtk_grid_attach(GTK_GRID(sliders), agc_gain_label, t2pos, 0, twidth, 1);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  agc_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -20.0, 120.0, 1.0);
  gtk_widget_set_size_request(agc_scale, sl_w_fix, widget_height);
  gtk_widget_set_valign(agc_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(agc_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(agc_scale), active_receiver->agc_gain);
  gtk_widget_show(agc_scale);
  gtk_grid_attach(GTK_GRID(sliders), agc_scale, s2pos, 0, swidth, 1);
  g_signal_connect(G_OBJECT(agc_scale), "value_changed", G_CALLBACK(agcgain_value_changed_cb), NULL);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#if defined (__AUTOG__)

  if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
    autogain_btn = gtk_check_button_new();
    gtk_widget_set_size_request(autogain_btn, 0, widget_height);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autogain_btn), autogain_enabled);
    gtk_widget_show(autogain_btn);
    gtk_grid_attach(GTK_GRID(sliders), autogain_btn, b3pos, 0, twidth, 1);
    gtk_widget_set_halign(autogain_btn, GTK_ALIGN_CENTER);
    autogain_btn_signal_id = g_signal_connect(autogain_btn, "toggled", G_CALLBACK(autogain_enable_cb), NULL);
  }

#endif

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (have_rx_gain) {
#if defined (__LDESK__)

    if (device == DEVICE_HERMES_LITE2) {
      rf_gain_label = gtk_label_new("HL2:Gain\nRxPGA");
      gtk_widget_set_name(rf_gain_label, csslabel_smaller);
    } else {
      rf_gain_label = gtk_label_new("Gain");
      gtk_widget_set_name(rf_gain_label, csslabel);
    }

#else
    rf_gain_label = gtk_label_new("RF");
    gtk_widget_set_name(rf_gain_label, csslabel);
#endif
    gtk_widget_set_size_request(rf_gain_label, lbl_w_fix, widget_height);
    gtk_widget_set_halign(rf_gain_label, GTK_ALIGN_END);
    gtk_label_set_justify(GTK_LABEL(rf_gain_label), GTK_JUSTIFY_CENTER);
    gtk_widget_show(rf_gain_label);
    gtk_grid_attach(GTK_GRID(sliders), rf_gain_label, t3pos, 0, twidth, 1);
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    rf_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, adc[0].min_gain, adc[0].max_gain, 1.0);
    gtk_widget_set_size_request(rf_gain_scale, sl_w_fix, widget_height);
    gtk_widget_set_valign(rf_gain_scale, GTK_ALIGN_CENTER);
    gtk_range_set_value (GTK_RANGE(rf_gain_scale), adc[0].gain);
    gtk_range_set_increments (GTK_RANGE(rf_gain_scale), 1.0, 1.0);
    gtk_widget_show(rf_gain_scale);
#if defined (__AUTOG__)

    if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
      gtk_grid_attach(GTK_GRID(sliders), rf_gain_scale, s3pos, 0, swidth, 1);

      for (float i = adc[0].min_gain; i <= adc[0].max_gain; i += 6.0) {
        gtk_scale_add_mark(GTK_SCALE(rf_gain_scale), i, GTK_POS_TOP, NULL);
      }
    } else {
      gtk_grid_attach(GTK_GRID(sliders), rf_gain_scale, s3pos, 0, swidth, 1);

      for (float i = adc[0].min_gain; i <= adc[0].max_gain; i += 6.0) {
        gtk_scale_add_mark(GTK_SCALE(rf_gain_scale), i, GTK_POS_TOP, NULL);
      }
    }

#else
    gtk_grid_attach(GTK_GRID(sliders), rf_gain_scale, s3pos, 0, swidth, 1);

    for (float i = adc[0].min_gain; i <= adc[0].max_gain; i += 6.0) {
      gtk_scale_add_mark(GTK_SCALE(rf_gain_scale), i, GTK_POS_TOP, NULL);
    }

#endif
    g_signal_connect(G_OBJECT(rf_gain_scale), "value_changed", G_CALLBACK(rf_gain_value_changed_cb), NULL);
  } else {
    rf_gain_label = NULL;
    rf_gain_scale = NULL;
  }

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (have_rx_att) {
    attenuation_label = gtk_label_new("ATT");
    gtk_widget_set_size_request(attenuation_label, lbl_w_fix, widget_height);
    gtk_widget_set_name(attenuation_label, csslabel);
    gtk_widget_set_halign(attenuation_label, GTK_ALIGN_END);
    gtk_widget_show(attenuation_label);
    gtk_grid_attach(GTK_GRID(sliders), attenuation_label, t3pos, 0, twidth, 1);
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    attenuation_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 31.0, 1.0);
    gtk_widget_set_size_request(attenuation_scale, sl_w_fix, widget_height);
    gtk_widget_set_valign(attenuation_scale, GTK_ALIGN_CENTER);
    gtk_range_set_value (GTK_RANGE(attenuation_scale), adc[active_receiver->adc].attenuation);
    gtk_range_set_increments (GTK_RANGE(attenuation_scale), 1.0, 1.0);
    gtk_widget_show(attenuation_scale);
    gtk_grid_attach(GTK_GRID(sliders), attenuation_scale, s3pos, 0, swidth, 1);
    g_signal_connect(G_OBJECT(attenuation_scale), "value_changed", G_CALLBACK(attenuation_value_changed_cb), NULL);
  } else {
    attenuation_label = NULL;
    attenuation_scale = NULL;
  }

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //
  // These handles need to be created because they are activated/deactivaded
  // depending on selecting/deselcting the CHARLY25 filter board
  // Because "touch-screen friendly" comboboxes cannot be shown/hidden properly,
  // we put this into a container
  //
  c25_att_label = gtk_label_new("Att/Pre");
  gtk_widget_set_name(c25_att_label, csslabel);
  gtk_widget_set_halign(c25_att_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(sliders), c25_att_label, t3pos, 0, twidth, 1);
  c25_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(sliders), c25_container, s3pos, 0, twidth, 1);
  GtkWidget *c25_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(c25_grid), TRUE);
  //
  // One could achieve a finer granulation by combining attenuators and preamps,
  // but it seems sufficient to either engage attenuators or preamps
  //
  c25_att_combobox = gtk_combo_box_text_new();
  gtk_widget_set_name(c25_att_combobox, csslabel);
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "-36", "-36 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "-24", "-24 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "-12", "-12 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "0",   "  0 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "18",  "+18 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "36",  "+36 dB");
  my_combo_attach(GTK_GRID(c25_grid), c25_att_combobox, 0, 0, 2, 1);
  g_signal_connect(G_OBJECT(c25_att_combobox), "changed", G_CALLBACK(c25_att_combobox_changed), NULL);
  gtk_container_add(GTK_CONTAINER(c25_container), c25_grid);

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (can_transmit) {
#if defined (__LDESK__)
    char _label[32];
#ifdef __APPLE__
    snprintf(_label, 32, "Mic Gain");
    mic_gain_label = gtk_label_new(_label);
    gtk_widget_set_name(mic_gain_label, csslabel);
#else
    snprintf(_label, 32, "Mic\nGain");
    mic_gain_label = gtk_label_new(_label);
    gtk_widget_set_name(mic_gain_label, csslabel_smaller);
#endif
#else
    mic_gain_label = gtk_label_new("Mic");
    gtk_widget_set_name(mic_gain_label, csslabel);
#endif
    gtk_widget_set_size_request(mic_gain_label, lbl_w_fix, widget_height);
    gtk_widget_set_halign(mic_gain_label, GTK_ALIGN_CENTER);
    gtk_label_set_justify(GTK_LABEL(mic_gain_label), GTK_JUSTIFY_CENTER);
    gtk_grid_attach(GTK_GRID(sliders), mic_gain_label, t1pos, 1, twidth, 1);
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    mic_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 50.0, 1.0);
    gtk_widget_set_size_request(mic_gain_scale, sl_w_fix, widget_height);
    gtk_widget_set_valign(mic_gain_scale, GTK_ALIGN_CENTER);
    gtk_range_set_increments (GTK_RANGE(mic_gain_scale), 1.0, 1.0);
    gtk_grid_attach(GTK_GRID(sliders), mic_gain_scale, s1pos, 1, swidth, 1);
    gtk_range_set_value (GTK_RANGE(mic_gain_scale), transmitter->mic_gain);

    for (float i = -12.0; i <= 50.0; i += 6.0) {
      gtk_scale_add_mark(GTK_SCALE(mic_gain_scale), i, GTK_POS_TOP, NULL);
    }

    g_signal_connect(G_OBJECT(mic_gain_scale), "value_changed", G_CALLBACK(micgain_value_changed_cb), NULL);
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#if defined (__LDESK__)

    if (device == DEVICE_HERMES_LITE2 && pa_enabled && !have_radioberry1 && !have_radioberry2) {
      drive_label = gtk_label_new("HL2:TX\nPwr (W)");
      gtk_widget_set_name(drive_label, csslabel_smaller);
    } else {
      drive_label = gtk_label_new("TX Pwr");
      gtk_widget_set_name(drive_label, csslabel);
    }

#else
    drive_label = gtk_label_new("TX Drv");
    gtk_widget_set_name(drive_label, csslabel);
#endif
    gtk_widget_set_size_request(drive_label, lbl_w_fix, widget_height);
    gtk_label_set_justify(GTK_LABEL(drive_label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(drive_label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(sliders), drive_label, t2pos, 1, twidth, 1);
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    if (device == DEVICE_HERMES_LITE2 && pa_enabled) {
      drive_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 5.0, 0.1);
    } else {
      drive_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, drive_max, 1.00);
    }

    gtk_widget_set_size_request(drive_scale, sl_w_fix, widget_height);
    gtk_widget_set_valign(drive_scale, GTK_ALIGN_CENTER);

    if (device == DEVICE_HERMES_LITE2 && pa_enabled) {
      gtk_range_set_increments (GTK_RANGE(drive_scale), 0.1, 0.1);
      gtk_range_set_value (GTK_RANGE(drive_scale), radio_get_drive() / 20);

      for (float i = 0.0; i <= 5.0; i += 0.5) {
        gtk_scale_add_mark(GTK_SCALE(drive_scale), i, GTK_POS_TOP, NULL);
      }
    } else {
      gtk_range_set_increments (GTK_RANGE(drive_scale), 1.0, 1.0);
      gtk_range_set_value (GTK_RANGE(drive_scale), radio_get_drive());
    }

    gtk_widget_show(drive_scale);
    gtk_grid_attach(GTK_GRID(sliders), drive_scale, s2pos, 1, swidth, 1);
    g_signal_connect(G_OBJECT(drive_scale), "value_changed", G_CALLBACK(drive_value_changed_cb), NULL);
  } else {
    mic_gain_label = NULL;
    mic_gain_scale = NULL;
    drive_label = NULL;
    drive_scale = NULL;
  }

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#if defined (__LDESK__)
  squelch_label = gtk_label_new("Sql");
#else
  squelch_label = gtk_label_new("Sqlch");
#endif
  gtk_widget_set_size_request(squelch_label, lbl_w_fix, widget_height);
  gtk_widget_set_name(squelch_label, csslabel);
  gtk_widget_set_halign(squelch_label, GTK_ALIGN_END);
  gtk_label_set_justify(GTK_LABEL(squelch_label), GTK_JUSTIFY_CENTER);
  gtk_widget_show(squelch_label);
  gtk_grid_attach(GTK_GRID(sliders), squelch_label, t3pos, 1, twidth, 1);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  squelch_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  gtk_widget_set_size_request(squelch_scale, sl_w_fix, widget_height);
  gtk_widget_set_valign(squelch_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments(GTK_RANGE(squelch_scale), 1.0, 1.0);
  gtk_range_set_value(GTK_RANGE(squelch_scale), active_receiver->squelch);
  gtk_widget_show(squelch_scale);
  gtk_grid_attach(GTK_GRID(sliders), squelch_scale, s3pos, 1, swidth, 1);
  squelch_signal_id = g_signal_connect(G_OBJECT(squelch_scale), "value_changed", G_CALLBACK(squelch_value_changed_cb),
                                       NULL);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  squelch_enable = gtk_check_button_new();
  gtk_widget_set_size_request(squelch_enable, 0, widget_height);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(squelch_enable), active_receiver->squelch_enable);
  gtk_widget_show(squelch_enable);
  gtk_grid_attach(GTK_GRID(sliders), squelch_enable, b3pos, 1, twidth, 1);
  gtk_widget_set_halign(squelch_enable, GTK_ALIGN_CENTER);
  g_signal_connect(squelch_enable, "toggled", G_CALLBACK(squelch_enable_cb), NULL);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#if defined (__LDESK__)

  if (can_transmit && display_sliders) {
    //-------------------------------------------------------------------------------------------
    // tune_drive_label
    tune_drive_label = gtk_label_new("TUNE\nDrv");
    gtk_widget_set_size_request(tune_drive_label, lbl_w_fix, widget_height);
    gtk_widget_set_name(tune_drive_label, "slider2_blue");
    gtk_label_set_justify(GTK_LABEL(tune_drive_label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(tune_drive_label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(sliders), tune_drive_label, t1pos, 2, twidth - 1, 1);
    gtk_widget_show(tune_drive_label);
    //-------------------------------------------------------------------------------------------
    // tune_drive_scale
    tune_drive_scale = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(tune_drive_scale), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(tune_drive_scale), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tune_drive_scale), transmitter->tune_drive);
    gtk_grid_attach(GTK_GRID(sliders), tune_drive_scale, s1pos, 2, twidth - 1, 1);
    gtk_widget_set_size_request(tune_drive_scale, 0, widget_height - 10);
    gtk_widget_set_margin_start(tune_drive_scale, 10);  // Abstand am Anfang
    gtk_widget_set_valign(tune_drive_scale, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_right(tune_drive_scale, 10);
    tune_drive_scale_signal_id = g_signal_connect(G_OBJECT(tune_drive_scale), "value_changed",
                                 G_CALLBACK(tune_drive_changed_cb), NULL);
    gtk_widget_show(tune_drive_scale);

    //-------------------------------------------------------------------------------------------
    if (n_input_devices > 0) {
      //-------------------------------------------------------------------------------------------
      // local_mic_label
      local_mic_label = gtk_label_new("Local\nMic");
      gtk_widget_set_size_request(local_mic_label, lbl_w_fix, widget_height);
      // gtk_widget_set_name(local_mic_label, csslabel_smaller);
      gtk_widget_set_name(local_mic_label, "slider2_blue");
      gtk_widget_set_halign(local_mic_label, GTK_ALIGN_CENTER);
      gtk_label_set_justify(GTK_LABEL(local_mic_label), GTK_JUSTIFY_CENTER);
      gtk_grid_attach(GTK_GRID(sliders), local_mic_label, t2pos, 2, twidth, 1);
      gtk_widget_show(local_mic_label);
      //-------------------------------------------------------------------------------------------
      // local_mic_button
      local_mic_button = gtk_check_button_new();
      gtk_widget_set_size_request(local_mic_button, 0, widget_height);
      gtk_widget_set_halign(local_mic_button, GTK_ALIGN_FILL);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (local_mic_button), transmitter->local_microphone);
      gtk_grid_attach(GTK_GRID(sliders), local_mic_button, b2pos, 2, twidth, 1);
      local_mic_toggle_signal_id = g_signal_connect(local_mic_button, "toggled", G_CALLBACK(local_mic_toggle_cb), NULL);
      gtk_widget_show(local_mic_button);
      //-------------------------------------------------------------------------------------------
      local_mic_input = gtk_combo_box_text_new();
      gtk_widget_set_name(local_mic_input, "boldlabel");
      gtk_widget_set_size_request(local_mic_input, sl_w_fix, widget_height);

      for (int i = 0; i < n_input_devices; i++) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(local_mic_input), NULL, input_devices[i].description);

        if (strcmp(transmitter->microphone_name, input_devices[i].name) == 0) {
          gtk_combo_box_set_active(GTK_COMBO_BOX(local_mic_input), i);
        }
      }

      // If the combo box shows no device, take the first one
      // AND set the mic.name to that device name.
      // This situation occurs if the local microphone device in the props
      // file is no longer present

      if (gtk_combo_box_get_active(GTK_COMBO_BOX(local_mic_input))  < 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(local_mic_input), 0);
        g_strlcpy(transmitter->microphone_name, input_devices[0].name, sizeof(transmitter->microphone_name));
      }

      gtk_grid_attach(GTK_GRID(sliders), local_mic_input, s2pos, 2, swidth, 1); // Zeile 0, Spalte 1
      gtk_widget_set_valign(local_mic_input, GTK_ALIGN_CENTER);
      local_mic_input_signal_id = g_signal_connect(local_mic_input, "changed", G_CALLBACK(local_input_changed_cb), NULL);
      gtk_widget_show(local_mic_input);
    }

    //-------------------------------------------------------------------------------------------
    bbcompr_label = gtk_label_new("BBand\nPROC");
    gtk_widget_set_size_request(bbcompr_label, lbl_w_fix, widget_height);
    gtk_widget_set_name(bbcompr_label, "slider2_blue");
    gtk_label_set_justify(GTK_LABEL(bbcompr_label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(bbcompr_label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(sliders), bbcompr_label, t3pos, 2, twidth, 1);
    gtk_widget_show(bbcompr_label);
    //-------------------------------------------------------------------------------------------
    bbcompr_scale = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(bbcompr_scale), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(bbcompr_scale), TRUE);
    gtk_widget_set_size_request(bbcompr_scale, 0, widget_height - 10);
    gtk_widget_set_valign(bbcompr_scale, GTK_ALIGN_CENTER);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(bbcompr_scale), (double)transmitter->compressor_level);
    gtk_grid_attach(GTK_GRID(sliders), bbcompr_scale, s3pos, 2, twidth - 1, 1);
    bbcompr_scale_signal_id = g_signal_connect(G_OBJECT(bbcompr_scale), "value_changed",
                              G_CALLBACK(bbcompr_scale_changed_cb), NULL);
    gtk_widget_show(bbcompr_scale);
    //-------------------------------------------------------------------------------------------
    lev_label = gtk_label_new("Lev");
    gtk_widget_set_size_request(lev_label, lbl_w_fix, widget_height);
    gtk_widget_set_name(lev_label, "slider2_blue");
    gtk_label_set_justify(GTK_LABEL(lev_label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(lev_label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(sliders), lev_label, s3pos + 1, 2, twidth - 1, 1);
    gtk_widget_show(lev_label);
    //-------------------------------------------------------------------------------------------
    lev_scale = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(lev_scale), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(lev_scale), TRUE);
    gtk_widget_set_size_request(lev_scale, 0, widget_height - 10);
    gtk_widget_set_valign(lev_scale, GTK_ALIGN_CENTER);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(lev_scale), (double)transmitter->lev_gain);
    gtk_grid_attach(GTK_GRID(sliders), lev_scale, s3pos + 2, 2, twidth - 1, 1);
    lev_scale_signal_id = g_signal_connect(G_OBJECT(lev_scale), "value_changed", G_CALLBACK(lev_scale_changed_cb), NULL);
    gtk_widget_show(lev_scale);
    //-------------------------------------------------------------------------------------------
    preamp_label = gtk_label_new("Mic\nPreA");
    gtk_widget_set_size_request(preamp_label, lbl_w_fix, widget_height);
    gtk_widget_set_name(preamp_label, "slider2_blue");
    gtk_label_set_justify(GTK_LABEL(preamp_label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(preamp_label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(sliders), preamp_label, s1pos + 1, 2, twidth - 1, 1);
    gtk_widget_show(preamp_label);
    //-------------------------------------------------------------------------------------------
    preamp_scale = gtk_spin_button_new_with_range(1.0, 20.0, 1.0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(preamp_scale), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(preamp_scale), TRUE);
    gtk_widget_set_size_request(preamp_scale, 0, widget_height - 10);
    gtk_widget_set_valign(preamp_scale, GTK_ALIGN_CENTER);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(preamp_scale), (double)transmitter->addgain_gain);
    gtk_grid_attach(GTK_GRID(sliders), preamp_scale, s1pos + 2, 2, twidth - 1, 1);
    preamp_scale_signal_id = g_signal_connect(G_OBJECT(preamp_scale), "value_changed", G_CALLBACK(preamp_scale_changed_cb),
                             NULL);
    gtk_widget_show(preamp_scale);

    // sanity check, if DIGIMODE selected set BBCOMPR and LEV inactive
    if (selected_mode == modeDIGL || selected_mode == modeDIGU) {
      gtk_widget_set_sensitive(preamp_scale, FALSE);
      gtk_widget_set_name(preamp_label, "slider2_red");
      gtk_widget_set_sensitive(bbcompr_scale, FALSE);
      gtk_widget_set_name(bbcompr_label, "slider2_red");
      gtk_widget_set_sensitive(lev_scale, FALSE);
      gtk_widget_set_name(lev_label, "slider2_red");
      gtk_widget_queue_draw(sliders);
      // gtk_widget_queue_draw(bbcompr_scale);
      // gtk_widget_queue_draw(lev_scale);
      // gtk_widget_queue_draw(preamp_scale);
    }
  } else {
    tune_drive_label = NULL;
    tune_drive_scale = NULL;
    local_mic_label = NULL;
    local_mic_button = NULL;
    local_mic_input = NULL;
    bbcompr_scale = NULL;
    lev_label = NULL;
    lev_scale = NULL;
  }

#endif
  return sliders;
}
