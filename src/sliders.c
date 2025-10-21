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
#include "noise_menu.h"

static int width;
static int height;

static GtkWidget *sliders;

static guint scale_timer;
static enum ACTION scale_status = NO_ACTION;
static GtkWidget *scale_dialog;

static GtkWidget *af_gain_label;
static GtkWidget *af_gain_btn;
static GtkWidget *af_gain_scale;
static gulong    af_gain_btn_signal_id;
static GtkWidget *rf_gain_label = NULL;
static GtkWidget *rf_gain_scale = NULL;
static gulong rf_gain_scale_signal_id;
static GtkWidget *hwagc_btn;
static GtkWidget *hwagc_label;
static gulong hwagc_btn_signal_id;
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
static GtkWidget *tune_drive_btn;
static GtkWidget *tune_drive_scale;
static gulong tune_drive_scale_signal_id;
static gulong tune_drive_btn_signal_id;
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
static GtkWidget *binaural_btn;
static GtkWidget *binaural_label;
static gulong binaural_btn_signal_id;
static GtkWidget *snb_btn;
static GtkWidget *snb_label;
static gulong snb_btn_signal_id;
static GtkWidget *split_btn;
static GtkWidget *split_label;
static gulong split_btn_signal_id;
static GtkWidget *swap_btn;
static GtkWidget *swap_label;
static GtkWidget *equal_btn;
static GtkWidget *equal_label;
static GtkWidget *agc_label;
static GtkWidget *agc_btn;
static GtkWidget *agc_gain_scale;
static gulong agc_gain_scale_signal_id;
static gulong agc_btn_signal_id;
static GtkWidget *ps_btn;
static GtkWidget *ps_label;
static gulong ps_btn_signal_id;
static GtkWidget *nr_btn;
static GtkWidget *nr_label;
static gulong nr_btn_signal_id;
static GtkWidget *hwagc_scale;
static gulong hwagc_scale_signal_id;
static GtkWidget *ifgr_scale;
static gulong ifgr_scale_signal_id;
static GtkStyleContext *nr_context;
static GtkStyleContext *agc_context;


char txpwr_ttip_txt[64];

// --- AGC Labels ---
static const char *agc_labels[] = {
  "AGC-OFF",
  "AGC-L",
  "AGC-S",
  "AGC-M",
  "AGC-F"
};

// --- NR Labels ---
static const char *nr_labels[] = {
  "NR-OFF",
  "NR",
  "NR2",
#ifdef EXTNR
  "NR3",
  "NR4"
#endif
};

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

    if (GTK_IS_SPIN_BUTTON(agc_gain_scale)) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(agc_gain_scale), (double)active_receiver->agc_gain);
    } else if (GTK_IS_RANGE(agc_gain_scale)) {
      gtk_range_set_value (GTK_RANGE(agc_gain_scale), (double)active_receiver->agc_gain);
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
    g_signal_handler_block(G_OBJECT(agc_gain_scale), agc_gain_scale_signal_id);

    if (GTK_IS_SPIN_BUTTON(agc_gain_scale)) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(agc_gain_scale), (double)receiver[rx]->agc_gain);
    } else if (GTK_IS_RANGE(agc_gain_scale)) {
      gtk_range_set_value (GTK_RANGE(agc_gain_scale), (double)receiver[rx]->agc_gain);
    }

    g_signal_handler_unblock(G_OBJECT(agc_gain_scale), agc_gain_scale_signal_id);
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
    if (strcmp(radio->name, "sdrplay") == 0) {
      soapy_protocol_set_gain_element(active_receiver, radio->info.soapy.rx_gain[index_rx_gains()],
                                      (int)adc[active_receiver->adc].gain);
    } else {
      soapy_protocol_set_gain(active_receiver);
    }

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

    // update the tooltip text if tx_drive is changed
    if (device == DEVICE_HERMES_LITE2 && pa_enabled) {
      snprintf(txpwr_ttip_txt, sizeof(txpwr_ttip_txt), "Set TX Pwr in W ≙ %.0f %%", radio_get_drive());
      gtk_widget_set_tooltip_text(drive_scale, NULL);
      gtk_widget_set_tooltip_text(drive_scale, txpwr_ttip_txt);
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

  // update the tooltip text if tx_drive is changed
  if (device == DEVICE_HERMES_LITE2 && pa_enabled) {
    snprintf(txpwr_ttip_txt, sizeof(txpwr_ttip_txt), "Set TX Pwr in W ≙ %.0f %%", radio_get_drive());
    gtk_widget_set_tooltip_text(widget, NULL);
    gtk_widget_set_tooltip_text(widget, txpwr_ttip_txt);
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

static void binaural_toggle_cb(GtkWidget *widget, gpointer data) {
  active_receiver->binaural = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  rx_set_af_binaural(active_receiver);
}

static void snb_toggle_cb(GtkWidget *widget, gpointer data) {
  active_receiver->snb = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  update_noise();
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
  update_slider_preamp_button(TRUE);
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
    char preamp_tip[256];

    if (transmitter->addgain_enable) {
      snprintf(preamp_tip, sizeof(preamp_tip),
               "Switch the Mic Preamplifier ON or OFF\n"
               "[Always OFF in DIGU/DIGL]\n"
               "Additional gain on top of Mic Gain\n"
               "Current Mic Gain %+ddb + Current Preamp Gain %+ddb = %+ddb\n\n"
               "Adjust this value in Menu → TX Menu.", (int)transmitter->mic_gain, (int)transmitter->addgain_gain,
               (int)(transmitter->mic_gain + transmitter->addgain_gain));
    } else {
      snprintf(preamp_tip, sizeof(preamp_tip),
               "Switch the Mic Preamplifier ON or OFF\n"
               "[Always OFF in DIGU/DIGL]\n"
               "Additional gain on top of Mic Gain\n"
               "Current Mic Gain %+ddb\n\n"
               "Adjust this value in Menu → TX Menu.", (int)transmitter->mic_gain);
    }

    gtk_widget_set_tooltip_text(preamp_btn, preamp_tip);

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
      gtk_widget_show(tune_drive_scale);
      gtk_widget_set_tooltip_text(tune_drive_btn, "TUNE with TUNE Drive:\nSet tune level in percent of maximum TX PWR");
    } else {
      gtk_widget_set_sensitive(tune_drive_scale, FALSE);
      gtk_widget_hide(tune_drive_scale);
      gtk_widget_set_tooltip_text(tune_drive_btn, "TUNE Drive = TX PWR");
    }

    g_signal_handler_unblock(G_OBJECT(tune_drive_scale), tune_drive_scale_signal_id);
    gtk_widget_queue_draw(tune_drive_scale);
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

void update_slider_autogain_btn() {
  if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (autogain_btn), autogain_btn_signal_id);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autogain_btn), autogain_enabled);
    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (autogain_btn), autogain_btn_signal_id);
    gtk_widget_queue_draw(autogain_btn);
  }
}

void update_slider_snb_button(gboolean show_widget) {
  if (display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (snb_btn), snb_btn_signal_id);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (snb_btn), active_receiver->snb);

    if (show_widget) {
      gtk_widget_set_sensitive(snb_btn, TRUE);
    } else {
      gtk_widget_set_sensitive(snb_btn, FALSE);
    }

    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (snb_btn), snb_btn_signal_id);
    gtk_widget_queue_draw(snb_btn);
  }
}

void update_slider_binaural_btn() {
  if (display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (binaural_btn), binaural_btn_signal_id);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (binaural_btn), active_receiver->binaural);
    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (binaural_btn), binaural_btn_signal_id);
    gtk_widget_queue_draw(binaural_btn);
  }
}

void update_slider_tune_drive_btn() {
  if (display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (tune_drive_btn), tune_drive_btn_signal_id);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tune_drive_btn), !(radio_get_tune()));

    if (radio_get_tune()) {
      gtk_label_set_text(GTK_LABEL(tune_drive_label), "TUNING");
    } else {
      gtk_label_set_text(GTK_LABEL(tune_drive_label), "TUNE");
    }

    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (tune_drive_btn), tune_drive_btn_signal_id);
    gtk_widget_queue_draw(tune_drive_btn);
  }
}

static void tune_drive_toggle_cb(GtkWidget *widget, gpointer data) {
  // int state = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  int state = radio_get_tune();
  radio_tune_update(!state);
  update_slider_tune_drive_btn();
}

void update_slider_af_gain_btn() {
  if (display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (af_gain_btn), af_gain_btn_signal_id);
    // invert button, red = MUTE, green = Playback
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (af_gain_btn), !active_receiver->mute_radio);

    if (active_receiver->mute_radio) {
      gtk_label_set_text(GTK_LABEL(af_gain_label), "MUTE");
      gtk_widget_set_tooltip_text(af_gain_btn, "Press button for PLAY Audio");
    } else {
      gtk_label_set_text(GTK_LABEL(af_gain_label), "Volume");
      gtk_widget_set_tooltip_text(af_gain_btn, "Press button for MUTE Audio");
    }

    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (af_gain_btn), af_gain_btn_signal_id);
    gtk_widget_queue_draw(af_gain_btn);
  }
}

static void af_gain_toggle_cb(GtkWidget *widget, gpointer data) {
  // int state = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  active_receiver->mute_radio = !active_receiver->mute_radio;
  g_idle_add(ext_vfo_update, NULL);
  update_slider_af_gain_btn();
}

void update_slider_split_btn() {
  if (display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (split_btn), split_btn_signal_id);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (split_btn), split);
    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (split_btn), split_btn_signal_id);
    gtk_widget_queue_draw(split_btn);
  }
}

static void split_btn_toggle_cb(GtkWidget *widget, gpointer data) {
  int new = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_set_split(new);

  if (new == 1) {
    vfo_a_to_b();
  }

  update_slider_split_btn();
}

#ifdef SOAPYSDR

static void hwagc_scale_value_changed_cb(GtkWidget *widget, gpointer data) {
  double spin_value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  soapy_protocol_set_agc_setpoint(active_receiver, (int)spin_value);
}

static void ifgr_scale_value_changed_cb(GtkWidget *widget, gpointer data) {
  double spin_value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  if (index_if_gains() > -1 && !adc[active_receiver->adc].agc) {
    soapy_protocol_set_gain_element(active_receiver, radio->info.soapy.rx_gain[index_if_gains()], (int)spin_value);
  }

  if (index_if_gains() > -1) { update_ifgr_scale_soapy(index_if_gains()); }
}


void update_slider_hwagc_btn() {
  if (display_sliders && device == SOAPYSDR_USB_DEVICE && radio->info.soapy.rx_has_automatic_gain) {
    if (strcmp(radio->name, "sdrplay") == 0) {
      g_signal_handler_block(GTK_TOGGLE_BUTTON (hwagc_btn), hwagc_btn_signal_id);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (hwagc_btn), adc[active_receiver->adc].agc);
      g_signal_handler_unblock(GTK_TOGGLE_BUTTON (hwagc_btn), hwagc_btn_signal_id);

      if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(hwagc_btn))) {
        gtk_label_set_text(GTK_LABEL(hwagc_label), "HW-AGC");
        gtk_widget_set_sensitive(hwagc_scale, TRUE);

        if (index_if_gains() > -1) { gtk_widget_set_sensitive(ifgr_scale, FALSE); }
      } else {
        gtk_label_set_text(GTK_LABEL(hwagc_label), "IFGR");
        gtk_widget_set_sensitive(hwagc_scale, FALSE);

        if (index_if_gains() > -1) { gtk_widget_set_sensitive(ifgr_scale, TRUE); }
      }

      gtk_widget_queue_draw(hwagc_btn);
      gtk_widget_queue_draw(hwagc_scale);
    }
  }
}

static void hwagc_btn_toggle_cb(GtkWidget *widget, gpointer data) {
  if (device == SOAPYSDR_USB_DEVICE && radio->info.soapy.rx_has_automatic_gain) {
    int hwagc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    adc[active_receiver->adc].agc = hwagc;
    soapy_protocol_set_automatic_gain(active_receiver, hwagc);
    // if (!agc) { soapy_protocol_set_gain(active_receiver); }
    update_slider_hwagc_btn();
  }
}

void update_rf_gain_scale_soapy(int idx) {
  if (display_sliders && device == SOAPYSDR_USB_DEVICE) {
    if (radio->info.soapy.rx_gains > 0) {
      g_signal_handler_block(G_OBJECT(rf_gain_scale), rf_gain_scale_signal_id);
      gtk_range_set_value (GTK_RANGE(rf_gain_scale), (double)soapy_protocol_get_gain_element(active_receiver,
                           radio->info.soapy.rx_gain[idx]));
      g_signal_handler_unblock(G_OBJECT(rf_gain_scale), rf_gain_scale_signal_id);
      gtk_widget_queue_draw(rf_gain_scale);
      t_print("%s: idx = %d rf_gain_scale value = %f\n", __FUNCTION__, idx,
              (double)soapy_protocol_get_gain_element(active_receiver,
                  radio->info.soapy.rx_gain[idx]));
    }
  }
}

void update_ifgr_scale_soapy(int idx) {
  if (display_sliders && device == SOAPYSDR_USB_DEVICE) {
    if (radio->info.soapy.rx_gains > 0) {
      g_signal_handler_block(G_OBJECT(ifgr_scale), ifgr_scale_signal_id);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(ifgr_scale), (double)soapy_protocol_get_gain_element(active_receiver,
                                radio->info.soapy.rx_gain[idx]));
      g_signal_handler_unblock(G_OBJECT(ifgr_scale), ifgr_scale_signal_id);
      gtk_widget_queue_draw(ifgr_scale);
      t_print("%s: idx = %d ifgr_scale value = %f\n", __FUNCTION__, idx,
              (double)soapy_protocol_get_gain_element(active_receiver,
                  radio->info.soapy.rx_gain[idx]));
    }
  }
}
#endif

static void swap_btn_pressed_cb(GtkWidget *widget, gpointer data) {
  vfo_a_swap_b();
}

static void swap_btn_released_cb(GtkWidget *widget, gpointer data) {
  return;
}

static void equal_btn_pressed_cb(GtkWidget *widget, gpointer data) {
  vfo_a_to_b();
}

static void equal_btn_released_cb(GtkWidget *widget, gpointer data) {
  return;
}

void update_slider_nr_btn() {
  if (display_sliders && have_rx_gain) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (nr_btn), nr_btn_signal_id);
    gtk_button_set_label(GTK_BUTTON(nr_btn), nr_labels[active_receiver->nr]);

    if (active_receiver->nr > 0) {
      gtk_style_context_add_class(nr_context, "active");
    } else {
      gtk_style_context_remove_class(nr_context, "active");
    }

    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (nr_btn), nr_btn_signal_id);
    gtk_widget_queue_draw(nr_btn);
  }
}

static void nr_btn_pressed_cb(GtkWidget *widget, gpointer data) {
  int id = active_receiver->id;
  active_receiver->nr++;
#ifdef EXTNR

  if (active_receiver->nr > 4) { active_receiver->nr = 0; }

#else

  if (active_receiver->nr > 2) { active_receiver->nr = 0; }

#endif

  if (id == 0) {
    int mode = vfo[id].mode;
    mode_settings[mode].nr = active_receiver->nr;
    copy_mode_settings(mode);
  }

  update_noise();
  gtk_button_set_label(GTK_BUTTON(nr_btn), nr_labels[active_receiver->nr]);

  if (active_receiver->nr > 0) {
    gtk_style_context_add_class(nr_context, "active");
  } else {
    gtk_style_context_remove_class(nr_context, "active");
  }
}

void update_slider_agc_btn() {
  if (display_sliders) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (agc_btn), agc_btn_signal_id);
    gtk_button_set_label(GTK_BUTTON(agc_btn), agc_labels[active_receiver->agc]);
    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (agc_btn), agc_btn_signal_id);
    gtk_widget_queue_draw(agc_btn);

    if (active_receiver->agc > 0) {
      gtk_style_context_add_class(agc_context, "active");
    } else {
      gtk_style_context_remove_class(agc_context, "active");
    }
  }
}

static void agc_btn_pressed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->agc++;

  if (active_receiver->agc >= AGC_LAST) {
    active_receiver->agc = 0;
  }

  rx_set_agc(active_receiver);
  gtk_button_set_label(GTK_BUTTON(agc_btn), agc_labels[active_receiver->agc]);
  g_idle_add(ext_vfo_update, NULL);

  if (active_receiver->agc > 0) {
    gtk_style_context_add_class(agc_context, "active");
  } else {
    gtk_style_context_remove_class(agc_context, "active");
  }
}

void update_slider_ps_btn() {
  if (can_transmit && display_sliders && have_rx_gain) {
    g_signal_handler_block(GTK_TOGGLE_BUTTON (ps_btn), ps_btn_signal_id);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ps_btn), transmitter->puresignal);
    g_signal_handler_unblock(GTK_TOGGLE_BUTTON (ps_btn), ps_btn_signal_id);
    gtk_widget_queue_draw(ps_btn);
  }
}

static void ps_toggle_cb(GtkWidget *widget, gpointer data) {
#if defined (__CPYMODE__)
  int _mode = vfo[active_receiver->id].mode;

  if (can_transmit) {
    // PS make no sense in CW and FM !
    if (_mode == modeUSB || _mode == modeLSB || _mode == modeDIGL || _mode == modeDIGU || _mode == modeAM
        || _mode == modeDSB) {
      tx_ps_onoff(transmitter, transmitter->puresignal ? 0 : 1);
      mode_settings[_mode].puresignal = transmitter->puresignal;
      copy_mode_settings(_mode);
    } else {
      mode_settings[_mode].puresignal = 0;
      copy_mode_settings(_mode);
    }
  }

#else

  if (can_transmit) {
    tx_ps_onoff(transmitter, transmitter->puresignal ? 0 : 1);
  }

#endif
  update_slider_ps_btn();
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
  int box_right_width = box_left_width - 50;
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
  GtkWidget *box_Z1_left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(box_Z1_left, box_left_width, widget_height);
  gtk_box_set_spacing(GTK_BOX(box_Z1_left), 5);
  //-----------------------------------------------------------------------------------------------------------
  af_gain_btn = gtk_toggle_button_new_with_label("Volume");
  // gtk_widget_set_name(af_gain_btn, "medium_toggle_button");
  gtk_widget_set_name(af_gain_btn, "front_toggle_button");

  if (!active_receiver->mute_radio) {
    gtk_widget_set_tooltip_text(af_gain_btn, "Press button for MUTE Audio");
  } else if (active_receiver->mute_radio) {
    gtk_widget_set_tooltip_text(af_gain_btn, "Press button for PLAY Audio");
  }

  // invert button, red = MUTE, green = Playback
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(af_gain_btn), !active_receiver->mute_radio);
  // begin label definition inside button
  af_gain_label = gtk_bin_get_child(GTK_BIN(af_gain_btn));
  gtk_label_set_justify(GTK_LABEL(af_gain_label), GTK_JUSTIFY_CENTER);
  // end label definition
  // Label breiter erzwingen
  gtk_widget_set_size_request(af_gain_btn, 105, -1);  // z.B. 100px
  gtk_widget_set_margin_top(af_gain_btn, 0);
  gtk_widget_set_margin_bottom(af_gain_btn, 0);
  gtk_widget_set_margin_end(af_gain_btn, 0);    // rechter Rand (Ende)
  gtk_widget_set_halign(af_gain_btn, GTK_ALIGN_START);
  gtk_widget_set_valign(af_gain_btn, GTK_ALIGN_CENTER);
  af_gain_btn_signal_id = g_signal_connect(G_OBJECT(af_gain_btn), "toggled", G_CALLBACK(af_gain_toggle_cb),
                          NULL);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(box_Z1_left), af_gain_btn, FALSE, FALSE, 0);
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
  gtk_box_pack_start(GTK_BOX(box_Z1_left), af_gain_scale, TRUE, TRUE, 0);
  //-----------------------------------------------------------------------------------------------------------
  // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
  gtk_grid_attach(GTK_GRID(sliders), box_Z1_left, 0, 0, 1, 1);  // Zeile 0 Spalte 0
  //-----------------------------------------------------------------------------------------------------------
  // Hauptcontainer: horizontale Box für AGC
  GtkWidget *box_Z1_middle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(box_Z1_middle, box_middle_width, widget_height);
  gtk_box_set_spacing(GTK_BOX(box_Z1_middle), 5);
  //-----------------------------------------------------------------------------------------------------------
  agc_btn = gtk_button_new_with_label(agc_labels[active_receiver->agc]);
  gtk_widget_set_name(agc_btn, "medium_toggle_button");
  agc_context = gtk_widget_get_style_context(agc_btn);

  if (active_receiver->agc > 0) {
    gtk_style_context_add_class(agc_context, "active");
  } else {
    gtk_style_context_remove_class(agc_context, "active");
  }

  gtk_widget_set_tooltip_text(agc_btn, "Set AGC speed:\n"
                                       "OFF → LONG → SLOW → MIDDLE → FAST");
  // begin label definition inside button
  agc_label = gtk_bin_get_child(GTK_BIN(agc_btn));
  gtk_label_set_justify(GTK_LABEL(agc_label), GTK_JUSTIFY_CENTER);
  // end label definition
  gtk_widget_set_size_request(agc_btn, 90, -1);  // z.B. 100px
  gtk_widget_set_margin_top(agc_btn, 0);
  gtk_widget_set_margin_bottom(agc_btn, 0);
  gtk_widget_set_margin_start(agc_btn, 0);
  gtk_widget_set_margin_end(agc_btn, 0);
  gtk_widget_set_halign(agc_btn, GTK_ALIGN_START);
  gtk_widget_set_valign(agc_btn, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(agc_btn, FALSE);  // fülle Box nicht nach rechts
  agc_btn_signal_id = g_signal_connect(agc_btn, "pressed", G_CALLBACK(agc_btn_pressed_cb), NULL);
  // g_signal_connect(agc_btn, "released", G_CALLBACK(agc_btn_pressed_cb), NULL);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(box_Z1_middle), agc_btn, FALSE, FALSE, 0);
  //-----------------------------------------------------------------------------------------------------------

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (optimize_for_touchscreen) {
    agc_gain_scale = gtk_spin_button_new_with_range(-20.0, 120.0, 1.0);
    gtk_widget_set_name(agc_gain_scale, "front_spin_button");
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(agc_gain_scale), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(agc_gain_scale), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(agc_gain_scale), (double)active_receiver->agc_gain);
    gtk_widget_set_margin_top(agc_gain_scale, 5);
    gtk_widget_set_margin_bottom(agc_gain_scale, 5);
    gtk_widget_set_margin_start(agc_gain_scale, 0);
    gtk_widget_set_margin_end(agc_gain_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(agc_gain_scale, FALSE);  // fülle Box nicht nach rechts
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_middle), agc_gain_scale, FALSE, FALSE, 0);
  } else {
    agc_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -20.0, 120.0, 1.0);
    gtk_range_set_increments (GTK_RANGE(agc_gain_scale), 1.0, 1.0);
    gtk_range_set_value (GTK_RANGE(agc_gain_scale), (double)active_receiver->agc_gain);

    for (double agc_mark = -20.0; agc_mark <= 120.0; agc_mark += 20.0) {
      gtk_scale_add_mark(GTK_SCALE(agc_gain_scale), agc_mark, GTK_POS_TOP, NULL);
    }

    gtk_widget_set_tooltip_text(agc_gain_scale,
                                "AGC of the currently active receiver.\n"
                                "Adjust coral colored horizontal line\n"
                                "slightly above the noise floor.");
    gtk_widget_set_margin_end(agc_gain_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(agc_gain_scale, FALSE);  // fülle Box nicht nach rechts
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_middle), agc_gain_scale, TRUE, TRUE, 0);
  }

  agc_gain_scale_signal_id = g_signal_connect(G_OBJECT(agc_gain_scale), "value_changed",
                             G_CALLBACK(agcgain_value_changed_cb),
                             NULL);
  // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
  gtk_grid_attach(GTK_GRID(sliders), box_Z1_middle, 1, 0, 1, 1);  // Zeile 0 Spalte 1

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (have_rx_gain) {
    //-----------------------------------------------------------------------------------------------------------
    // Hauptcontainer: horizontale Box für RF Gain
    GtkWidget *box_Z1_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z1_right, box_right_width, widget_height);
    gtk_box_set_spacing(GTK_BOX(box_Z1_right), 5);
    //-----------------------------------------------------------------------------------------------------------
#if defined (__AUTOG__)

    if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && can_transmit) {
      autogain_btn = gtk_toggle_button_new_with_label("RxPGA");
      gtk_widget_set_tooltip_text(autogain_btn, "AutoGain ON/OFF");
      gtk_widget_set_name(autogain_btn, "medium_toggle_button");
      // Label breiter erzwingen
      gtk_widget_set_size_request(autogain_btn, 90, -1);  // z.B. 100px
      gtk_widget_set_margin_top(autogain_btn, 0);
      gtk_widget_set_margin_bottom(autogain_btn, 0);
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
    } else {
      // if not HL2 make sure, that all autogain is OFF
      if (autogain_enabled || autogain_time_enabled) {
        autogain_enabled = 0;
        autogain_time_enabled = 0;
      }

      autogain_btn = NULL;
#ifdef SOAPYSDR

      if (device == SOAPYSDR_USB_DEVICE && radio->info.soapy.rx_gains > 0) {
        rf_gain_label = gtk_label_new(radio->info.soapy.rx_gain[index_rx_gains()]);
      } else {
        rf_gain_label = gtk_label_new("RF Gain");
      }

#else
      rf_gain_label = gtk_label_new("RF Gain");
#endif
      gtk_widget_set_name(rf_gain_label, "boldlabel_border_blue");
      // Label breiter erzwingen
      gtk_widget_set_size_request(rf_gain_label, 90, -1);  // z.B. 100px
      gtk_widget_set_margin_top(rf_gain_label, 0);
      gtk_widget_set_margin_bottom(rf_gain_label, 0);
      gtk_widget_set_margin_end(rf_gain_label, 0);    // rechter Rand (Ende)
      gtk_widget_set_margin_start(rf_gain_label, 0);    // linker Rand (Anfang)
      gtk_widget_set_halign(rf_gain_label, GTK_ALIGN_START);
      gtk_widget_set_valign(rf_gain_label, GTK_ALIGN_CENTER);
      // Widgets in Box packen
      gtk_box_pack_start(GTK_BOX(box_Z1_right), rf_gain_label, FALSE, FALSE, 0);
    }

#else

    if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
      rf_gain_label = gtk_label_new("RxPGA");
#ifdef SOAPYSDR
    } else if (device == SOAPYSDR_USB_DEVICE && radio->info.soapy.rx_gains > 0) {
      rf_gain_label = gtk_label_new(radio->info.soapy.rx_gain[index_rx_gains()]);
#endif
    } else {
      rf_gain_label = gtk_label_new("RF Gain");
    }

    gtk_widget_set_name(rf_gain_label, "boldlabel_border_blue");
    // Label breiter erzwingen
    gtk_widget_set_size_request(rf_gain_label, 90, -1);  // z.B. 100px
    gtk_widget_set_margin_top(rf_gain_label, 0);
    gtk_widget_set_margin_bottom(rf_gain_label, 0);
    gtk_widget_set_margin_end(rf_gain_label, 0);    // rechter Rand (Ende)
    gtk_widget_set_margin_start(rf_gain_label, 0);    // linker Rand (Anfang)
    gtk_widget_set_halign(rf_gain_label, GTK_ALIGN_START);
    gtk_widget_set_valign(rf_gain_label, GTK_ALIGN_CENTER);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_right), rf_gain_label, FALSE, FALSE, 0);
#endif
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#ifdef SOAPYSDR

    if (device == SOAPYSDR_USB_DEVICE) {
      if (radio->info.soapy.rx_gains > 0) {
        rxgain_index_0 = index_rx_gains();

        if (adc[0].min_gain != radio->info.soapy.rx_range[rxgain_index_0].minimum) {
          adc[0].min_gain = radio->info.soapy.rx_range[rxgain_index_0].minimum;
        }

        if (adc[0].max_gain != radio->info.soapy.rx_range[rxgain_index_0].maximum) {
          adc[0].max_gain = radio->info.soapy.rx_range[rxgain_index_0].maximum;
        }

        if (adc[0].gain < adc[0].min_gain || adc[0].gain > adc[0].max_gain) {
          adc[0].gain = adc[0].min_gain;
        }
      }
    }

#endif
    t_print("%s: adc[0].min_gain = %f adc[0].max_gain = %f\n", __FUNCTION__, adc[0].min_gain, adc[0].max_gain);
    rf_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, adc[0].min_gain, adc[0].max_gain, 1.0);
    gtk_range_set_value (GTK_RANGE(rf_gain_scale), adc[0].gain);
    gtk_range_set_increments (GTK_RANGE(rf_gain_scale), 1.0, 1.0);
    double steps = 1.0;

    if (adc[0].max_gain > 10) { steps = 10.0; }

    if (adc[0].max_gain > 99) { steps = 20.0; }

    for (double i = adc[0].min_gain; i <= adc[0].max_gain; i += steps) {
      gtk_scale_add_mark(GTK_SCALE(rf_gain_scale), i, GTK_POS_TOP, NULL);
    }

    rf_gain_scale_signal_id = g_signal_connect(G_OBJECT(rf_gain_scale), "value_changed",
                              G_CALLBACK(rf_gain_value_changed_cb), NULL);
    gtk_widget_set_margin_start(rf_gain_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_margin_end(rf_gain_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(rf_gain_scale, FALSE);  // fülle Box nicht nach rechts

    if (strcmp(radio->name, "sdrplay") == 0) {
      gtk_widget_set_tooltip_text(rf_gain_scale, "[RFGR] Set RF Gain Reduction:\n\n"
                                                 "0 = no RF Gain Reduction\n"
                                                 "higher Value = increase RF Gain Reduction\n"
                                                 "(Range of RF Gain Reduction is device-dependent)");
    }

    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_right), rf_gain_scale, TRUE, TRUE, 0);
    //-------------------------------------------------------------------------------------------
    nr_btn = gtk_button_new_with_label(nr_labels[active_receiver->nr]);
    nr_context = gtk_widget_get_style_context(nr_btn);

    if (active_receiver->nr > 0) {
      gtk_style_context_add_class(nr_context, "active");
    } else {
      gtk_style_context_remove_class(nr_context, "active");
    }

    gtk_widget_set_name(nr_btn, "medium_toggle_button");
    gtk_widget_set_tooltip_text(nr_btn, "Set Noise Reduction type:\n"
#ifdef EXTNR
                                        "OFF → NR → NR2 → NR3 → NR4");
#else
                                        "OFF → NR → NR2");
#endif
    // begin label definition inside button
    nr_label = gtk_bin_get_child(GTK_BIN(nr_btn));
    gtk_label_set_justify(GTK_LABEL(nr_label), GTK_JUSTIFY_CENTER);
    // end label definition
    gtk_widget_set_size_request(nr_btn, box_right_width / 6, -1);  // z.B. 100px
    gtk_widget_set_margin_top(nr_btn, 0);
    gtk_widget_set_margin_bottom(nr_btn, 0);
    gtk_widget_set_margin_start(nr_btn, 0);
    gtk_widget_set_margin_end(nr_btn, 0);
    gtk_widget_set_halign(nr_btn, GTK_ALIGN_START);
    gtk_widget_set_valign(nr_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(nr_btn, FALSE);  // fülle Box nicht nach rechts
    nr_btn_signal_id = g_signal_connect(nr_btn, "pressed", G_CALLBACK(nr_btn_pressed_cb), NULL);
    // g_signal_connect(agc_btn, "released", G_CALLBACK(agc_btn_pressed_cb), NULL);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z1_right), nr_btn, FALSE, FALSE, 0);

    //-------------------------------------------------------------------------------------------
    if (can_transmit && display_sliders) {
      ps_btn = gtk_toggle_button_new_with_label("PS");
      // gtk_widget_set_name(snb_btn, "front_toggle_button");
      gtk_widget_set_name(ps_btn, "medium_toggle_button");
      gtk_widget_set_tooltip_text(ps_btn, "Pure Signal ON / OFF\n"
                                          "(aka Digital Predistortion [DPD] for RF)\n"
                                          "When enabled, enhances IP3 performance up to -60 dBc.\n\n"
                                          "Please check first PS Menu for correct settings.\n"
                                          "When using an external PA, an RF sampler is required\n"
                                          "to provide RF signal feedback to the SDR.");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ps_btn), transmitter->puresignal);
      // begin label definition inside button
      ps_label = gtk_bin_get_child(GTK_BIN(ps_btn));
      gtk_label_set_justify(GTK_LABEL(ps_label), GTK_JUSTIFY_CENTER);
      // end label definition
      gtk_widget_set_size_request(ps_btn, box_right_width / 6, -1);  // z.B. 100px
      gtk_widget_set_margin_top(ps_btn, 0);
      gtk_widget_set_margin_bottom(ps_btn, 0);
      gtk_widget_set_margin_end(ps_btn, 0);    // rechter Rand (Ende)
      gtk_widget_set_margin_start(ps_btn, 0);    // linker Rand (Anfang)
      gtk_widget_set_halign(ps_btn, GTK_ALIGN_START);
      gtk_widget_set_valign(ps_btn, GTK_ALIGN_CENTER);
      ps_btn_signal_id = g_signal_connect(G_OBJECT(ps_btn), "toggled", G_CALLBACK(ps_toggle_cb), NULL);
      // Widgets in Box packen
      gtk_box_pack_start(GTK_BOX(box_Z1_right), ps_btn, FALSE, FALSE, 0);
    } else {
      ps_btn = NULL;
      ps_label = NULL;
    }

    //-------------------------------------------------------------------------------------------
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
    GtkWidget *box_Z1_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z1_right, box_right_width, widget_height);
    gtk_box_set_spacing(GTK_BOX(box_Z1_right), 5);
    //-----------------------------------------------------------------------------------------------------------
    attenuation_label = gtk_label_new("ATT");
    gtk_widget_set_name(attenuation_label, "boldlabel_border_blue");
    // Label breiter erzwingen
    gtk_widget_set_size_request(attenuation_label, 90, -1);  // z.B. 100px
    gtk_widget_set_margin_top(attenuation_label, 0);
    gtk_widget_set_margin_bottom(attenuation_label, 0);
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
  c25_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
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
    GtkWidget *box_Z2_left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z2_left, box_left_width, widget_height);
    gtk_box_set_spacing(GTK_BOX(box_Z2_left), 5);
    //-----------------------------------------------------------------------------------------------------------
    char _label[32];
    snprintf(_label, 32, "Mic Gain");
    mic_gain_label = gtk_label_new(_label);
    gtk_widget_set_name(mic_gain_label, "boldlabel_border_blue");
    // Label breiter erzwingen
    gtk_widget_set_size_request(mic_gain_label, 105, -1);  // z.B. 100px
    gtk_widget_set_margin_top(mic_gain_label, 0);
    gtk_widget_set_margin_bottom(mic_gain_label, 0);
    gtk_widget_set_halign(mic_gain_label, GTK_ALIGN_START);
    gtk_widget_set_valign(mic_gain_label, GTK_ALIGN_CENTER);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z2_left), mic_gain_label, FALSE, FALSE, 0);

    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    if (optimize_for_touchscreen) {
      mic_gain_scale = gtk_spin_button_new_with_range(-12.0, 50.0, 1.0);
      gtk_widget_set_name(mic_gain_scale, "front_spin_button");
      gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(mic_gain_scale), TRUE);
      gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(mic_gain_scale), TRUE);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(mic_gain_scale), (double)transmitter->mic_gain);
      gtk_widget_set_margin_top(mic_gain_scale, 5);
      gtk_widget_set_margin_bottom(mic_gain_scale, 5);
      gtk_widget_set_margin_start(mic_gain_scale, 0);
      gtk_widget_set_margin_end(mic_gain_scale, 0);  // rechter Rand (Ende)
      gtk_widget_set_hexpand(mic_gain_scale, FALSE);  // fülle Box nicht nach rechts
      gtk_widget_set_halign(mic_gain_scale, GTK_ALIGN_START);
      gtk_box_pack_start(GTK_BOX(box_Z2_left), mic_gain_scale, FALSE, FALSE, 0);
    } else {
      mic_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 50.0, 1.0);
      gtk_widget_set_margin_start(mic_gain_scale, 0);  // rechter Rand (Ende)
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
    //-----------------------------------------------------------------------------------------------------------
    preamp_btn = gtk_toggle_button_new_with_label("Mic PreA");
    gtk_widget_set_name(preamp_btn, "medium_toggle_button");
    char preamp_tip[256];

    if (transmitter->addgain_enable) {
      snprintf(preamp_tip, sizeof(preamp_tip),
               "Switch the Mic Preamplifier ON or OFF\n"
               "[Always OFF in DIGU/DIGL]\n"
               "Additional gain on top of Mic Gain\n"
               "Current Mic Gain %+ddb + Current Preamp Gain %+ddb = %+ddb\n\n"
               "Adjust this value in Menu → TX Menu.", (int)transmitter->mic_gain, (int)transmitter->addgain_gain,
               (int)(transmitter->mic_gain + transmitter->addgain_gain));
    } else {
      snprintf(preamp_tip, sizeof(preamp_tip),
               "Switch the Mic Preamplifier ON or OFF\n"
               "[Always OFF in DIGU/DIGL]\n"
               "Additional gain on top of Mic Gain\n"
               "Current Mic Gain %+ddb\n\n"
               "Adjust this value in Menu → TX Menu.", (int)transmitter->mic_gain);
    }

    gtk_widget_set_tooltip_text(preamp_btn, preamp_tip);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preamp_btn), transmitter->addgain_enable);
    // begin label definition inside button
    preamp_label = gtk_bin_get_child(GTK_BIN(preamp_btn));
    gtk_label_set_justify(GTK_LABEL(preamp_label), GTK_JUSTIFY_CENTER);
    // end label definition
    gtk_widget_set_size_request(preamp_btn, box_middle_width / 6, -1);  // z.B. 100px
    gtk_widget_set_margin_top(preamp_btn, 0);
    gtk_widget_set_margin_bottom(preamp_btn, 0);
    gtk_widget_set_margin_start(preamp_btn, 0);
    gtk_widget_set_margin_end(preamp_btn, 5);
    gtk_widget_set_halign(preamp_btn, GTK_ALIGN_START);
    gtk_widget_set_valign(preamp_btn, GTK_ALIGN_CENTER);
    preamp_btn_signal_id = g_signal_connect(preamp_btn, "toggled", G_CALLBACK(preamp_btn_toggle_cb), NULL);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z2_left), preamp_btn, FALSE, FALSE, 0);
    //-----------------------------------------------------------------------------------------------------------
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    gtk_grid_attach(GTK_GRID(sliders), box_Z2_left, 0, 1, 1, 1);  // Spalte 0 Zeile 1
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    //-----------------------------------------------------------------------------------------------------------
    // Hauptcontainer: horizontale Box für TX Pwr
    GtkWidget *box_Z2_middle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z2_middle, box_middle_width, widget_height);
    gtk_box_set_spacing(GTK_BOX(box_Z2_middle), 5);

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
    gtk_widget_set_margin_top(drive_label, 0);
    gtk_widget_set_margin_bottom(drive_label, 0);
    gtk_widget_set_margin_end(drive_label, 0);    // rechter Rand (Ende)
    gtk_widget_set_margin_start(drive_label, 0);    // linker Rand (Anfang)
    gtk_widget_set_halign(drive_label, GTK_ALIGN_START);
    gtk_widget_set_valign(drive_label, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box_Z2_middle), drive_label, FALSE, FALSE, 0);
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    if (device == DEVICE_HERMES_LITE2 && pa_enabled) {
      if (optimize_for_touchscreen) {
        drive_scale = gtk_spin_button_new_with_range(0.0, 5.0, 0.1);
        gtk_widget_set_name(drive_scale, "front_spin_button");
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(drive_scale), TRUE);
        gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(drive_scale), TRUE);
        gtk_widget_set_margin_top(drive_scale, 5);
        gtk_widget_set_margin_bottom(drive_scale, 5);
        gtk_widget_set_margin_start(drive_scale, 0);
        gtk_widget_set_margin_end(drive_scale, 0);  // rechter Rand (Ende)
        gtk_widget_set_hexpand(drive_scale, FALSE);  // fülle Box nicht nach rechts
        gtk_box_pack_start(GTK_BOX(box_Z2_middle), drive_scale, FALSE, FALSE, 0);
      } else {
        drive_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 5.0, 0.1);
        gtk_widget_set_margin_end(drive_scale, 0);  // rechter Rand (Ende)
        gtk_widget_set_hexpand(drive_scale, FALSE);  // fülle Box nicht nach rechts
        gtk_box_pack_start(GTK_BOX(box_Z2_middle), drive_scale, TRUE, TRUE, 0);
      }

      snprintf(txpwr_ttip_txt, sizeof(txpwr_ttip_txt), "Set TX Pwr in W ≙ %.0f %%", radio_get_drive());
      gtk_widget_set_tooltip_text(drive_scale, txpwr_ttip_txt);
    } else {
      if (optimize_for_touchscreen) {
        drive_scale = gtk_spin_button_new_with_range(0.0, drive_max, 1.00);
        gtk_widget_set_name(drive_scale, "front_spin_button");
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(drive_scale), TRUE);
        gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(drive_scale), TRUE);
        gtk_widget_set_margin_top(drive_scale, 5);
        gtk_widget_set_margin_bottom(drive_scale, 5);
        gtk_widget_set_margin_start(drive_scale, 0);
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

#ifdef SOAPYSDR

  if (!can_transmit && display_sliders && device == SOAPYSDR_USB_DEVICE) {
    if ((strcmp(radio->name, "sdrplay") == 0)) {
      //-----------------------------------------------------------------------------------------------------------
      // Hauptcontainer: horizontale Box für TX Pwr
      GtkWidget *box_Z2_middle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
      gtk_widget_set_size_request(box_Z2_middle, box_middle_width, widget_height);
      gtk_box_set_spacing(GTK_BOX(box_Z2_middle), 5);

      //-----------------------------------------------------------------------------------------------------------
      if (radio->info.soapy.rx_has_automatic_gain) {
        //---------------------------------------------------------------------------------------------------------
        hwagc_btn = gtk_toggle_button_new_with_label("HW-AGC");
        gtk_widget_set_name(hwagc_btn, "medium_toggle_button");
        char hwagc_tip[1024];
        snprintf(hwagc_tip, sizeof(hwagc_tip), "%s %s Hardware-AGC ON/OFF\n\n"
                                               "If Hardware AGC is ON:\n"
                                               "[IFGR] setting will be ignored and overwritten by the Hardware AGC.\n"
                                               "[RFGR] setting works as an attenuator at the RF frontend.\n\n"
                                               "If Hardware AGC is OFF:\n"
                                               "[IFGR] and [RFGR] settings are active.",
                 radio->info.soapy.driver_key, radio->info.soapy.hardware_key);
        gtk_widget_set_tooltip_text(hwagc_btn, hwagc_tip);
        gtk_widget_set_size_request(hwagc_btn, 90, -1);  // z.B. 100px
        gtk_widget_set_margin_top(hwagc_btn, 0);
        gtk_widget_set_margin_bottom(hwagc_btn, 0);
        gtk_widget_set_margin_end(hwagc_btn, 0);    // rechter Rand (Ende)
        gtk_widget_set_margin_start(hwagc_btn, 0);    // linker Rand (Anfang)
        gtk_widget_set_halign(hwagc_btn, GTK_ALIGN_START);
        gtk_widget_set_valign(hwagc_btn, GTK_ALIGN_CENTER);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(hwagc_btn), adc[active_receiver->adc].agc);
        // begin label definition inside button
        hwagc_label = gtk_bin_get_child(GTK_BIN(hwagc_btn));

        // end label definition
        if (adc[active_receiver->adc].agc) {
          gtk_label_set_text(GTK_LABEL(hwagc_label), "HW-AGC");
        } else {
          gtk_label_set_text(GTK_LABEL(hwagc_label), "IFGR");
        }

        hwagc_btn_signal_id = g_signal_connect(hwagc_btn, "toggled", G_CALLBACK(hwagc_btn_toggle_cb), NULL);
        // Widgets in Box packen
        gtk_box_pack_start(GTK_BOX(box_Z2_middle), hwagc_btn, FALSE, FALSE, 0);
        //---------------------------------------------------------------------------------------------------------
        hwagc_scale = gtk_spin_button_new_with_range(-60, 0, 1);
        gtk_widget_set_name(hwagc_scale, "front_spin_button");
        gtk_widget_set_tooltip_text(hwagc_scale, "AGC_Setpoint defines the target level (dbFS)\n"
                                                 "to which the SDRplay hardware AGC regulates.\n\n"
                                                 "Unit: dBFS (dB Full Scale)\n"
                                                 "Valid range: -60 bis 0 dBFS\n"
                                                 "Default: -30 dBFS");
        gtk_widget_set_size_request(hwagc_scale, box_middle_width / 6, -1);  // z.B. 100px
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(hwagc_scale), TRUE);
        gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(hwagc_scale), TRUE);
        gtk_widget_set_margin_top(hwagc_scale, 5);
        gtk_widget_set_margin_bottom(hwagc_scale, 5);
        gtk_widget_set_margin_start(hwagc_scale, 0);
        gtk_widget_set_margin_end(hwagc_scale, 0);  // rechter Rand (Ende)
        gtk_widget_set_halign(hwagc_scale, GTK_ALIGN_START);
        gtk_widget_set_valign(hwagc_scale, GTK_ALIGN_CENTER);
        gtk_widget_set_hexpand(hwagc_scale, FALSE);  // fülle Box nicht nach rechts
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(hwagc_scale), soapy_protocol_get_agc_setpoint(active_receiver));
        hwagc_scale_signal_id = g_signal_connect(G_OBJECT(hwagc_scale), "value_changed",
                                G_CALLBACK(hwagc_scale_value_changed_cb), NULL);
        gtk_box_pack_start(GTK_BOX(box_Z2_middle), hwagc_scale, FALSE, FALSE, 0);
        //---------------------------------------------------------------------------------------------------------
        // hole Daten...
        int check_ifgr_index = index_if_gains();

        if (check_ifgr_index >= 0) {
          SoapySDRRange ifgr_range = radio->info.soapy.rx_range[index_if_gains()];

          if (ifgr_range.step == 0.0) { ifgr_range.step = 1.0; }

          ifgr_scale = gtk_spin_button_new_with_range((int)ifgr_range.minimum, (int)ifgr_range.maximum, (int)ifgr_range.step);
          char ifgr_tip[1024];
          snprintf(ifgr_tip, sizeof(ifgr_tip), "[IFGR] IF Gain Reduction\n"
                                               "Controls the gain after the LNA (RF stage) in the IF section.\n"
                                               "Operates in dB and reduces the gain in discrete steps.\n"
                                               "Unit: db\n"
                                               "Valid range: %d - %d\n\n"
                                               "Cannot be used when hardware AGC is enabled.",
                   (int)ifgr_range.minimum, (int)ifgr_range.maximum);
          gtk_widget_set_tooltip_text(ifgr_scale, ifgr_tip);
          gtk_widget_set_name(ifgr_scale, "front_spin_button");
          gtk_widget_set_size_request(ifgr_scale, box_middle_width / 6, -1);  // z.B. 100px
          gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(ifgr_scale), TRUE);
          gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(ifgr_scale), TRUE);
          gtk_widget_set_margin_top(ifgr_scale, 5);
          gtk_widget_set_margin_bottom(ifgr_scale, 5);
          gtk_widget_set_margin_start(ifgr_scale, 0);
          gtk_widget_set_margin_end(ifgr_scale, 0);  // rechter Rand (Ende)
          gtk_widget_set_halign(ifgr_scale, GTK_ALIGN_START);
          gtk_widget_set_valign(ifgr_scale, GTK_ALIGN_CENTER);
          gtk_widget_set_hexpand(ifgr_scale, FALSE);  // fülle Box nicht nach rechts
          int ifgr_value = soapy_protocol_get_gain_element(active_receiver, radio->info.soapy.rx_gain[index_if_gains()]);
          gtk_spin_button_set_value(GTK_SPIN_BUTTON(ifgr_scale), ifgr_value);
          ifgr_scale_signal_id = g_signal_connect(G_OBJECT(ifgr_scale), "value_changed",
                                                  G_CALLBACK(ifgr_scale_value_changed_cb), NULL);
          gtk_box_pack_start(GTK_BOX(box_Z2_middle), ifgr_scale, FALSE, FALSE, 0);

          //---------------------------------------------------------------------------------------------------------
          if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(hwagc_btn))) {
            gtk_widget_set_sensitive(hwagc_scale, TRUE);
            gtk_widget_set_sensitive(ifgr_scale, FALSE);
          } else {
            gtk_widget_set_sensitive(hwagc_scale, FALSE);
            gtk_widget_set_sensitive(ifgr_scale, TRUE);
          }
        }
      } else {
        hwagc_btn = NULL;
        hwagc_label = NULL;
        hwagc_scale = NULL;
        ifgr_scale = NULL;
      }

      //-----------------------------------------------------------------------------------------------------------
      // ins Grid
      gtk_grid_attach(GTK_GRID(sliders), box_Z2_middle, 1, 1, 1, 1);  // Spalte 0 Zeile 1
    }
  }

#endif
  //-----------------------------------------------------------------------------------------------------------
  // Hauptcontainer: horizontale Box für SQL
  GtkWidget *box_Z2_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(box_Z2_right, box_right_width, widget_height);
  gtk_box_set_spacing(GTK_BOX(box_Z2_right), 5);
  //-----------------------------------------------------------------------------------------------------------
  squelch_enable = gtk_toggle_button_new_with_label("SQL");
  gtk_widget_set_name(squelch_enable, "medium_toggle_button");
  gtk_widget_set_tooltip_text(squelch_enable, "Squelch ON / OFF");
  gtk_widget_set_size_request(squelch_enable, 90, -1);  // z.B. 100px
  gtk_widget_set_margin_top(squelch_enable, 0);
  gtk_widget_set_margin_bottom(squelch_enable, 0);
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
  gtk_widget_set_size_request(squelch_scale, box_right_width * 2 / 6, -1);  // z.B. 100px
  gtk_widget_set_hexpand(squelch_scale, FALSE);  // fülle Box nicht nach rechts
  gtk_range_set_increments(GTK_RANGE(squelch_scale), 1.0, 1.0);
  gtk_range_set_value(GTK_RANGE(squelch_scale), active_receiver->squelch);
  gtk_widget_set_tooltip_text(squelch_scale, "Set Squelch Threshold");

  for (int i = 0; i <= 100; i += 25) {
    gtk_scale_add_mark(GTK_SCALE(squelch_scale), i, GTK_POS_TOP, NULL);
  }

  squelch_signal_id = g_signal_connect(G_OBJECT(squelch_scale), "value_changed", G_CALLBACK(squelch_value_changed_cb),
                                       NULL);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(box_Z2_right), squelch_scale, TRUE, TRUE, 0);
  //-------------------------------------------------------------------------------------------
  binaural_btn = gtk_toggle_button_new_with_label("BIN");
  gtk_widget_set_name(binaural_btn, "medium_toggle_button");
  // gtk_widget_set_name(binaural_btn, "front_toggle_button");
  gtk_widget_set_tooltip_text(binaural_btn, "Outputs I and Q on the Left\n"
                                            "and Right audio channels");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(binaural_btn), active_receiver->binaural);
  // begin label definition inside button
  binaural_label = gtk_bin_get_child(GTK_BIN(binaural_btn));
  gtk_label_set_justify(GTK_LABEL(binaural_label), GTK_JUSTIFY_CENTER);
  // end label definition
  gtk_widget_set_size_request(binaural_btn, box_right_width / 6, -1);  // z.B. 100px
  gtk_widget_set_margin_top(binaural_btn, 0);
  gtk_widget_set_margin_bottom(binaural_btn, 0);
  gtk_widget_set_margin_end(binaural_btn, 0);    // rechter Rand (Ende)
  gtk_widget_set_margin_start(binaural_btn, 0);    // linker Rand (Anfang)
  gtk_widget_set_halign(binaural_btn, GTK_ALIGN_START);
  gtk_widget_set_valign(binaural_btn, GTK_ALIGN_CENTER);
  binaural_btn_signal_id = g_signal_connect(G_OBJECT(binaural_btn), "toggled", G_CALLBACK(binaural_toggle_cb), NULL);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(box_Z2_right), binaural_btn, FALSE, FALSE, 0);
  //-------------------------------------------------------------------------------------------
  snb_btn = gtk_toggle_button_new_with_label("SNB");
  // gtk_widget_set_name(snb_btn, "front_toggle_button");
  gtk_widget_set_name(snb_btn, "medium_toggle_button");
  gtk_widget_set_tooltip_text(snb_btn, "Spectral Noise Blanker");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(snb_btn), active_receiver->snb);
  // begin label definition inside button
  snb_label = gtk_bin_get_child(GTK_BIN(snb_btn));
  gtk_label_set_justify(GTK_LABEL(snb_label), GTK_JUSTIFY_CENTER);
  // end label definition
  gtk_widget_set_size_request(snb_btn, box_right_width / 6, -1);  // z.B. 100px
  gtk_widget_set_margin_top(snb_btn, 0);
  gtk_widget_set_margin_bottom(snb_btn, 0);
  gtk_widget_set_margin_end(snb_btn, 0);    // rechter Rand (Ende)
  gtk_widget_set_margin_start(snb_btn, 0);    // linker Rand (Anfang)
  gtk_widget_set_halign(snb_btn, GTK_ALIGN_START);
  gtk_widget_set_valign(snb_btn, GTK_ALIGN_CENTER);
  snb_btn_signal_id = g_signal_connect(G_OBJECT(snb_btn), "toggled", G_CALLBACK(snb_toggle_cb), NULL);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(box_Z2_right), snb_btn, FALSE, FALSE, 0);
  //-------------------------------------------------------------------------------------------
  // Box ins Grid Spalte 3 Zeile 2
  gtk_grid_attach(GTK_GRID(sliders), box_Z2_right, 2, 1, 1, 1);  // Zeile 0 Spalte 2

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (can_transmit && display_sliders) {
    //-----------------------------------------------------------------------------------------------------------
    // Hauptcontainer: horizontale Box für TUNE DRV + MicPreAmp
    GtkWidget *box_Z3_left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z3_left, box_left_width, widget_height);
    gtk_box_set_spacing(GTK_BOX(box_Z3_left), 5);
    //-----------------------------------------------------------------------------------------------------------
    // tune_drive_button
    tune_drive_btn = gtk_toggle_button_new_with_label("TUNE");
    gtk_widget_set_name(tune_drive_btn, "front_toggle_button");
    // gtk_widget_set_name(tune_drive_btn, "medium_toggle_button");

    if (!transmitter->tune_use_drive) {
      gtk_widget_set_tooltip_text(tune_drive_btn, "TUNE with TUNE Drive:\nSet tune level in percent of maximum TX PWR");
    } else if (transmitter->tune_use_drive) {
      gtk_widget_set_tooltip_text(tune_drive_btn, "TUNE Drive = TX PWR");
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tune_drive_btn), !(radio_get_tune()));
    // begin label definition inside button
    tune_drive_label = gtk_bin_get_child(GTK_BIN(tune_drive_btn));
    gtk_label_set_justify(GTK_LABEL(tune_drive_label), GTK_JUSTIFY_CENTER);
    // end label definition
    // Label breiter erzwingen
    gtk_widget_set_size_request(tune_drive_btn, 105, -1);  // z.B. 100px
    gtk_widget_set_margin_top(tune_drive_btn, 0);
    gtk_widget_set_margin_bottom(tune_drive_btn, 0);
    gtk_widget_set_margin_end(tune_drive_btn, 0);    // rechter Rand (Ende)
    gtk_widget_set_halign(tune_drive_btn, GTK_ALIGN_START);
    gtk_widget_set_valign(tune_drive_btn, GTK_ALIGN_CENTER);
    tune_drive_btn_signal_id = g_signal_connect(G_OBJECT(tune_drive_btn), "toggled", G_CALLBACK(tune_drive_toggle_cb),
                               NULL);
    gtk_box_pack_start(GTK_BOX(box_Z3_left), tune_drive_btn, FALSE, FALSE, 0);
    //-------------------------------------------------------------------------------------------
    // tune_drive_scale
    tune_drive_scale = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_widget_set_name(tune_drive_scale, "front_spin_button");
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
    gtk_box_pack_start(GTK_BOX(box_Z3_left), tune_drive_scale, FALSE, FALSE, 0);
    //-----------------------------------------------------------------------------------------------------------
    split_btn = gtk_toggle_button_new_with_label("VFO\nSplit");
    gtk_widget_set_name(split_btn, "medium_toggle_button");
    gtk_widget_set_tooltip_text(split_btn, "Enable split mode:\n\n"
                                           "In split mode with a single receiver:\n"
                                           "RX is on VFO A and TX is on VFO B.\n\n"
                                           "In split mode with two receivers:\n"
                                           "RX1 is for RX and RX2 is for TX.\n\n"
                                           "Note: When split mode is activated, VFO B is set to VFO A once.");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_btn), split);
    // begin label definition inside button
    split_label = gtk_bin_get_child(GTK_BIN(split_btn));
    gtk_label_set_justify(GTK_LABEL(split_label), GTK_JUSTIFY_CENTER);
    // end label definition
    gtk_widget_set_size_request(split_btn, 55, -1);  // z.B. 100px
    gtk_widget_set_margin_top(split_btn, 0);
    gtk_widget_set_margin_bottom(split_btn, 0);
    gtk_widget_set_margin_start(split_btn, 3);
    gtk_widget_set_margin_end(split_btn, 0);
    gtk_widget_set_halign(split_btn, GTK_ALIGN_START);
    gtk_widget_set_valign(split_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(split_btn, FALSE);  // fülle Box nicht nach rechts
    split_btn_signal_id = g_signal_connect(split_btn, "toggled", G_CALLBACK(split_btn_toggle_cb), NULL);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z3_left), split_btn, FALSE, FALSE, 0);
    //-----------------------------------------------------------------------------------------------------------
    swap_btn = gtk_button_new_with_label("VFO\nA←→B");
    gtk_widget_set_name(swap_btn, "medium_toggle_button");
    gtk_widget_set_tooltip_text(swap_btn, "Swap VFO A←→B");
    // begin label definition inside button
    swap_label = gtk_bin_get_child(GTK_BIN(swap_btn));
    gtk_label_set_justify(GTK_LABEL(swap_label), GTK_JUSTIFY_CENTER);
    // end label definition
    gtk_widget_set_size_request(swap_btn, 55, -1);  // z.B. 100px
    gtk_widget_set_margin_top(swap_btn, 0);
    gtk_widget_set_margin_bottom(swap_btn, 0);
    gtk_widget_set_margin_start(swap_btn, 3);
    gtk_widget_set_margin_end(swap_btn, 0);
    gtk_widget_set_halign(swap_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(swap_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(swap_btn, FALSE);  // fülle Box nicht nach rechts
    g_signal_connect(swap_btn, "pressed", G_CALLBACK(swap_btn_pressed_cb), NULL);
    g_signal_connect(swap_btn, "released", G_CALLBACK(swap_btn_released_cb), NULL);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z3_left), swap_btn, FALSE, FALSE, 0);
    //-----------------------------------------------------------------------------------------------------------
    equal_btn = gtk_button_new_with_label("VFO\nA=B");
    gtk_widget_set_name(equal_btn, "medium_toggle_button");
    gtk_widget_set_tooltip_text(equal_btn, "Set VFO A = VFO B");
    // begin label definition inside button
    equal_label = gtk_bin_get_child(GTK_BIN(equal_btn));
    gtk_label_set_justify(GTK_LABEL(equal_label), GTK_JUSTIFY_CENTER);
    // end label definition
    gtk_widget_set_size_request(equal_btn, 55, -1);  // z.B. 100px
    gtk_widget_set_margin_top(equal_btn, 0);
    gtk_widget_set_margin_bottom(equal_btn, 0);
    gtk_widget_set_margin_start(equal_btn, 3);
    gtk_widget_set_margin_end(equal_btn, 0);
    gtk_widget_set_halign(equal_btn, GTK_ALIGN_END);
    gtk_widget_set_valign(equal_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(equal_btn, FALSE);  // fülle Box nicht nach rechts
    g_signal_connect(equal_btn, "pressed", G_CALLBACK(equal_btn_pressed_cb), NULL);
    g_signal_connect(equal_btn, "released", G_CALLBACK(equal_btn_released_cb), NULL);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z3_left), equal_btn, FALSE, FALSE, 0);
    //-----------------------------------------------------------------------------------------------------------
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
    gtk_grid_attach(GTK_GRID(sliders), box_Z3_left, 0, 2, 1, 1);
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    //-------------------------------------------------------------------------------------------
    if (n_input_devices > 0) {
      //-----------------------------------------------------------------------------------------------------------
      // Hauptcontainer: horizontale Box für TUNE DRV + MicPreAmp
      GtkWidget *box_Z3_middle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
      gtk_widget_set_size_request(box_Z3_middle, box_middle_width, widget_height);
      gtk_box_set_spacing(GTK_BOX(box_Z3_middle), 5);
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
      gtk_widget_set_margin_start(local_mic_input, 3);
      gtk_widget_set_margin_end(local_mic_input, 3);  // rechter Rand (Ende)
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
    GtkWidget *box_Z3_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z3_right, box_right_width, widget_height);
    gtk_box_set_spacing(GTK_BOX(box_Z3_right), 5);
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
    gtk_widget_set_name(bbcompr_scale, "front_spin_button");
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(bbcompr_scale), TRUE);
    gtk_widget_set_tooltip_text(bbcompr_scale, "Speech Processor Gain in db");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(bbcompr_scale), (double)transmitter->compressor_level);
    gtk_widget_set_margin_top(bbcompr_scale, 5);
    gtk_widget_set_margin_bottom(bbcompr_scale, 5);
    gtk_widget_set_margin_start(bbcompr_scale, 3);
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
    gtk_widget_set_margin_start(lev_btn, 0);
    gtk_widget_set_halign(lev_btn, GTK_ALIGN_START);
    gtk_widget_set_valign(lev_btn, GTK_ALIGN_CENTER);
    lev_btn_signal_id = g_signal_connect(lev_btn, "toggled", G_CALLBACK(lev_btn_toggle_cb), NULL);
    //-------------------------------------------------------------------------------------------
    lev_scale = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
    gtk_widget_set_name(lev_scale, "front_spin_button");
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(lev_scale), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(lev_scale), TRUE);
    gtk_widget_set_tooltip_text(lev_scale, "Leveler Gain in db");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(lev_scale), (double)transmitter->lev_gain);
    gtk_widget_set_margin_top(lev_scale, 5);
    gtk_widget_set_margin_bottom(lev_scale, 5);
    gtk_widget_set_margin_start(lev_scale, 3);
    gtk_widget_set_margin_end(lev_scale, 0);  // rechter Rand (Ende)
    gtk_widget_set_hexpand(lev_scale, FALSE);  // fülle Box nicht nach rechts
    lev_scale_signal_id = g_signal_connect(G_OBJECT(lev_scale), "value_changed", G_CALLBACK(lev_scale_changed_cb), NULL);
    // Widgets in Box packen
    gtk_box_pack_start(GTK_BOX(box_Z3_right), lev_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_Z3_right), lev_scale, FALSE, FALSE, 0);
    // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
    gtk_grid_attach(GTK_GRID(sliders), box_Z3_right, 2, 2, 1, 1);

    // sanity check, if DIGIMODE selected set PreAmp, SNB, BBCOMPR and LEV inactive
    if (selected_mode == modeDIGL || selected_mode == modeDIGU) {
      gtk_widget_set_sensitive(preamp_btn, FALSE);
      gtk_widget_set_sensitive(bbcompr_scale, FALSE);
      gtk_widget_set_sensitive(bbcompr_btn, FALSE);
      gtk_widget_set_sensitive(lev_scale, FALSE);
      gtk_widget_set_sensitive(lev_btn, FALSE);
      // gtk_widget_set_sensitive(snb_btn, FALSE);
      gtk_widget_queue_draw(sliders);
    }
  } else {
    tune_drive_label = NULL;
    tune_drive_btn = NULL;
    tune_drive_scale = NULL;
    split_btn = NULL;
    split_label = NULL;
    swap_btn = NULL;
    swap_label = NULL;
    equal_btn = NULL;
    equal_label = NULL;
    local_mic_label = NULL;
    local_mic_button = NULL;
    local_mic_input = NULL;
    bbcompr_label = NULL;
    bbcompr_btn = NULL;
    bbcompr_scale = NULL;
    lev_label = NULL;
    lev_btn = NULL;
    lev_scale = NULL;
  }

  gtk_widget_show_all(sliders);
  return sliders;
}
