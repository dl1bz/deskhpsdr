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
#include <pthread.h>
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
#include "toolset.h"

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
static gulong    agc_scale_signal_id;
static GtkWidget *attenuation_label = NULL;
static GtkWidget *attenuation_scale = NULL;
static GtkWidget *c25_box = NULL;
static GtkWidget *c25_att_combobox = NULL;
static GtkWidget *c25_att_label = NULL;
static GtkWidget *mic_gain_label;
static GtkWidget *mic_gain_scale;
static gulong    mic_gain_scale_signal_id;
static GtkWidget *drive_label;
static GtkWidget *drive_scale;
static gulong    drive_scale_signal_id;
static GtkWidget *squelch_label;
static GtkWidget *squelch_scale;
static gulong     squelch_signal_id;
static GtkWidget *squelch_enable;
static GtkWidget *tune_drive_label;
static GtkWidget *tune_drive_scale;
static gulong tune_drive_scale_signal_id;
static GtkWidget *local_mic_input;
static gulong local_mic_input_signal_id;
static GtkWidget *local_mic_label;
static GtkWidget *local_mic_button;
static gulong local_mic_toggle_signal_id;
static GtkWidget *autogain_btn;
static gulong autogain_btn_signal_id;
static GtkWidget *bbcompr_scale;
static gulong bbcompr_scale_signal_id;
static GtkWidget *bbcompr_label;
static GtkWidget *bbcompr_btn;
static gulong bbcompr_btn_signal_id;
static GtkWidget *lev_label;
static GtkWidget *lev_scale;
static GtkWidget *lev_btn;
static gulong lev_btn_signal_id;
static gulong lev_scale_signal_id;
static GtkWidget *preamp_label;
static GtkWidget *preamp_btn;
static gulong preamp_btn_signal_id;
static GtkWidget *preamp_scale;
static gulong preamp_scale_signal_id;

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

void sliders_hide_row(int row) {
  if (can_transmit) {
    for (int col = 0; col < 24; col++) {
      GtkWidget *widget = gtk_grid_get_child_at(GTK_GRID(sliders), col, row);

      if (widget) {
        gtk_widget_hide(widget);  // Das Widget ausblenden
      }
    }
  }
}

void sliders_show_row(int row) {
  if (can_transmit) {
    for (int col = 0; col < 24; col++) {
      GtkWidget *widget = gtk_grid_get_child_at(GTK_GRID(sliders), col, row);

      if (widget) {
        gtk_widget_show(widget);  // Das Widget ausblenden
      }
    }
  }
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

    if (GTK_IS_SPIN_BUTTON(agc_scale)) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(agc_scale), (double)active_receiver->agc_gain);
    } else if (GTK_IS_RANGE(agc_scale)) {
      gtk_range_set_value (GTK_RANGE(agc_scale), (double)active_receiver->agc_gain);
    }

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

    if (c25_box != NULL) { gtk_widget_show(c25_box); }

    //
    // There is no step attenuator visible any more. Set to zero
    //
    set_attenuation_value(0.0);
    set_rf_gain(active_receiver->id, 0.0); // this will be a no-op
  } else {
    if (attenuation_label != NULL) { gtk_widget_show(attenuation_label); }

    if (rf_gain_label != NULL) { gtk_widget_show(rf_gain_label); }

    if (attenuation_scale != NULL) { gtk_widget_show(attenuation_scale); }

    if (c25_box != NULL) { gtk_widget_hide(c25_box); }
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
  if (GTK_IS_SPIN_BUTTON(widget)) {
    active_receiver->agc_gain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  } else if (GTK_IS_RANGE(widget)) {
    active_receiver->agc_gain = gtk_range_get_value(GTK_RANGE(widget));
  }

  rx_set_agc(active_receiver);
}

void set_agc_gain(int rx, double value) {
  //t_print("%s value=%f\n",__FUNCTION__, value);
  if (rx >= receivers) { return; }

  receiver[rx]->agc_gain = value;
  rx_set_agc(receiver[rx]);

  if (display_sliders && active_receiver->id == rx) {
    g_signal_handler_block(G_OBJECT(agc_scale), agc_scale_signal_id);

    if (GTK_IS_SPIN_BUTTON(agc_scale)) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(agc_scale), (double)receiver[rx]->agc_gain);
    } else if (GTK_IS_RANGE(agc_scale)) {
      gtk_range_set_value (GTK_RANGE(agc_scale), (double)receiver[rx]->agc_gain);
    }

    g_signal_handler_unblock(G_OBJECT(agc_scale), agc_scale_signal_id);
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

// Callback, um rf_gain_slider-Wert im Mainthread zu setzen
gboolean update_rf_gain_slider_value(gpointer data) {
  double value = *(double *)data;
  gtk_range_set_value(GTK_RANGE(rf_gain_scale), value);
  g_free(data);
  return FALSE;
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
    if (pthread_equal(pthread_self(), deskhpsdr_main_thread)) {
      gtk_range_set_value (GTK_RANGE(rf_gain_scale), adc[rxadc].gain);
    } else {
      // wir brauchen ein Callback um den rf_gain_slider nur im Hauptthread zu aktualisieren
      double *val = g_new(double, 1);
      *val = adc[rxadc].gain;
      g_idle_add(update_rf_gain_slider_value, val);
    }
  } else {
    // Falls wir NICHT im Main Thread sind, dürfen wir show_popup_slider()
    // nicht aus einem anderen "fremden" Thread aufrufen, anderenfalls App-Crash !!!
    if (!pthread_equal(pthread_self(), deskhpsdr_main_thread)) {
      return;
    }

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
    if (optimize_for_touchscreen) {
      transmitter->mic_gain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
    } else {
      transmitter->mic_gain = gtk_range_get_value(GTK_RANGE(widget));
    }

    tx_set_mic_gain(transmitter);
    g_idle_add(ext_vfo_update, NULL);
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
    tx_set_mic_gain(transmitter);

    if (display_sliders) {
      g_signal_handler_block(G_OBJECT(mic_gain_scale), mic_gain_scale_signal_id);

      if (optimize_for_touchscreen) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(mic_gain_scale), value);
      } else {
        gtk_range_set_value (GTK_RANGE(mic_gain_scale), value);
      }

      g_signal_handler_unblock(G_OBJECT(mic_gain_scale), mic_gain_scale_signal_id);
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

    g_signal_handler_block(G_OBJECT(drive_scale), drive_scale_signal_id);

    if (optimize_for_touchscreen) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(drive_scale), value);
    } else {
      gtk_range_set_value (GTK_RANGE(drive_scale), value);
    }

    g_signal_handler_unblock(G_OBJECT(drive_scale), drive_scale_signal_id);
  } else {
    show_popup_slider(DRIVE, 0, 0.0, drive_max, 1.0, value, "TX Drive");
  }
}

static void drive_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = 0.0;

  if (GTK_IS_SPIN_BUTTON(widget)) {
    value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  } else if (GTK_IS_RANGE(widget)) {
    value = gtk_range_get_value(GTK_RANGE(widget));
  }

  // double value = gtk_range_get_value(GTK_RANGE(drive_scale));

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

  if (GTK_IS_SPIN_BUTTON(widget)) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), value);
  } else if (GTK_IS_RANGE(widget)) {
    gtk_range_set_value (GTK_RANGE(widget), value);
  }
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

#if defined (__CPYMODE__)
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

static void bbcompr_btn_toggle_cb(GtkWidget *widget, gpointer data) {
  int v = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  int mode = vfo_get_tx_mode();

  if (mode == modeDIGL || mode == modeDIGU) {
    transmitter->compressor = 0;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), transmitter->compressor);
  } else {
    transmitter->compressor = v;
  }

  mode_settings[mode].compressor = v;
  copy_mode_settings(mode);
  tx_set_compressor(transmitter);
  g_idle_add(ext_vfo_update, NULL);
}

static void lev_btn_toggle_cb(GtkWidget *widget, gpointer data) {
  int v = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  int mode = vfo_get_tx_mode();

  if (mode == modeDIGL || mode == modeDIGU) {
    transmitter->lev_enable = 0;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), transmitter->lev_enable);
  } else {
    transmitter->lev_enable = v;
  }

  mode_settings[mode].lev_enable = transmitter->lev_enable;
  copy_mode_settings(mode);
  tx_set_compressor(transmitter);
  g_idle_add(ext_vfo_update, NULL);
}

static void preamp_btn_toggle_cb(GtkWidget *widget, gpointer data) {
  int v = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  int mode = vfo_get_tx_mode();

  if (mode == modeDIGL || mode == modeDIGU) {
    transmitter->addgain_enable = 0;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), transmitter->addgain_enable);
  } else {
    transmitter->addgain_enable = v;
  }

  tx_set_mic_gain(transmitter);
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

void update_slider_local_mic_input(int src) {
  if (display_sliders) {
    // t_print("%s: local_mic_input = %d src = %d\n", __FUNCTION__, gtk_combo_box_get_active(GTK_COMBO_BOX(local_mic_input)), src);
    if (src != gtk_combo_box_get_active(GTK_COMBO_BOX(local_mic_input))) {
      g_signal_handler_block(G_OBJECT(local_mic_input), local_mic_input_signal_id);

      if (strcmp(transmitter->microphone_name, input_devices[src].name) == 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(local_mic_input), src);
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
}

void update_slider_bbcompr_button(gboolean show_widget) {
  if (display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (bbcompr_btn), bbcompr_btn_signal_id);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (bbcompr_btn), transmitter->compressor);

    if (show_widget) {
      gtk_widget_set_sensitive(bbcompr_btn, TRUE);
    } else {
      gtk_widget_set_sensitive(bbcompr_btn, FALSE);
    }

    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (bbcompr_btn), bbcompr_btn_signal_id);
    gtk_widget_queue_draw(bbcompr_btn);
  }
}

void update_slider_lev_button(gboolean show_widget) {
  if (display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (lev_btn), lev_btn_signal_id);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (lev_btn), transmitter->lev_enable);

    if (show_widget) {
      gtk_widget_set_sensitive(lev_btn, TRUE);
    } else {
      gtk_widget_set_sensitive(lev_btn, FALSE);
    }

    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (lev_btn), lev_btn_signal_id);
    gtk_widget_queue_draw(lev_btn);
  }
}

void update_slider_preamp_button(gboolean show_widget) {
  if (display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (preamp_btn), preamp_btn_signal_id);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (preamp_btn), transmitter->addgain_enable);

    if (show_widget) {
      gtk_widget_set_sensitive(preamp_btn, TRUE);
    } else {
      gtk_widget_set_sensitive(preamp_btn, FALSE);
    }

    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (preamp_btn), preamp_btn_signal_id);
    gtk_widget_queue_draw(preamp_btn);
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
      gtk_widget_set_name(tune_drive_label, "label2_grey");
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
    } else {
      gtk_widget_set_sensitive(bbcompr_scale, FALSE);
    }

    g_signal_handler_unblock(G_OBJECT(bbcompr_scale), bbcompr_scale_signal_id);
    gtk_widget_queue_draw(bbcompr_scale);
  }
}

void update_slider_lev_scale(gboolean show_widget) {
  if (display_sliders) {
    g_signal_handler_block(G_OBJECT(lev_scale), lev_scale_signal_id);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(lev_scale), transmitter->lev_gain);

    if (show_widget && transmitter->lev_enable) {
      gtk_widget_set_sensitive(lev_scale, TRUE);
    } else {
      gtk_widget_set_sensitive(lev_scale, FALSE);
    }

    g_signal_handler_unblock(G_OBJECT(lev_scale), lev_scale_signal_id);
    gtk_widget_queue_draw(lev_scale);
  }
}

void update_slider_preamp_scale(gboolean show_widget) {
  if (display_sliders) {
    g_signal_handler_block(G_OBJECT(preamp_scale), preamp_scale_signal_id);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(preamp_scale), transmitter->addgain_gain);

    if (show_widget && transmitter->addgain_enable) {
      gtk_widget_set_sensitive(preamp_scale, TRUE);
    } else {
      gtk_widget_set_sensitive(preamp_scale, FALSE);
    }

    g_signal_handler_unblock(G_OBJECT(preamp_scale), preamp_scale_signal_id);
    gtk_widget_queue_draw(preamp_scale);
    // gtk_widget_queue_draw(preamp_label);
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
  // width = my_width - 50;
  width = my_width;
  int selected_mode = vfo[active_receiver->id].mode;
  int widget_height = 0;
  height = my_height;
  widget_height = height / 2;

  if (can_transmit && display_extra_sliders) {
    widget_height = height / 3;
  }

  t_print("sliders_init: width=%d height=%d widget_height=%d\n", width, height, widget_height);
  const char *csslabel;
  int tpix;

  if (width < 1441) {
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

  int box_left_width = (int)floor(my_width / 3); // abrunden auf ganze Zahl
  int box_right_width = box_left_width;
  int box_middle_width = my_width - box_left_width - box_right_width - 50;
  t_print("%s: my_width = %d box_left_width = %d box_middle_width = %d box_right_width = %d (summe = %d)\n",
          __FUNCTION__, my_width, box_left_width, box_middle_width, box_right_width,
          box_left_width + box_middle_width + box_right_width);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  /* Basic layout GRID "sliders"
  *
  * |--------------------------------------- width ----------------------------------------------|
  * +------------------------------+------------------------------+------------------------------+ -+-
  * | VOLUME                       |  AGC                         | RxPGA                        |  |
  * +------------------------------+------------------------------+------------------------------+  |
  * | MIC GAIN                     |  TX PWR                      | SQL                          |  height
  * +------------------------------+------------------------------+------------------------------+  |
  * | TUNE DRV    | MIC PREAMP.    |  LOC MIC                     | BBPROC       |  LEVELLER     |  |
  * +------------------------------+------------------------------+------------------------------+ -+-
  *
  */
  sliders = gtk_grid_new();
  gtk_widget_set_size_request (sliders, width, height);
  gtk_grid_set_row_homogeneous(GTK_GRID(sliders), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(sliders), FALSE);
  gtk_widget_set_margin_top(sliders, 0);    // Kein Abstand oben
  gtk_widget_set_margin_bottom(sliders, 0); // Kein Abstand unten
  gtk_widget_set_margin_start(sliders, 3);  // Kein Abstand am Anfang
  gtk_widget_set_margin_end(sliders, 3);    // Kein Abstand am Ende
  // Safety: Gain und Att dürfen nicht gleichzeitig aktiv sein
  g_return_val_if_fail(!(have_rx_gain && have_rx_att), sliders);
  //-----------------------------------------------------------------------------------------------------------
  // Hauptcontainer: horizontale Box für Volume
  GtkWidget *box_Z1_left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(box_Z1_left, box_left_width, widget_height);
  //-----------------------------------------------------------------------------------------------------------
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  af_gain_label = gtk_label_new("Volume");
  gtk_widget_set_name(af_gain_label, "boldlabel_border_blue");
  // Label breiter erzwingen
  gtk_widget_set_size_request(af_gain_label, 105, -1);  // z.B. 105px
  gtk_widget_set_margin_top(af_gain_label, 5);
  gtk_widget_set_margin_bottom(af_gain_label, 5);
  gtk_widget_set_halign(af_gain_label, GTK_ALIGN_START);
  gtk_widget_set_valign(af_gain_label, GTK_ALIGN_CENTER);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  af_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -40.0, 0.0, 1.0);
  gtk_widget_set_tooltip_text(af_gain_scale, "Set AF Volume");
  gtk_widget_set_margin_end(af_gain_scale, 0);  // rechter Rand (Ende)
  gtk_widget_set_hexpand(af_gain_scale, FALSE);  // fülle Box nicht nach rechts
  gtk_range_set_increments (GTK_RANGE(af_gain_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(af_gain_scale), active_receiver->volume);

  for (float i = -40.0; i <= 0.0; i += 5.0) {
    gtk_scale_add_mark(GTK_SCALE(af_gain_scale), i, GTK_POS_TOP, NULL);
  }

  g_signal_connect(G_OBJECT(af_gain_scale), "value_changed", G_CALLBACK(afgain_value_changed_cb), NULL);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(box_Z1_left), af_gain_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box_Z1_left), af_gain_scale, TRUE, TRUE, 0);
  // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
  gtk_grid_attach(GTK_GRID(sliders), box_Z1_left, 0, 0, 1, 1);  // Zeile 0 Spalte 0
  //-----------------------------------------------------------------------------------------------------------
  // Hauptcontainer: horizontale Box für AGC
  GtkWidget *box_Z1_middle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(box_Z1_middle, box_middle_width, widget_height);
  //-----------------------------------------------------------------------------------------------------------
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  agc_gain_label = gtk_label_new("AGC");
  gtk_widget_set_name(agc_gain_label, "boldlabel_border_blue");
  // Label breiter erzwingen
  gtk_widget_set_size_request(agc_gain_label, 90, -1);  // z.B. 100px
  gtk_widget_set_margin_top(agc_gain_label, 5);
  gtk_widget_set_margin_bottom(agc_gain_label, 5);
  gtk_widget_set_margin_end(agc_gain_label, 0);    // rechter Rand (Ende)
  gtk_widget_set_margin_start(agc_gain_label, 0);    // linker Rand (Anfang)
  gtk_widget_set_halign(agc_gain_label, GTK_ALIGN_START);
  gtk_widget_set_valign(agc_gain_label, GTK_ALIGN_CENTER);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(box_Z1_middle), agc_gain_label, FALSE, FALSE, 0);

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (optimize_for_touchscreen) {
    agc_scale = gtk_spin_button_new_with_range(-20.0, 120.0, 1.0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(agc_scale), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(agc_scale), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(agc_scale), (double)active_receiver->agc_gain);
    gtk_widget_set_margin_top(agc_scale, 5);
    gtk_widget_set_margin_bottom(agc_scale, 5);
    gtk_widget_set_margin_start(agc_scale, 3);
    gtk_widget_set_margin_end(agc_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(agc_scale, FALSE);  // fülle Box nicht nach rechts
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_middle), agc_scale, FALSE, FALSE, 0);
  } else {
    agc_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -20.0, 120.0, 1.0);
    gtk_range_set_increments (GTK_RANGE(agc_scale), 1.0, 1.0);
    gtk_range_set_value (GTK_RANGE(agc_scale), (double)active_receiver->agc_gain);

    for (double agc_mark = -20.0; agc_mark <= 120.0; agc_mark += 20.0) {
      gtk_scale_add_mark(GTK_SCALE(agc_scale), agc_mark, GTK_POS_TOP, NULL);
    }

    gtk_widget_set_tooltip_text(agc_scale,
                                "AGC of the currently active receiver.\n"
                                "Adjust coral colored horizontal line\n"
                                "slightly above the noise floor.");
    gtk_widget_set_margin_end(agc_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(agc_scale, FALSE);  // fülle Box nicht nach rechts
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_middle), agc_scale, TRUE, TRUE, 0);
  }

  agc_scale_signal_id = g_signal_connect(G_OBJECT(agc_scale), "value_changed", G_CALLBACK(agcgain_value_changed_cb),
                                         NULL);
  // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
  gtk_grid_attach(GTK_GRID(sliders), box_Z1_middle, 1, 0, 1, 1);  // Zeile 0 Spalte 1

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (have_rx_gain) {
#if defined (__AUTOG__)
    //-----------------------------------------------------------------------------------------------------------
    // Hauptcontainer: horizontale Box für RF Gain
    GtkWidget *box_Z1_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z1_right, box_right_width, widget_height);
    //-----------------------------------------------------------------------------------------------------------

    if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
      autogain_btn = gtk_toggle_button_new_with_label("RxPGA");
      gtk_widget_set_tooltip_text(autogain_btn, "AutoGain ON/OFF");
    } else {
      autogain_btn = gtk_toggle_button_new_with_label("RF Gain");
    }

    gtk_widget_set_name(autogain_btn, "medium_toggle_button");
    // Label breiter erzwingen
    gtk_widget_set_size_request(autogain_btn, 90, -1);  // z.B. 100px
    gtk_widget_set_margin_top(autogain_btn, 5);
    gtk_widget_set_margin_bottom(autogain_btn, 5);
    gtk_widget_set_margin_end(autogain_btn, 0);    // rechter Rand (Ende)
    gtk_widget_set_margin_start(autogain_btn, 0);    // linker Rand (Anfang)
    gtk_widget_set_halign(autogain_btn, GTK_ALIGN_START);
    gtk_widget_set_valign(autogain_btn, GTK_ALIGN_CENTER);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autogain_btn), autogain_enabled);
    // begin label definition inside button
    rf_gain_label = gtk_bin_get_child(GTK_BIN(autogain_btn));
    g_signal_connect(autogain_btn, "toggled", G_CALLBACK(autogain_enable_cb), NULL);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_right), autogain_btn, FALSE, FALSE, 0);
#else
    //-----------------------------------------------------------------------------------------------------------
    // Hauptcontainer: horizontale Box für RF Gain
    GtkWidget *box_Z1_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z1_right, box_right_width, widget_height);
    //-----------------------------------------------------------------------------------------------------------

    if (device == DEVICE_HERMES_LITE2) {
      rf_gain_label = gtk_label_new("RxPGA");
    } else {
      rf_gain_label = gtk_label_new("RF Gain");
    }

    gtk_widget_set_name(rf_gain_label, "boldlabel_border_blue");
    // Label breiter erzwingen
    gtk_widget_set_size_request(rf_gain_label, 90, -1);  // z.B. 100px
    gtk_widget_set_margin_top(rf_gain_label, 5);
    gtk_widget_set_margin_bottom(rf_gain_label, 5);
    gtk_widget_set_margin_end(rf_gain_label, 0);    // rechter Rand (Ende)
    gtk_widget_set_margin_start(rf_gain_label, 0);    // linker Rand (Anfang)
    gtk_widget_set_halign(rf_gain_label, GTK_ALIGN_START);
    gtk_widget_set_valign(rf_gain_label, GTK_ALIGN_CENTER);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_right), rf_gain_label, FALSE, FALSE, 0);
#endif
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    rf_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, adc[0].min_gain, adc[0].max_gain, 1.0);
    gtk_range_set_value (GTK_RANGE(rf_gain_scale), adc[0].gain);
    gtk_range_set_increments (GTK_RANGE(rf_gain_scale), 1.0, 1.0);

    for (float i = adc[0].min_gain; i <= adc[0].max_gain; i += 6.0) {
      gtk_scale_add_mark(GTK_SCALE(rf_gain_scale), i, GTK_POS_TOP, NULL);
    }

    g_signal_connect(G_OBJECT(rf_gain_scale), "value_changed", G_CALLBACK(rf_gain_value_changed_cb), NULL);
    gtk_widget_set_margin_end(rf_gain_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(rf_gain_scale, FALSE);  // fülle Box nicht nach rechts
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_right), rf_gain_scale, TRUE, TRUE, 0);
    gtk_grid_attach(GTK_GRID(sliders), box_Z1_right, 2, 0, 1, 1);  // Zeile 0 Spalte 2
  } else {
    rf_gain_label = NULL;
    autogain_btn = NULL;
    rf_gain_scale = NULL;
  }

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (have_rx_att) {
    //-----------------------------------------------------------------------------------------------------------
    // Hauptcontainer: horizontale Box für RF Gain
    GtkWidget *box_Z1_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z1_right, box_right_width, widget_height);
    //-----------------------------------------------------------------------------------------------------------
    attenuation_label = gtk_label_new("ATT");
    gtk_widget_set_name(attenuation_label, "boldlabel_border_blue");
    // Label breiter erzwingen
    gtk_widget_set_size_request(attenuation_label, 90, -1);  // z.B. 100px
    gtk_widget_set_margin_top(attenuation_label, 5);
    gtk_widget_set_margin_bottom(attenuation_label, 5);
    gtk_widget_set_margin_end(attenuation_label, 0);    // rechter Rand (Ende)
    gtk_widget_set_margin_start(attenuation_label, 0);    // linker Rand (Anfang)
    gtk_widget_set_halign(attenuation_label, GTK_ALIGN_START);
    gtk_widget_set_valign(attenuation_label, GTK_ALIGN_CENTER);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_right), attenuation_label, FALSE, FALSE, 0);
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    attenuation_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 31.0, 1.0);
    gtk_range_set_value (GTK_RANGE(attenuation_scale), adc[active_receiver->adc].attenuation);
    gtk_range_set_increments (GTK_RANGE(attenuation_scale), 1.0, 1.0);
    g_signal_connect(G_OBJECT(attenuation_scale), "value_changed", G_CALLBACK(attenuation_value_changed_cb), NULL);
    gtk_widget_set_margin_end(attenuation_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(attenuation_scale, FALSE);  // fülle Box nicht nach rechts
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_right), attenuation_scale, TRUE, TRUE, 0);
    gtk_grid_attach(GTK_GRID(sliders), box_Z1_right, 2, 0, 1, 1);  // Zeile 0 Spalte 2
  } else {
    attenuation_label = NULL;
    attenuation_scale = NULL;
  }

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  // C25: eine Box (Label kompakt, Combo flexibel) in Z0/S2
  c25_att_label = gtk_label_new("Att/Pre");
  gtk_widget_set_name(c25_att_label, csslabel);
  gtk_widget_set_margin_top(c25_att_label, 5);
  gtk_widget_set_margin_bottom(c25_att_label, 5);
  gtk_label_set_xalign(GTK_LABEL(c25_att_label), 1.0);
  gtk_widget_set_valign(c25_att_label, GTK_ALIGN_CENTER);
  c25_att_combobox = gtk_combo_box_text_new();
  gtk_widget_set_name(c25_att_combobox, csslabel);
  gtk_widget_set_hexpand(c25_att_combobox, TRUE);
  gtk_widget_set_halign(c25_att_combobox, GTK_ALIGN_FILL);
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "-36", "-36 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "-24", "-24 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "-12", "-12 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "0",   "  0 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "18",  "+18 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_att_combobox), "36",  "+36 dB");
  g_signal_connect(G_OBJECT(c25_att_combobox), "changed",
                   G_CALLBACK(c25_att_combobox_changed), NULL);
  c25_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start(GTK_BOX(c25_box), c25_att_label,     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(c25_box), c25_att_combobox,  TRUE,  TRUE,  0);
  gtk_widget_set_hexpand(c25_box, TRUE);
  gtk_widget_set_halign(c25_box, GTK_ALIGN_FILL);
  gtk_widget_set_valign(c25_box, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(sliders), c25_box, /*col*/ 2, /*row*/ 0, /*w*/ 1, /*h*/ 1);

  if (filter_board == CHARLY25) {
    update_c25_att();
    gtk_widget_show(c25_box);
  } else {
    gtk_widget_hide(c25_box);
  }

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (can_transmit) {
    // Hauptcontainer: horizontale Box für RF Gain
    GtkWidget *box_Z2_left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z2_left, box_left_width, widget_height);
    //-----------------------------------------------------------------------------------------------------------
    char _label[32];
    snprintf(_label, 32, "Mic Gain");
    mic_gain_label = gtk_label_new(_label);
    // gtk_widget_set_size_request(mic_gain_label, 2 * widget_height, widget_height - 10);
    gtk_widget_set_name(mic_gain_label, "boldlabel_border_blue");
    // Label breiter erzwingen
    gtk_widget_set_size_request(mic_gain_label, 105, -1);  // z.B. 100px
    gtk_widget_set_margin_top(mic_gain_label, 5);
    gtk_widget_set_margin_bottom(mic_gain_label, 5);
    gtk_widget_set_halign(mic_gain_label, GTK_ALIGN_START);
    gtk_widget_set_valign(mic_gain_label, GTK_ALIGN_CENTER);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z2_left), mic_gain_label, FALSE, FALSE, 0);

    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    if (optimize_for_touchscreen) {
      mic_gain_scale = gtk_spin_button_new_with_range(-12.0, 50.0, 1.0);
      gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(mic_gain_scale), TRUE);
      gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(mic_gain_scale), TRUE);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(mic_gain_scale), (double)transmitter->mic_gain);
      gtk_widget_set_margin_top(mic_gain_scale, 5);
      gtk_widget_set_margin_bottom(mic_gain_scale, 5);
      gtk_widget_set_margin_start(mic_gain_scale, 3);
      gtk_widget_set_margin_end(mic_gain_scale, 0);  // rechter Rand (Ende)
      gtk_widget_set_hexpand(mic_gain_scale, FALSE);  // fülle Box nicht nach rechts
      gtk_box_pack_start(GTK_BOX(box_Z2_left), mic_gain_scale, FALSE, FALSE, 0);
    } else {
      mic_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 50.0, 1.0);
      gtk_widget_set_margin_end(mic_gain_scale, 0);  // rechter Rand (Ende)
      gtk_widget_set_hexpand(mic_gain_scale, FALSE);  // fülle Box nicht nach rechts
      gtk_range_set_increments (GTK_RANGE(mic_gain_scale), 1.0, 1.0);
      gtk_range_set_value (GTK_RANGE(mic_gain_scale), transmitter->mic_gain);

      for (float i = -12.0; i <= 50.0; i += 6.0) {
        gtk_scale_add_mark(GTK_SCALE(mic_gain_scale), i, GTK_POS_TOP, NULL);
      }

      gtk_box_pack_start(GTK_BOX(box_Z2_left), mic_gain_scale, TRUE, TRUE, 0);
    }

    gtk_widget_set_tooltip_text(mic_gain_scale, "Set Mic Gain in db");
    mic_gain_scale_signal_id = g_signal_connect(G_OBJECT(mic_gain_scale), "value_changed",
                               G_CALLBACK(micgain_value_changed_cb), NULL);
    gtk_grid_attach(GTK_GRID(sliders), box_Z2_left, 0, 1, 1, 1);  // Spalte 0 Zeile 1
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    //-----------------------------------------------------------------------------------------------------------
    // Hauptcontainer: horizontale Box für TX Pwr
    GtkWidget *box_Z2_middle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z2_middle, box_middle_width, widget_height);

    //-----------------------------------------------------------------------------------------------------------
    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && pa_enabled && !have_radioberry1
        && !have_radioberry2) {
      drive_label = gtk_label_new("TXPwr(W)");
    } else {
      drive_label = gtk_label_new("TXPwr(%)");
    }

    gtk_widget_set_name(drive_label, "boldlabel_border_blue");
    // Label breiter erzwingen
    gtk_widget_set_size_request(drive_label, 90, -1);
    gtk_widget_set_margin_top(drive_label, 5);
    gtk_widget_set_margin_bottom(drive_label, 5);
    gtk_widget_set_margin_end(drive_label, 0);    // rechter Rand (Ende)
    gtk_widget_set_margin_start(drive_label, 0);    // linker Rand (Anfang)
    gtk_widget_set_halign(drive_label, GTK_ALIGN_START);
    gtk_widget_set_valign(drive_label, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box_Z2_middle), drive_label, FALSE, FALSE, 0);
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    if (device == DEVICE_HERMES_LITE2 && pa_enabled) {
      if (optimize_for_touchscreen) {
        drive_scale = gtk_spin_button_new_with_range(0.0, 5.0, 0.1);
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(drive_scale), TRUE);
        gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(drive_scale), TRUE);
        gtk_widget_set_margin_top(drive_scale, 5);
        gtk_widget_set_margin_bottom(drive_scale, 5);
        gtk_widget_set_margin_start(drive_scale, 3);
        gtk_widget_set_margin_end(drive_scale, 0);  // rechter Rand (Ende)
        gtk_widget_set_hexpand(drive_scale, FALSE);  // fülle Box nicht nach rechts
        gtk_box_pack_start(GTK_BOX(box_Z2_middle), drive_scale, FALSE, FALSE, 0);
      } else {
        drive_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 5.0, 0.1);
        gtk_widget_set_margin_end(drive_scale, 0);  // rechter Rand (Ende)
        gtk_widget_set_hexpand(drive_scale, FALSE);  // fülle Box nicht nach rechts
        gtk_box_pack_start(GTK_BOX(box_Z2_middle), drive_scale, TRUE, TRUE, 0);
      }

      gtk_widget_set_tooltip_text(drive_scale, "Set TX Pwr in W");
    } else {
      if (optimize_for_touchscreen) {
        drive_scale = gtk_spin_button_new_with_range(0.0, drive_max, 1.00);
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(drive_scale), TRUE);
        gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(drive_scale), TRUE);
        gtk_widget_set_margin_top(drive_scale, 5);
        gtk_widget_set_margin_bottom(drive_scale, 5);
        gtk_widget_set_margin_start(drive_scale, 3);
        gtk_widget_set_margin_end(drive_scale, 0);  // rechter Rand (Ende)
        gtk_widget_set_hexpand(drive_scale, FALSE);  // fülle Box nicht nach rechts
        gtk_box_pack_start(GTK_BOX(box_Z2_middle), drive_scale, FALSE, FALSE, 0);
      } else {
        drive_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, drive_max, 1.00);
        gtk_widget_set_margin_end(drive_scale, 0);  // rechter Rand (Ende)
        gtk_widget_set_hexpand(drive_scale, FALSE);  // fülle Box nicht nach rechts
        gtk_box_pack_start(GTK_BOX(box_Z2_middle), drive_scale, TRUE, TRUE, 0);
      }

      gtk_widget_set_tooltip_text(drive_scale, "Set TX Pwr in %");
    }

    if (device == DEVICE_HERMES_LITE2 && pa_enabled) {
      if (optimize_for_touchscreen) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(drive_scale), radio_get_drive() / 20);
      } else {
        gtk_range_set_increments (GTK_RANGE(drive_scale), 0.1, 0.1);
        gtk_range_set_value (GTK_RANGE(drive_scale), radio_get_drive() / 20);
      }

      if (!optimize_for_touchscreen) {
        for (float i = 0.0; i <= 5.0; i += 0.5) {
          gtk_scale_add_mark(GTK_SCALE(drive_scale), i, GTK_POS_TOP, NULL);
        }
      }
    } else {
      if (optimize_for_touchscreen) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(drive_scale), radio_get_drive());
      } else {
        gtk_range_set_increments (GTK_RANGE(drive_scale), 1.0, 1.0);
        gtk_range_set_value (GTK_RANGE(drive_scale), radio_get_drive());
      }
    }

    drive_scale_signal_id = g_signal_connect(G_OBJECT(drive_scale), "value_changed", G_CALLBACK(drive_value_changed_cb),
                            NULL);
    gtk_grid_attach(GTK_GRID(sliders), box_Z2_middle, 1, 1, 1, 1);  // Spalte 0 Zeile 1
  } else {
    mic_gain_label = NULL;
    mic_gain_scale = NULL;
    drive_label = NULL;
    drive_scale = NULL;
  }

  //-----------------------------------------------------------------------------------------------------------
  // Hauptcontainer: horizontale Box für SQL
  GtkWidget *box_Z2_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(box_Z2_right, box_right_width, widget_height);
  //-----------------------------------------------------------------------------------------------------------
  squelch_enable = gtk_toggle_button_new_with_label("SQL");
  gtk_widget_set_name(squelch_enable, "medium_toggle_button");
  gtk_widget_set_tooltip_text(squelch_enable, "Squelch ON / OFF");
  gtk_widget_set_size_request(squelch_enable, 90, -1);  // z.B. 100px
  gtk_widget_set_margin_top(squelch_enable, 5);
  gtk_widget_set_margin_bottom(squelch_enable, 5);
  gtk_widget_set_margin_end(squelch_enable, 0);    // rechter Rand (Ende)
  gtk_widget_set_margin_start(squelch_enable, 0);    // linker Rand (Anfang)
  gtk_widget_set_halign(squelch_enable, GTK_ALIGN_START);
  gtk_widget_set_valign(squelch_enable, GTK_ALIGN_CENTER);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(squelch_enable), active_receiver->squelch_enable);
  // begin label definition inside button
  squelch_label = gtk_bin_get_child(GTK_BIN(squelch_enable));
  // end label definition
  g_signal_connect(squelch_enable, "toggled", G_CALLBACK(squelch_enable_cb), NULL);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(box_Z2_right), squelch_enable, FALSE, FALSE, 0);
  //-------------------------------------------------------------------------------------------
  squelch_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  gtk_widget_set_margin_end(squelch_scale, 0);  // rechter Rand (Ende)
  gtk_widget_set_hexpand(squelch_scale, FALSE);  // fülle Box nicht nach rechts
  gtk_range_set_increments(GTK_RANGE(squelch_scale), 1.0, 1.0);
  gtk_range_set_value(GTK_RANGE(squelch_scale), active_receiver->squelch);
  gtk_widget_set_tooltip_text(squelch_scale, "Set Squelch Threshold");
  squelch_signal_id = g_signal_connect(G_OBJECT(squelch_scale), "value_changed", G_CALLBACK(squelch_value_changed_cb),
                                       NULL);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(box_Z2_right), squelch_scale, TRUE, TRUE, 0);
  gtk_grid_attach(GTK_GRID(sliders), box_Z2_right, 2, 1, 1, 1);  // Zeile 0 Spalte 2

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (can_transmit && display_sliders) {
    //-----------------------------------------------------------------------------------------------------------
    // Hauptcontainer: horizontale Box für TUNE DRV + MicPreAmp
    GtkWidget *box_Z3_left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z3_left, box_left_width, widget_height);
    //-----------------------------------------------------------------------------------------------------------
    // tune_drive_label
    tune_drive_label = gtk_label_new("TUNE Drv");
    gtk_widget_set_name(tune_drive_label, "boldlabel_border_blue");
    // Label breiter erzwingen
    gtk_widget_set_size_request(tune_drive_label, 105, -1);  // z.B. 100px
    gtk_widget_set_margin_top(tune_drive_label, 5);
    gtk_widget_set_margin_bottom(tune_drive_label, 5);
    gtk_widget_set_margin_end(tune_drive_label, 0);    // rechter Rand (Ende)
    gtk_widget_set_halign(tune_drive_label, GTK_ALIGN_START);
    gtk_widget_set_valign(tune_drive_label, GTK_ALIGN_CENTER);
    //-------------------------------------------------------------------------------------------
    // tune_drive_scale
    tune_drive_scale = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(tune_drive_scale), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(tune_drive_scale), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tune_drive_scale), transmitter->tune_drive);
    gtk_widget_set_tooltip_text(tune_drive_scale, "Set TX Pwr in % for TUNE");
    gtk_widget_set_margin_top(tune_drive_scale, 5);
    gtk_widget_set_margin_bottom(tune_drive_scale, 5);
    gtk_widget_set_margin_start(tune_drive_scale, 3);
    gtk_widget_set_margin_end(tune_drive_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(tune_drive_scale, FALSE);  // fülle Box nicht nach rechts
    tune_drive_scale_signal_id = g_signal_connect(G_OBJECT(tune_drive_scale), "value_changed",
                                 G_CALLBACK(tune_drive_changed_cb), NULL);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z3_left), tune_drive_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_Z3_left), tune_drive_scale, FALSE, FALSE, 0);
    //-------------------------------------------------------------------------------------------
    preamp_btn = gtk_toggle_button_new_with_label("Mic\nPreA");
    gtk_widget_set_name(preamp_btn, "front_toggle_button");
    gtk_widget_set_tooltip_text(preamp_btn, "Mic Preamplifier ON / OFF");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preamp_btn), transmitter->addgain_enable);
    // begin label definition inside button
    preamp_label = gtk_bin_get_child(GTK_BIN(preamp_btn));
    gtk_label_set_justify(GTK_LABEL(preamp_label), GTK_JUSTIFY_CENTER);
    // end label definition
    gtk_widget_set_size_request(preamp_btn, 55, -1);  // z.B. 100px
    gtk_widget_set_margin_top(preamp_btn, 0);
    gtk_widget_set_margin_bottom(preamp_btn, 0);
    gtk_widget_set_margin_start(preamp_btn, 5);
    gtk_widget_set_halign(preamp_btn, GTK_ALIGN_START);
    gtk_widget_set_valign(preamp_btn, GTK_ALIGN_CENTER);
    preamp_btn_signal_id = g_signal_connect(preamp_btn, "toggled", G_CALLBACK(preamp_btn_toggle_cb), NULL);
    //-------------------------------------------------------------------------------------------
    preamp_scale = gtk_spin_button_new_with_range(1.0, 20.0, 1.0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(preamp_scale), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(preamp_scale), TRUE);
    gtk_widget_set_tooltip_text(preamp_scale, "Mic Preamp Gain in db");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(preamp_scale), (double)transmitter->addgain_gain);
    gtk_widget_set_margin_top(preamp_scale, 5);
    gtk_widget_set_margin_bottom(preamp_scale, 5);
    gtk_widget_set_margin_end(preamp_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(preamp_scale, FALSE);  // fülle Box nicht nach rechts
    preamp_scale_signal_id = g_signal_connect(G_OBJECT(preamp_scale), "value_changed", G_CALLBACK(preamp_scale_changed_cb),
                             NULL);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z3_left), preamp_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_Z3_left), preamp_scale, FALSE, FALSE, 0);
    // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
    gtk_grid_attach(GTK_GRID(sliders), box_Z3_left, 0, 2, 1, 1);
    //-------------------------------------------------------------------------------------------

    //-------------------------------------------------------------------------------------------
    if (n_input_devices > 0) {
      //-----------------------------------------------------------------------------------------------------------
      // Hauptcontainer: horizontale Box für TUNE DRV + MicPreAmp
      GtkWidget *box_Z3_middle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
      gtk_widget_set_size_request(box_Z3_middle, box_middle_width, widget_height);
      //-----------------------------------------------------------------------------------------------------------
      local_mic_button = gtk_toggle_button_new_with_label("Local\nMic");
      gtk_widget_set_name(local_mic_button, "front_toggle_button");
      gtk_widget_set_tooltip_text(local_mic_button,
                                  "Set use of local connected audio input device\n(e.g. local Mic) ON / OFF");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(local_mic_button), transmitter->local_microphone);
      gtk_widget_set_size_request(local_mic_button, 90, -1);  // z.B. 100px
      gtk_widget_set_margin_top(local_mic_button, 0);
      gtk_widget_set_margin_bottom(local_mic_button, 0);
      gtk_widget_set_margin_start(local_mic_button, 0);
      gtk_widget_set_margin_end(local_mic_button, 0);
      gtk_widget_set_halign(local_mic_button, GTK_ALIGN_START);
      gtk_widget_set_valign(local_mic_button, GTK_ALIGN_CENTER);
      // begin label definition inside button
      local_mic_label = gtk_bin_get_child(GTK_BIN(local_mic_button));
      gtk_label_set_justify(GTK_LABEL(local_mic_label), GTK_JUSTIFY_CENTER);
      // end label definition
      local_mic_toggle_signal_id = g_signal_connect(local_mic_button, "toggled", G_CALLBACK(local_mic_toggle_cb), NULL);
      //-------------------------------------------------------------------------------------------
      local_mic_input = gtk_combo_box_text_new();
      gtk_widget_set_name(local_mic_input, "boldlabel");
      gtk_widget_set_tooltip_text(local_mic_input, "Select local audio input device");
      gtk_widget_set_margin_top(local_mic_input, 5);
      gtk_widget_set_margin_bottom(local_mic_input, 5);
      gtk_widget_set_margin_end(local_mic_input, 0);  // rechter Rand (Ende)
      gtk_widget_set_hexpand(local_mic_input, FALSE);  // fülle Box nicht nach rechts

      for (int i = 0; i < n_input_devices; i++) {
#ifdef __APPLE__
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(local_mic_input), NULL, truncate_text_3p(input_devices[i].description,
                                  32));
#else
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(local_mic_input), NULL, truncate_text_3p(input_devices[i].description,
                                  28));
#endif

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

      gtk_widget_set_can_focus(local_mic_input, TRUE);
      gboolean flag = FALSE;
      local_mic_input_signal_id = g_signal_connect(local_mic_input, "changed", G_CALLBACK(local_input_changed_cb),
                                  GINT_TO_POINTER(flag));
      // Widgets in Box packen
      gtk_box_pack_start(GTK_BOX(box_Z3_middle), local_mic_button, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(box_Z3_middle), local_mic_input, FALSE, FALSE, 0);
      // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
      gtk_grid_attach(GTK_GRID(sliders), box_Z3_middle, 1, 2, 1, 1); // Spalte 2 Zeile 3
    }

    //-----------------------------------------------------------------------------------------------------------
    // Hauptcontainer: horizontale Box
    GtkWidget *box_Z3_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z3_right, box_right_width, widget_height);
    //-----------------------------------------------------------------------------------------------------------
    bbcompr_btn = gtk_toggle_button_new_with_label("Speech\nProc");
    gtk_widget_set_name(bbcompr_btn, "front_toggle_button");
    gtk_widget_set_tooltip_text(bbcompr_btn, "Speech Processor ON/OFF");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bbcompr_btn), transmitter->compressor);
    // begin label definition inside button
    bbcompr_label = gtk_bin_get_child(GTK_BIN(bbcompr_btn));
    gtk_label_set_justify(GTK_LABEL(bbcompr_label), GTK_JUSTIFY_CENTER);
    // end label definition
    gtk_widget_set_size_request(bbcompr_btn, 90, -1);  // z.B. 100px
    gtk_widget_set_margin_top(bbcompr_btn, 0);
    gtk_widget_set_margin_bottom(bbcompr_btn, 0);
    gtk_widget_set_halign(bbcompr_btn, GTK_ALIGN_START);
    gtk_widget_set_valign(bbcompr_btn, GTK_ALIGN_CENTER);
    bbcompr_btn_signal_id = g_signal_connect(bbcompr_btn, "toggled", G_CALLBACK(bbcompr_btn_toggle_cb), NULL);
    //-------------------------------------------------------------------------------------------
    bbcompr_scale = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(bbcompr_scale), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(bbcompr_scale), TRUE);
    gtk_widget_set_tooltip_text(bbcompr_scale, "Speech Processor Gain in db");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(bbcompr_scale), (double)transmitter->compressor_level);
    gtk_widget_set_margin_top(bbcompr_scale, 5);
    gtk_widget_set_margin_bottom(bbcompr_scale, 5);
    gtk_widget_set_margin_start(bbcompr_scale, 5);
    gtk_widget_set_margin_end(bbcompr_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(bbcompr_scale, FALSE);  // fülle Box nicht nach rechts
    bbcompr_scale_signal_id = g_signal_connect(G_OBJECT(bbcompr_scale), "value_changed",
                              G_CALLBACK(bbcompr_scale_changed_cb), NULL);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z3_right), bbcompr_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_Z3_right), bbcompr_scale, FALSE, FALSE, 0);
    //-------------------------------------------------------------------------------------------
    lev_btn = gtk_toggle_button_new_with_label("Mic\nLeveler");
    gtk_widget_set_name(lev_btn, "front_toggle_button");
    gtk_widget_set_tooltip_text(lev_btn, "Mic Leveler ON/OFF");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lev_btn), transmitter->lev_enable);
    // begin label definition inside button
    lev_label = gtk_bin_get_child(GTK_BIN(lev_btn));
    gtk_label_set_justify(GTK_LABEL(lev_label), GTK_JUSTIFY_CENTER);
    // end label definition
    gtk_widget_set_size_request(lev_btn, 55, -1);  // z.B. 100px
    gtk_widget_set_margin_top(lev_btn, 0);
    gtk_widget_set_margin_bottom(lev_btn, 0);
    gtk_widget_set_margin_start(lev_btn, 10);
    gtk_widget_set_halign(lev_btn, GTK_ALIGN_START);
    gtk_widget_set_valign(lev_btn, GTK_ALIGN_CENTER);
    lev_btn_signal_id = g_signal_connect(lev_btn, "toggled", G_CALLBACK(lev_btn_toggle_cb), NULL);
    //-------------------------------------------------------------------------------------------
    lev_scale = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(lev_scale), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(lev_scale), TRUE);
    gtk_widget_set_tooltip_text(lev_scale, "Leveler Gain in db");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(lev_scale), (double)transmitter->lev_gain);
    gtk_widget_set_margin_top(lev_scale, 5);
    gtk_widget_set_margin_bottom(lev_scale, 5);
    gtk_widget_set_margin_end(lev_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(lev_scale, FALSE);  // fülle Box nicht nach rechts
    lev_scale_signal_id = g_signal_connect(G_OBJECT(lev_scale), "value_changed", G_CALLBACK(lev_scale_changed_cb), NULL);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z3_right), lev_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_Z3_right), lev_scale, FALSE, FALSE, 0);
    // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
    gtk_grid_attach(GTK_GRID(sliders), box_Z3_right, 2, 2, 1, 1);

    // sanity check, if DIGIMODE selected set BBCOMPR and LEV inactive
    if (selected_mode == modeDIGL || selected_mode == modeDIGU) {
      gtk_widget_set_sensitive(preamp_scale, FALSE);
      gtk_widget_set_sensitive(preamp_btn, FALSE);
      gtk_widget_set_sensitive(bbcompr_scale, FALSE);
      gtk_widget_set_sensitive(bbcompr_btn, FALSE);
      gtk_widget_set_sensitive(lev_scale, FALSE);
      gtk_widget_set_sensitive(lev_btn, FALSE);
      gtk_widget_queue_draw(sliders);
    }
  } else {
    tune_drive_label = NULL;
    tune_drive_scale = NULL;
    local_mic_label = NULL;
    local_mic_button = NULL;
    local_mic_input = NULL;
    bbcompr_label = NULL;
    bbcompr_btn = NULL;
    bbcompr_scale = NULL;
    lev_label = NULL;
    lev_btn = NULL;
    lev_scale = NULL;
    preamp_label = NULL;
    preamp_btn = NULL;
    preamp_scale = NULL;
  }

  gtk_widget_show_all(sliders);
  return sliders;
}
