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
#include <semaphore.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "new_menu.h"
#include "fft_menu.h"
#include "radio.h"
#include "message.h"
#include "ext.h"
#include "sliders.h"
#include "receiver.h"
#include "tci.h"

static GtkWidget *dialog = NULL;
static GtkWidget *rx_iq_gain_spin[2] = { NULL, NULL };
static GtkWidget *rx_iq_phase_spin[2] = { NULL, NULL };
static GtkWidget *rx_iq_auto_button[2] = { NULL, NULL };
static GtkWidget *rx_iq_status_label[2] = { NULL, NULL };
static volatile int rx_iq_auto_running[2] = { 0, 0 };
static volatile int rx_iq_auto_cancel[2] = { 0, 0 };

typedef struct _rx_iq_auto_result {
  int id;
  double gain;
  double phase;
  int apply;
  char status[64];
} RX_IQ_AUTO_RESULT;

typedef struct _rx_iq_auto_job {
  int id;
  double start_gain;
  double start_phase;
  double start_irr;
} RX_IQ_AUTO_JOB;

static void cleanup(void) {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    rx_iq_gain_spin[0] = NULL;
    rx_iq_gain_spin[1] = NULL;
    rx_iq_phase_spin[0] = NULL;
    rx_iq_phase_spin[1] = NULL;
    rx_iq_auto_button[0] = NULL;
    rx_iq_auto_button[1] = NULL;
    rx_iq_status_label[0] = NULL;
    rx_iq_status_label[1] = NULL;
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static void binaural_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  RECEIVER *rx = receiver[id];
  if (!rx_binaural_allowed(rx)) {
    rx->binaural = 0;
    g_signal_handlers_block_by_func(widget, G_CALLBACK(binaural_cb), data);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
    g_signal_handlers_unblock_by_func(widget, G_CALLBACK(binaural_cb), data);
    rx_set_af_binaural(rx);
    update_slider_binaural_btn();
    tci_rx_bin_enable_changed(id);
    return;
  }
  rx->binaural = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  rx_set_af_binaural(rx);
  update_slider_binaural_btn();
  tci_rx_bin_enable_changed(id);
}


static void image_measure_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  RECEIVER *rx = receiver[id];
  rx->image_measure = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void image_measure_hz_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  RECEIVER *rx = receiver[id];
  rx->image_measure_hz = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
}

static void rx_iq_set_status(int id, const char *status) {
  if (id < 0 || id >= receivers || id >= 2 || receiver[id] == NULL || status == NULL) {
    return;
  }
  g_strlcpy(receiver[id]->rx_iq_status, status, sizeof(receiver[id]->rx_iq_status));
  if (rx_iq_status_label[id] != NULL) {
    gtk_label_set_text(GTK_LABEL(rx_iq_status_label[id]), receiver[id]->rx_iq_status);
  }
}

static void rx_iq_gain_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  RECEIVER *rx = receiver[id];
  rx->rx_iq_gain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
}

static void rx_iq_phase_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  RECEIVER *rx = receiver[id];
  rx->rx_iq_phase = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
}

static void rx_iq_reset_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  RECEIVER *rx = receiver[id];
  (void)widget;
  if (id >= 0 && id < 2) {
    rx_iq_auto_cancel[id] = 1;
  }
  rx->rx_iq_gain = 0.0;
  rx->rx_iq_phase = 0.0;
  rx_iq_set_status(id, "Idle");
  if (id >= 0 && id < 2) {
    if (rx_iq_gain_spin[id] != NULL) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_iq_gain_spin[id]), 0.0);
    }
    if (rx_iq_phase_spin[id] != NULL) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_iq_phase_spin[id]), 0.0);
    }
  }
}


static gboolean rx_iq_auto_finish_cb(gpointer data) {
  RX_IQ_AUTO_RESULT *result = (RX_IQ_AUTO_RESULT *)data;
  int id;
  if (result == NULL) {
    return FALSE;
  }
  id = result->id;
  if (id >= 0 && id < 2) {
    if (result->apply && id < receivers && receiver[id] != NULL) {
      receiver[id]->rx_iq_gain = result->gain;
      receiver[id]->rx_iq_phase = result->phase;
      if (rx_iq_gain_spin[id] != NULL) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_iq_gain_spin[id]), result->gain);
      }
      if (rx_iq_phase_spin[id] != NULL) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_iq_phase_spin[id]), result->phase);
      }
    }
    rx_iq_auto_running[id] = 0;
    rx_iq_auto_cancel[id] = 0;
    if (rx_iq_auto_button[id] != NULL) {
      gtk_widget_set_sensitive(rx_iq_auto_button[id], TRUE);
    }
    rx_iq_set_status(id, result->status[0] != '\0' ? result->status : "Idle");
  }
  g_free(result);
  return FALSE;
}

static double rx_iq_auto_measure(RECEIVER *rx, double gain, double phase) {
  int id;
  int n;
  int valid = 0;
  double sum = 0.0;
  if (rx == NULL) {
    return -999.0;
  }
  id = rx->id;
  if (id < 0 || id >= 2 || rx_iq_auto_cancel[id]) {
    return -999.0;
  }
  rx->rx_iq_gain = gain;
  rx->rx_iq_phase = phase;
  /*
   * The image rejection value is produced asynchronously by the panadapter.
   * Do not evaluate immediately after changing IQ gain/phase; wait for fresh
   * display data and average a few samples to avoid optimizing on stale bins.
   */
  g_usleep(250000);
  for (n = 0; n < 5; n++) {
    if (rx_iq_auto_cancel[id]) {
      return -999.0;
    }
    if (rx->image_measure && rx->image_measure_valid) {
      sum += fabs(rx->image_rejection_db);
      valid++;
    }
    g_usleep(100000);
  }
  if (rx_iq_auto_cancel[id] || valid == 0) {
    return -999.0;
  }
  return sum / (double)valid;
}

static int rx_iq_auto_opt_gain(RECEIVER *rx, double *best_gain, double best_phase, double *best_irr,
                               double center, double range, double step, gint64 deadline) {
  double gain;
  double limit = center + range + (step * 0.5);
  for (gain = center - range; gain <= limit; gain += step) {
    double irr;
    if (g_get_monotonic_time() >= deadline || rx_iq_auto_cancel[rx->id]) {
      return 0;
    }
    irr = rx_iq_auto_measure(rx, gain, best_phase);
    if (irr > *best_irr) {
      *best_irr = irr;
      *best_gain = gain;
    }
  }
  rx->rx_iq_gain = *best_gain;
  rx->rx_iq_phase = best_phase;
  return 1;
}

static int rx_iq_auto_opt_phase(RECEIVER *rx, double best_gain, double *best_phase, double *best_irr,
                                double center, double range, double step, gint64 deadline) {
  double phase;
  double limit = center + range + (step * 0.5);
  for (phase = center - range; phase <= limit; phase += step) {
    double irr;
    if (g_get_monotonic_time() >= deadline || rx_iq_auto_cancel[rx->id]) {
      return 0;
    }
    irr = rx_iq_auto_measure(rx, best_gain, phase);
    if (irr > *best_irr) {
      *best_irr = irr;
      *best_phase = phase;
    }
  }
  rx->rx_iq_gain = best_gain;
  rx->rx_iq_phase = *best_phase;
  return 1;
}

static gpointer rx_iq_auto_thread(gpointer data) {
  RX_IQ_AUTO_JOB *job = (RX_IQ_AUTO_JOB *)data;
  RX_IQ_AUTO_RESULT *result;
  RECEIVER *rx;
  gint64 deadline;
  double best_gain;
  double best_phase;
  double best_irr;
  int apply = 0;
  if (job == NULL) {
    return NULL;
  }
  result = g_new0(RX_IQ_AUTO_RESULT, 1);
  result->id = job->id;
  result->gain = job->start_gain;
  result->phase = job->start_phase;
  result->apply = 1;
  if (job->id < 0 || job->id >= receivers || job->id >= 2 || receiver[job->id] == NULL) {
    g_free(job);
    g_idle_add(rx_iq_auto_finish_cb, result);
    return NULL;
  }
  rx = receiver[job->id];
  if (!rx->image_measure || !rx->image_measure_valid || rx->image_signal_db < -100.0) {
    t_print("RX%d auto IQ aborted: no calibration signal\n", rx->id);
    result->apply = 0;
    g_strlcpy(result->status, "No calibration signal", sizeof(result->status));
    g_free(job);
    g_idle_add(rx_iq_auto_finish_cb, result);
    return NULL;
  }
  best_gain = job->start_gain;
  best_phase = job->start_phase;
  best_irr = fabs(rx->image_rejection_db);
  deadline = g_get_monotonic_time() + (10 * G_USEC_PER_SEC);
  t_print("RX%d auto IQ started: gain=%+.4f dB phase=%+.4f deg IRR=%.1f dB\n",
          rx->id, job->start_gain, job->start_phase, best_irr);
  {
    double zero_irr = rx_iq_auto_measure(rx, 0.0, 0.0);
    if (zero_irr > -900.0) {
      t_print("RX%d auto IQ zero candidate: IRR=%.1f dB\n", rx->id, zero_irr);
    }
    if (zero_irr > best_irr) {
      best_irr = zero_irr;
      best_gain = 0.0;
      best_phase = 0.0;
    }
  }
  if (!rx_iq_auto_opt_gain(rx, &best_gain, best_phase, &best_irr, best_gain, 0.05, 0.005, deadline)) {
    goto cancelled;
  }
  if (!rx_iq_auto_opt_phase(rx, best_gain, &best_phase, &best_irr, best_phase, 0.50, 0.05, deadline)) {
    goto cancelled;
  }
  if (!rx_iq_auto_opt_gain(rx, &best_gain, best_phase, &best_irr, best_gain, 0.01, 0.001, deadline)) {
    goto cancelled;
  }
  if (!rx_iq_auto_opt_phase(rx, best_gain, &best_phase, &best_irr, best_phase, 0.10, 0.01, deadline)) {
    goto cancelled;
  }
  if (best_irr >= (job->start_irr + 0.5)) {
    apply = 1;
  }
  if (apply) {
    result->gain = best_gain;
    result->phase = best_phase;
    t_print("RX%d auto IQ complete: gain=%+.4f dB phase=%+.4f deg IRR=%.1f dB\n",
            rx->id, best_gain, best_phase, best_irr);
    snprintf(result->status, sizeof(result->status), "Complete (%.1f dB)", best_irr);
  } else {
    result->gain = job->start_gain;
    result->phase = job->start_phase;
    t_print("RX%d auto IQ complete: no improvement, restored gain=%+.4f dB phase=%+.4f deg IRR=%.1f dB\n",
            rx->id, job->start_gain, job->start_phase, job->start_irr);
    g_strlcpy(result->status, "No improvement", sizeof(result->status));
  }
  g_free(job);
  g_idle_add(rx_iq_auto_finish_cb, result);
  return NULL;
cancelled:
  if (rx_iq_auto_cancel[rx->id]) {
    result->gain = job->start_gain;
    result->phase = job->start_phase;
    result->apply = 0;
    t_print("RX%d auto IQ cancelled\n", rx->id);
    g_strlcpy(result->status, "Cancelled", sizeof(result->status));
  } else if (best_irr >= (job->start_irr + 0.5)) {
    result->gain = best_gain;
    result->phase = best_phase;
    t_print("RX%d auto IQ timeout: keeping best gain=%+.4f dB phase=%+.4f deg IRR=%.1f dB\n",
            rx->id, best_gain, best_phase, best_irr);
    snprintf(result->status, sizeof(result->status), "Complete (%.1f dB)", best_irr);
  } else {
    result->gain = job->start_gain;
    result->phase = job->start_phase;
    t_print("RX%d auto IQ aborted: timeout, restored gain=%+.4f dB phase=%+.4f deg IRR=%.1f dB\n",
            rx->id, job->start_gain, job->start_phase, job->start_irr);
    g_strlcpy(result->status, "No improvement", sizeof(result->status));
  }
  g_free(job);
  g_idle_add(rx_iq_auto_finish_cb, result);
  return NULL;
}

static void rx_iq_auto_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  RECEIVER *rx;
  RX_IQ_AUTO_JOB *job;
  if (id < 0 || id >= receivers || id >= 2 || receiver[id] == NULL) {
    return;
  }
  rx = receiver[id];
  if (rx_iq_auto_running[id]) {
    return;
  }
  if (!rx->image_measure || !rx->image_measure_valid || rx->image_signal_db < -100.0) {
    t_print("RX%d auto IQ aborted: no calibration signal\n", id);
    rx_iq_set_status(id, "No calibration signal");
    return;
  }
  rx_iq_auto_cancel[id] = 0;
  rx_iq_auto_running[id] = 1;
  rx_iq_set_status(id, "Auto calibrating...");
  if (widget != NULL) {
    gtk_widget_set_sensitive(widget, FALSE);
  }
  job = g_new0(RX_IQ_AUTO_JOB, 1);
  job->id = id;
  job->start_gain = rx->rx_iq_gain;
  job->start_phase = rx->rx_iq_phase;
  job->start_irr = fabs(rx->image_rejection_db);
  g_thread_unref(g_thread_new("rx_iq_auto", rx_iq_auto_thread, job));
}

static void filter_type_cb(GtkToggleButton *widget, gpointer data) {
  int type = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  int channel  = GPOINTER_TO_INT(data);
  switch (channel) {
  case 0:
  case 1:
    receiver[channel]->low_latency = type;
    rx_set_fft_latency(receiver[channel]);
    break;
  case 8:
    if (can_transmit) {
      transmitter->low_latency = type;
      tx_set_latency(transmitter);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
    }
    break;
  }
  //t_print("WDSP filter type channel=%d changed to %d\n", channel, type);
}

static void filter_size_cb(GtkWidget *widget, gpointer data) {
  int channel = GPOINTER_TO_INT(data);
  const char *p = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
  int size;
  // Get size from string in the combobox
  if (sscanf(p, "%d", &size) != 1) { return; }
  switch (channel) {
  case 0:
  case 1:
    receiver[channel]->fft_size = size;
    rx_set_fft_size(receiver[channel]);
    break;
  case 8:
    if (can_transmit) {
      transmitter->fft_size = size;
      tx_set_fft_size(transmitter);
    }
    break;
  }
  //t_print("WDSP filter size channel=%d changed to %d\n", channel, size);
}

void fft_menu(GtkWidget *parent) {
  GtkWidget *w;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[32];
  snprintf(_title, 32, "%s - DSP", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  g_signal_connect(dialog, "delete_event", G_CALLBACK(close_cb), NULL);
  g_signal_connect(dialog, "destroy", G_CALLBACK(close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  w = gtk_button_new_with_label("Close");
  g_signal_connect(w, "button_press_event", G_CALLBACK(close_cb), NULL);
  gtk_widget_set_name(w, "close_button");
  gtk_grid_attach(GTK_GRID(grid), w, 0, 0, 1, 1);
  w = gtk_label_new("Filter Type");
  gtk_widget_set_name(w, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), w, 0, 2, 1, 1);
  w = gtk_label_new("Filter Size");
  gtk_widget_set_name(w, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), w, 0, 3, 1, 1);
  w = gtk_label_new("Binaural");
  gtk_widget_set_name(w, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), w, 0, 4, 1, 1);
  w = gtk_label_new("RX Image Measure");
  gtk_widget_set_name(w, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), w, 0, 5, 1, 1);
  w = gtk_label_new("Image Offset Hz");
  gtk_widget_set_name(w, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), w, 0, 6, 1, 1);
  w = gtk_label_new("RX IQ Gain");
  gtk_widget_set_name(w, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), w, 0, 7, 1, 1);
  w = gtk_label_new("RX IQ Phase");
  gtk_widget_set_name(w, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), w, 0, 8, 1, 1);
  w = gtk_label_new("RX IQ Reset");
  gtk_widget_set_name(w, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), w, 0, 9, 1, 1);
  w = gtk_label_new("RX IQ Auto");
  gtk_widget_set_name(w, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), w, 0, 10, 1, 1);
  w = gtk_label_new("RX IQ Status");
  gtk_widget_set_name(w, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), w, 0, 11, 1, 1);
  int col = 1;
  for (int i = 0; i <= receivers; i++) {
    // i == receivers means "TX"
    int chan;
    int j, s, dsize, fsize, ftype;
    char text[32];
    if ((i == receivers) && !can_transmit) { break; }
    if (i == 0) {
      w = gtk_label_new("RX1");
      gtk_widget_set_name(w, "boldlabel");
      chan = 0;                               // actual channel
      fsize = receiver[0]->fft_size;          // actual size value
      dsize = receiver[0]->dsp_size;          // minimum size value
      ftype = receiver[0]->low_latency;       // 0: linear phase, 1: low latency
    } else if (i == receivers - 1) {
      w = gtk_label_new("RX2");
      gtk_widget_set_name(w, "boldlabel");
      chan = 1;
      fsize = receiver[1]->fft_size;
      dsize = receiver[1]->dsp_size;
      ftype = receiver[1]->low_latency;
    } else {
      w = gtk_label_new("TX");
      gtk_widget_set_name(w, "boldlabel");
      chan = 8;
      fsize = transmitter->fft_size;
      dsize = transmitter->dsp_size;
      ftype = transmitter->low_latency;
    }
    gtk_grid_attach(GTK_GRID(grid), w, col, 1, 1, 1);
    //
    // To enable CESSB overshoot correction with TX compression, we cannot
    // allow low latency filters for TX
    //
    w = gtk_combo_box_text_new();
    gtk_widget_set_tooltip_text(w, "TX Low Latency sets CESSB option to *DISABLED*\n\n"
                                   "These two functions cannot be used simultaneously.\n"
                                   "Setting to Linear Phase is REQUIRED when using CESSB.\n\n"
                                   "Note: RX is not affected. Selection is unrestricted.");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, "Linear Phase");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, "Low Latency");
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), ftype);
    my_combo_attach(GTK_GRID(grid), w, col, 2, 1, 1);
    g_signal_connect(w, "changed", G_CALLBACK(filter_type_cb), GINT_TO_POINTER(chan));
    //
    // The filter size must be a power of two and at least equal to the dsp size
    // Apart from that, we allow values from 1k ... 32k.
    //
    w = gtk_combo_box_text_new();
    s = 512;
    j = 0;
    for (;;) {
      s = 2 * s;
      if (s >= dsize) {
        snprintf(text, 32, "%d", s);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, text);
        if (s == fsize) { gtk_combo_box_set_active(GTK_COMBO_BOX(w), j); }
        j++;
      }
      if (s >= 32768) { break; }
    }
    my_combo_attach(GTK_GRID(grid), w, col, 3, 1, 1);
    g_signal_connect(w, "changed", G_CALLBACK(filter_size_cb), GINT_TO_POINTER(chan));
    if (i < receivers) {
      w = gtk_check_button_new();
      gtk_widget_set_tooltip_text(w, "Outputs I and Q on the Left and Right audio channels.\n\n"
                                     "If Audio Output Device is Mono,\n"
                                     "Binaural option is not available");
      if (receiver[i]->local_audio_channels == 1) {
        receiver[i]->binaural = 0;
      }
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), receiver[i]->binaural);
      gtk_widget_set_sensitive(w, receiver[i]->local_audio_channels > 1);
      gtk_grid_attach(GTK_GRID(grid), w, col, 4, 1, 1);
      g_signal_connect(w, "toggled", G_CALLBACK(binaural_cb), GINT_TO_POINTER(chan));
      w = gtk_check_button_new();
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), receiver[i]->image_measure);
      gtk_widget_set_tooltip_text(w, "Enable RX image rejection measurement in the panadapter");
      gtk_grid_attach(GTK_GRID(grid), w, col, 5, 1, 1);
      g_signal_connect(w, "toggled", G_CALLBACK(image_measure_cb), GINT_TO_POINTER(chan));
      w = gtk_spin_button_new_with_range(100.0, 10000.0, 10.0);
      gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w), 0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), receiver[i]->image_measure_hz);
      gtk_widget_set_tooltip_text(w, "RX image rejection measurement offset in Hz");
      gtk_grid_attach(GTK_GRID(grid), w, col, 6, 1, 1);
      g_signal_connect(w, "value_changed", G_CALLBACK(image_measure_hz_cb), GINT_TO_POINTER(chan));
      w = gtk_spin_button_new_with_range(-5.0, 5.0, 0.01);
      gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w), 2);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), receiver[i]->rx_iq_gain);
      gtk_widget_set_tooltip_text(w, "Manual RX IQ gain correction in dB");
      if (chan >= 0 && chan < 2) {
        rx_iq_gain_spin[chan] = w;
      }
      gtk_grid_attach(GTK_GRID(grid), w, col, 7, 1, 1);
      g_signal_connect(w, "value_changed", G_CALLBACK(rx_iq_gain_cb), GINT_TO_POINTER(chan));
      w = gtk_spin_button_new_with_range(-20.0, 20.0, 0.01);
      gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w), 2);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), receiver[i]->rx_iq_phase);
      gtk_widget_set_tooltip_text(w, "Manual RX IQ phase correction in degrees");
      if (chan >= 0 && chan < 2) {
        rx_iq_phase_spin[chan] = w;
      }
      gtk_grid_attach(GTK_GRID(grid), w, col, 8, 1, 1);
      g_signal_connect(w, "value_changed", G_CALLBACK(rx_iq_phase_cb), GINT_TO_POINTER(chan));
      w = gtk_button_new_with_label("Reset");
      gtk_widget_set_tooltip_text(w, "Reset manual RX IQ gain and phase correction to 0.00");
      gtk_grid_attach(GTK_GRID(grid), w, col, 9, 1, 1);
      g_signal_connect(w, "clicked", G_CALLBACK(rx_iq_reset_cb), GINT_TO_POINTER(chan));
      w = gtk_button_new_with_label("Auto RX IQ");
      gtk_widget_set_tooltip_text(w, "Run one-shot RX IQ calibration using the active image rejection measurement");
      if (chan >= 0 && chan < 2) {
        rx_iq_auto_button[chan] = w;
        gtk_widget_set_sensitive(w, !rx_iq_auto_running[chan]);
      }
      gtk_grid_attach(GTK_GRID(grid), w, col, 10, 1, 1);
      g_signal_connect(w, "clicked", G_CALLBACK(rx_iq_auto_cb), GINT_TO_POINTER(chan));
      w = gtk_label_new(receiver[i]->rx_iq_status);
      gtk_widget_set_halign(w, GTK_ALIGN_START);
      if (chan >= 0 && chan < 2) {
        rx_iq_status_label[chan] = w;
      }
      gtk_grid_attach(GTK_GRID(grid), w, col, 11, 1, 1);
    }
    col++;
  }
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}

