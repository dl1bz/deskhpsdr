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
#ifdef CLIENT_SERVER
  #include "client_server.h"
#endif
#include "actions.h"
#include "message.h"

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

  if (radio_is_remote) {
#ifdef CLIENT_SERVER
    send_attenuation(client_socket, active_receiver->id, (int)adc[active_receiver->adc].attenuation);
#endif
  } else {
    schedule_high_priority();
  }
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

  if (radio_is_remote) {
#ifdef CLIENT_SERVER
    send_agc_gain(client_socket, active_receiver->id, active_receiver->agc_gain, active_receiver->agc_hang,
                  active_receiver->agc_thresh, active_receiver->agc_hang_threshold);
#endif
  } else {
    rx_set_agc(active_receiver);
  }
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

  if (radio_is_remote) {
#ifdef CLIENT_SERVER
    send_volume(client_socket, active_receiver->id, active_receiver->volume);
#endif
    return;
  }

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

  if (radio_is_remote) {
#ifdef CLIENT_SERVER
    send_rfgain(client_socket, active_receiver->id, adc[active_receiver->adc].gain);
#endif
    return;
  }

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

void set_filter_width(int rx, int width) {
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

void set_filter_shift(int rx, int shift) {
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
    gtk_range_set_value (GTK_RANGE(drive_scale), value);
  } else {
    show_popup_slider(DRIVE, 0, 0.0, drive_max, 1.0, value, "TX Drive");
  }
}

static void drive_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(drive_scale));
  //t_print("%s: value=%f\n", __FUNCTION__, value);
  int txmode = vfo_get_tx_mode();

  if (txmode == modeDIGU || txmode == modeDIGL) {
    if (value > drive_digi_max) { value = drive_digi_max; }
  }

  gtk_range_set_value (GTK_RANGE(drive_scale), value);
  radio_set_drive(value);
}

void set_filter_cut_high(int rx, int var) {
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

void set_filter_cut_low(int rx, int var) {
  t_print("%s var=%d\n", __FUNCTION__, var);
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

  if (radio_is_remote) {
#ifdef CLIENT_SERVER
    send_squelch(client_socket, active_receiver->id, active_receiver->squelch_enable, active_receiver->squelch);
#endif
  } else {
    rx_set_squelch(active_receiver);
  }
}

static void squelch_enable_cb(GtkWidget *widget, gpointer data) {
  active_receiver->squelch_enable = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

  if (radio_is_remote) {
#ifdef CLIENT_SERVER
    send_squelch(client_socket, active_receiver->id, active_receiver->squelch_enable, active_receiver->squelch);
#endif
  } else {
    rx_set_squelch(active_receiver);
  }
}

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

GtkWidget *sliders_init(int my_width, int my_height) {
  width = my_width;
  height = my_height;
  t_print("sliders_init: width=%d height=%d\n", width, height);
  //
  // The larger the width, the smaller the fraction used for the label can be
  // font size.
  //
  int twidth, swidth, tpix;
  int t1pos, t2pos, t3pos;
  int s1pos, s2pos, s3pos, sqpos;
  const char *csslabel;

  if (width < 1024) {
    // label  width: 1/9 of screen width
    // slider width: 2/9 of screen width
    tpix   =  width / 9;      // width of text label in pixel
    twidth =  3;              // width of text label in grid units
    swidth =  6;              // width of slider in grid units
  } else if (width < 1280) {
    // label  width: 1/12 of screen width
    // slider width: 3/12 of screen width
    tpix   =  width / 12;
    twidth =  3;              // width of text label in pixel
    swidth =  9;              // width of slider in grid units
  } else {
    // label  width: 1/15 of screen width
    // slider width: 4/12 of screen width
    tpix   =  width / 15;
    twidth =  2;              // width of text label in pixel
    swidth =  8;              // width of slider in grid units
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

  t1pos  =  0;
  s1pos  =  t1pos + twidth;
  t2pos  =  s1pos + swidth;
  s2pos  =  t2pos + twidth;
  t3pos  =  s2pos + swidth;
  s3pos  =  t3pos + twidth;
  sqpos  =  s3pos + 1;
  sliders = gtk_grid_new();
  gtk_widget_set_size_request (sliders, width, height);
  gtk_grid_set_row_homogeneous(GTK_GRID(sliders), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(sliders), TRUE);
  af_gain_label = gtk_label_new("AF");
  gtk_widget_set_name(af_gain_label, csslabel);
  gtk_widget_set_halign(af_gain_label, GTK_ALIGN_END);
  gtk_widget_show(af_gain_label);
  gtk_grid_attach(GTK_GRID(sliders), af_gain_label, t1pos, 0, twidth, 1);
  af_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -40.0, 0.0, 1.00);
  gtk_widget_set_size_request(af_gain_scale, 0, height / 2);
  gtk_widget_set_valign(af_gain_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(af_gain_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(af_gain_scale), active_receiver->volume);
  gtk_widget_show(af_gain_scale);
  gtk_grid_attach(GTK_GRID(sliders), af_gain_scale, s1pos, 0, swidth, 1);
  g_signal_connect(G_OBJECT(af_gain_scale), "value_changed", G_CALLBACK(afgain_value_changed_cb), NULL);
  agc_gain_label = gtk_label_new("AGC");
  gtk_widget_set_name(agc_gain_label, csslabel);
  gtk_widget_set_halign(agc_gain_label, GTK_ALIGN_END);
  gtk_widget_show(agc_gain_label);
  gtk_grid_attach(GTK_GRID(sliders), agc_gain_label, t2pos, 0, twidth, 1);
  agc_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -20.0, 120.0, 1.0);
  gtk_widget_set_size_request(agc_scale, 0, height / 2);
  gtk_widget_set_valign(agc_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(agc_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(agc_scale), active_receiver->agc_gain);
  gtk_widget_show(agc_scale);
  gtk_grid_attach(GTK_GRID(sliders), agc_scale, s2pos, 0, swidth, 1);
  g_signal_connect(G_OBJECT(agc_scale), "value_changed", G_CALLBACK(agcgain_value_changed_cb), NULL);

  if (have_rx_gain) {
    rf_gain_label = gtk_label_new("RF");
    gtk_widget_set_name(rf_gain_label, csslabel);
    gtk_widget_set_halign(rf_gain_label, GTK_ALIGN_END);
    gtk_widget_show(rf_gain_label);
    gtk_grid_attach(GTK_GRID(sliders), rf_gain_label, t3pos, 0, twidth, 1);
    rf_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, adc[0].min_gain, adc[0].max_gain, 1.0);
    gtk_widget_set_size_request(rf_gain_scale, 0, height / 2);
    gtk_widget_set_valign(rf_gain_scale, GTK_ALIGN_CENTER);
    gtk_range_set_value (GTK_RANGE(rf_gain_scale), adc[0].gain);
    gtk_range_set_increments (GTK_RANGE(rf_gain_scale), 1.0, 1.0);
    gtk_widget_show(rf_gain_scale);
    gtk_grid_attach(GTK_GRID(sliders), rf_gain_scale, s3pos, 0, swidth, 1);
    g_signal_connect(G_OBJECT(rf_gain_scale), "value_changed", G_CALLBACK(rf_gain_value_changed_cb), NULL);
  } else {
    rf_gain_label = NULL;
    rf_gain_scale = NULL;
  }

  if (have_rx_att) {
    attenuation_label = gtk_label_new("ATT");
    gtk_widget_set_name(attenuation_label, csslabel);
    gtk_widget_set_halign(attenuation_label, GTK_ALIGN_END);
    gtk_widget_show(attenuation_label);
    gtk_grid_attach(GTK_GRID(sliders), attenuation_label, t3pos, 0, twidth, 1);
    attenuation_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 31.0, 1.0);
    gtk_widget_set_size_request(attenuation_scale, 0, height / 2);
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
  gtk_grid_attach(GTK_GRID(sliders), c25_container, s3pos, 0, swidth, 1);
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

  if (can_transmit) {
    mic_gain_label = gtk_label_new("Mic");
    gtk_widget_set_name(mic_gain_label, csslabel);
    gtk_widget_set_halign(mic_gain_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(sliders), mic_gain_label, t1pos, 1, twidth, 1);
    mic_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 50.0, 1.0);
    gtk_widget_set_size_request(mic_gain_scale, 0, height / 2);
    gtk_widget_set_valign(mic_gain_scale, GTK_ALIGN_CENTER);
    gtk_range_set_increments (GTK_RANGE(mic_gain_scale), 1.0, 1.0);
    gtk_grid_attach(GTK_GRID(sliders), mic_gain_scale, s1pos, 1, swidth, 1);
    gtk_range_set_value (GTK_RANGE(mic_gain_scale), transmitter->mic_gain);
    g_signal_connect(G_OBJECT(mic_gain_scale), "value_changed", G_CALLBACK(micgain_value_changed_cb), NULL);
    drive_label = gtk_label_new("TX Drv");
    gtk_widget_set_name(drive_label, csslabel);
    gtk_widget_set_halign(drive_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(sliders), drive_label, t2pos, 1, twidth, 1);
    drive_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, drive_max, 1.00);
    gtk_widget_set_size_request(drive_scale, 0, height / 2);
    gtk_widget_set_valign(drive_scale, GTK_ALIGN_CENTER);
    gtk_range_set_increments (GTK_RANGE(drive_scale), 1.0, 1.0);
    gtk_range_set_value (GTK_RANGE(drive_scale), radio_get_drive());
    gtk_widget_show(drive_scale);
    gtk_grid_attach(GTK_GRID(sliders), drive_scale, s2pos, 1, swidth, 1);
    g_signal_connect(G_OBJECT(drive_scale), "value_changed", G_CALLBACK(drive_value_changed_cb), NULL);
  } else {
    mic_gain_label = NULL;
    mic_gain_scale = NULL;
    drive_label = NULL;
    drive_scale = NULL;
  }

  squelch_label = gtk_label_new("Sqlch");
  gtk_widget_set_name(squelch_label, csslabel);
  gtk_widget_set_halign(squelch_label, GTK_ALIGN_END);
  gtk_widget_show(squelch_label);
  gtk_grid_attach(GTK_GRID(sliders), squelch_label, t3pos, 1, twidth, 1);
  squelch_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  gtk_widget_set_size_request(squelch_scale, 0, height / 2);
  gtk_widget_set_valign(squelch_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(squelch_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(squelch_scale), active_receiver->squelch);
  gtk_widget_show(squelch_scale);
  gtk_grid_attach(GTK_GRID(sliders), squelch_scale, sqpos, 1, swidth - 1, 1);
  squelch_signal_id = g_signal_connect(G_OBJECT(squelch_scale), "value_changed", G_CALLBACK(squelch_value_changed_cb),
                                       NULL);
  squelch_enable = gtk_check_button_new();
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(squelch_enable), active_receiver->squelch_enable);
  gtk_widget_show(squelch_enable);
  gtk_grid_attach(GTK_GRID(sliders), squelch_enable, s3pos, 1, 1, 1);
  gtk_widget_set_halign(squelch_enable, GTK_ALIGN_CENTER);
  g_signal_connect(squelch_enable, "toggled", G_CALLBACK(squelch_enable_cb), NULL);
  return sliders;
}
