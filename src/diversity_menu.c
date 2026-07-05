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
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "new_menu.h"
#include "diversity_menu.h"
#include "radio.h"
#include "receiver.h"
#include "new_protocol.h"
#include "old_protocol.h"
#include "sliders.h"
#include "ext.h"
#include "message.h"
#include "main.h"

#include <math.h>

static GtkWidget *dialog = NULL;
static GtkWidget *gain_coarse_scale = NULL;
static GtkWidget *gain_fine_scale = NULL;
static GtkWidget *phase_fine_scale = NULL;
static GtkWidget *phase_coarse_scale = NULL;
static GtkWidget *diversity_enable_button = NULL;

static double gain_coarse, gain_fine;

static gboolean diversity_is_angelia_p2(void) {
  return protocol == NEW_PROTOCOL && device == NEW_DEVICE_ANGELIA;
}

static const char *diversity_unavailable_reason(void) {
  if (receiver[0] == NULL || n_adc < 2) {
    return "Diversity requires at least two ADCs.";
  }
  return NULL;
}

static gboolean diversity_available(void) {
  return diversity_unavailable_reason() == NULL;
}
static double phase_coarse, phase_fine;

static void diversity_set_tooltip(GtkWidget *label, GtkWidget *widget, const char *text) {
  if (label != NULL) {
    gtk_widget_set_tooltip_text(label, text);
  }
  if (widget != NULL) {
    gtk_widget_set_tooltip_text(widget, text);
  }
}

static void cleanup(void) {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gain_coarse_scale = NULL;
    gain_fine_scale = NULL;
    phase_coarse_scale = NULL;
    phase_fine_scale = NULL;
    diversity_enable_button = NULL;
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


static void diversity_update_enable_button(void) {
  if (diversity_enable_button == NULL) {
    return;
  }
  const char *reason = diversity_unavailable_reason();
  gtk_widget_set_sensitive(diversity_enable_button, reason == NULL);
  if (reason != NULL) {
    gtk_widget_set_tooltip_text(diversity_enable_button, reason);
  } else {
    gtk_widget_set_tooltip_text(diversity_enable_button,
                                "Enable receive diversity. RX1 becomes the combined ADC0/ADC1 diversity signal; RX2 is used only as the ADC1 monitor path. Requires at least two ADCs.");
  }
}

static void brick3_cb(GtkWidget *widget, gpointer data) {
  (void)data;
  diversity_brick3_mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ? 1 : 0;
  if (diversity_enabled) {
    schedule_high_priority();
    schedule_receive_specific();
  }
  diversity_update_enable_button();
}

static void diversity_cb(GtkWidget *widget, gpointer data) {
  int state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  set_diversity(state);
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) != diversity_enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), diversity_enabled);
  }
}

//
// the magic constant 0.017... is Pi/180
// The DIVERSITY rotation parameters must be re-calculated
// each time the gain or the phase changes.
//
static void set_gain_phase(void) {
  double amplitude, arg;
  amplitude = pow(10.0, 0.05 * div_gain);
  arg = div_phase * 0.017453292519943295769236907684886;
  div_cos = amplitude * cos(arg);
  div_sin = amplitude * sin(arg);
}

static void gain_coarse_changed_cb(GtkWidget *widget, gpointer data) {
  gain_coarse = gtk_range_get_value(GTK_RANGE(widget));
  div_gain = gain_coarse + gain_fine;
  set_gain_phase();
}

static void gain_fine_changed_cb(GtkWidget *widget, gpointer data) {
  gain_fine = gtk_range_get_value(GTK_RANGE(widget));
  div_gain = gain_coarse + gain_fine;
  set_gain_phase();
}

static void phase_coarse_changed_cb(GtkWidget *widget, gpointer data) {
  phase_coarse = gtk_range_get_value(GTK_RANGE(widget));
  div_phase = phase_coarse + phase_fine;
  set_gain_phase();
}

static void phase_fine_changed_cb(GtkWidget *widget, gpointer data) {
  phase_fine = gtk_range_get_value(GTK_RANGE(widget));
  div_phase = phase_coarse + phase_fine;
  set_gain_phase();
}

void set_diversity_gain(double val) {
  if (val < -27.0) { val = -27.0; }
  if (val >  27.0) { val =  27.0; }
  div_gain = val;
  //
  // calculate coarse and fine value.
  // if gain is 27, we can only use coarse=25 and fine=2,
  // but normally we want to keep "fine" small
  //
  gain_coarse = 2.0 * round(0.5 * div_gain);
  if (div_gain >  25.0) { gain_coarse = 25.0; }
  if (div_gain < -25.0) { gain_coarse = -25.0; }
  gain_fine = div_gain - gain_coarse;
  if (gain_coarse_scale != NULL && gain_fine_scale != NULL) {
    gtk_range_set_value(GTK_RANGE(gain_coarse_scale), gain_coarse);
    gtk_range_set_value(GTK_RANGE(gain_fine_scale), gain_fine);
  } else {
    show_diversity_gain();
  }
  set_gain_phase();
}

void set_diversity_phase(double value) {
  while (value >  180.0) { value -= 360.0; }
  while (value < -180.0) { value += 360.0; }
  div_phase = value;
  //
  // calculate coarse and fine
  //
  phase_coarse = 4.0 * round(div_phase * 0.25);
  phase_fine = div_phase - phase_coarse;
  if (phase_coarse_scale != NULL && phase_fine_scale != NULL) {
    gtk_range_set_value(GTK_RANGE(phase_coarse_scale), phase_coarse);
    gtk_range_set_value(GTK_RANGE(phase_fine_scale), phase_fine);
  } else {
    show_diversity_phase();
  }
  set_gain_phase();
}

void set_diversity(int state) {
  state = state ? 1 : 0;
  if (state && !diversity_available()) {
    const char *reason = diversity_unavailable_reason();
    t_print("%s: %s\n", __func__, reason != NULL ? reason : "diversity is not available");
    state = 0;
  }
  if (state && receivers > 1 && receiver[0] != NULL && receiver[1] != NULL &&
      receiver[1]->sample_rate != receiver[0]->sample_rate) {
    t_print("%s: syncing RX2 sample rate from %d to %d for diversity\n",
            __func__, receiver[1]->sample_rate, receiver[0]->sample_rate);
    rx_change_sample_rate(receiver[1], receiver[0]->sample_rate);
  }
  //
  // If we have only one receiver, then changing diversity
  // changes the number of HPSR receivers so we restart the
  // original protocol
  //
  int restart_old_protocol = protocol == ORIGINAL_PROTOCOL && receivers == 1 && diversity_enabled != state;
  if (restart_old_protocol) {
    old_protocol_stop();
  }
  diversity_enabled = state;
  if (receivers > 1 && receiver[1] != NULL) {
    rx_vfo_changed(receiver[1]);
  }
  if (restart_old_protocol) {
    old_protocol_run();
  }
  schedule_high_priority();
  schedule_receive_specific();
  g_idle_add(ext_vfo_update, NULL);
}

void diversity_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[32];
  snprintf(_title, 32, "%s - Diversity", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  g_signal_connect(dialog, "delete_event", G_CALLBACK(close_cb), NULL);
  g_signal_connect(dialog, "destroy", G_CALLBACK(close_cb), NULL);
  //
  // set coarse/fine values from "sanitized" actual values
  //
  if (div_gain >  27.0) { div_gain = 27.0; }
  if (div_gain < -27.0) { div_gain = -27.0; }
  while (div_phase >  180.0) { div_phase -= 360.0; }
  while (div_phase < -180.0) { div_phase += 360.0; }
  gain_coarse = 2.0 * round(0.5 * div_gain);
  if (div_gain >  25.0) { gain_coarse = 25.0; }
  if (div_gain < -25.0) { gain_coarse = -25.0; }
  gain_fine = div_gain - gain_coarse;
  phase_coarse = 4.0 * round(div_phase * 0.25);
  phase_fine = div_phase - phase_coarse;
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect(close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  GtkWidget *diversity_b = gtk_check_button_new_with_label("Diversity Enable");
  diversity_enable_button = diversity_b;
  gtk_widget_set_name(diversity_b, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(diversity_b), diversity_enabled);
  diversity_update_enable_button();
  gtk_widget_show(diversity_b);
  gtk_grid_attach(GTK_GRID(grid), diversity_b, 1, 0, 1, 1);
  g_signal_connect(diversity_b, "toggled", G_CALLBACK(diversity_cb), NULL);
  GtkWidget *brick3_b = gtk_check_button_new_with_label("Brick3");
  gtk_widget_set_name(brick3_b, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(brick3_b), diversity_brick3_mode != 0);
  gtk_widget_set_sensitive(brick3_b, diversity_is_angelia_p2());
  gtk_widget_set_tooltip_text(brick3_b,
                              "Enable the Brick3/ANAN-100D diversity routing workaround. This does not enable or disable generic Angelia Diversity; it only keeps the normal RX2/DDC context valid for the Brick3 STM32 while DDC0/DDC1 are used for Diversity.");
  gtk_widget_show(brick3_b);
  gtk_grid_attach(GTK_GRID(grid), brick3_b, 1, 1, 1, 1);
  g_signal_connect(brick3_b, "toggled", G_CALLBACK(brick3_cb), NULL);
  GtkWidget *gain_coarse_label = gtk_label_new("Gain (dB, coarse):");
  gtk_widget_set_name(gain_coarse_label, "boldlabel");
  gtk_widget_set_halign(gain_coarse_label, GTK_ALIGN_END);
  gtk_misc_set_alignment(GTK_MISC(gain_coarse_label), 0, 0);
  gtk_widget_show(gain_coarse_label);
  gtk_grid_attach(GTK_GRID(grid), gain_coarse_label, 0, 2, 1, 1);
  gain_coarse_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -25.0, +25.0, 0.5);
  gtk_widget_set_size_request(gain_coarse_scale, 300, 25);
  gtk_range_set_value(GTK_RANGE(gain_coarse_scale), gain_coarse);
  diversity_set_tooltip(gain_coarse_label, gain_coarse_scale,
                        "Coarse gain correction for the ADC1 diversity path before it is combined with ADC0. Use this for large amplitude differences between the two antennas/receivers.");
  gtk_widget_show(gain_coarse_scale);
  gtk_grid_attach(GTK_GRID(grid), gain_coarse_scale, 1, 2, 1, 1);
  g_signal_connect(G_OBJECT(gain_coarse_scale), "value_changed", G_CALLBACK(gain_coarse_changed_cb), NULL);
  GtkWidget *gain_fine_label = gtk_label_new("Gain (dB, fine):");
  gtk_widget_set_name(gain_fine_label, "boldlabel");
  gtk_widget_set_halign(gain_fine_label, GTK_ALIGN_END);
  gtk_misc_set_alignment(GTK_MISC(gain_fine_label), 0, 0);
  gtk_widget_show(gain_fine_label);
  gtk_grid_attach(GTK_GRID(grid), gain_fine_label, 0, 3, 1, 1);
  gain_fine_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -2.0, +2.0, 0.05);
  gtk_widget_set_size_request(gain_fine_scale, 300, 25);
  gtk_range_set_value(GTK_RANGE(gain_fine_scale), gain_fine);
  diversity_set_tooltip(gain_fine_label, gain_fine_scale,
                        "Fine gain correction for the ADC1 diversity path. Use this after the coarse gain is close, to maximize cancellation or combining depth.");
  gtk_widget_show(gain_fine_scale);
  gtk_grid_attach(GTK_GRID(grid), gain_fine_scale, 1, 3, 1, 1);
  g_signal_connect(G_OBJECT(gain_fine_scale), "value_changed", G_CALLBACK(gain_fine_changed_cb), NULL);
  GtkWidget *phase_coarse_label = gtk_label_new("Phase (coarse):");
  gtk_widget_set_name(phase_coarse_label, "boldlabel");
  gtk_widget_set_halign(phase_coarse_label, GTK_ALIGN_END);
  gtk_misc_set_alignment(GTK_MISC(phase_coarse_label), 0, 0);
  gtk_widget_show(phase_coarse_label);
  gtk_grid_attach(GTK_GRID(grid), phase_coarse_label, 0, 4, 1, 1);
  phase_coarse_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -180.0, 180.0, 2.0);
  gtk_widget_set_size_request(phase_coarse_scale, 300, 25);
  gtk_range_set_value(GTK_RANGE(phase_coarse_scale), phase_coarse);
  diversity_set_tooltip(phase_coarse_label, phase_coarse_scale,
                        "Coarse phase rotation for the ADC1 diversity path before it is combined with ADC0. Use this for large phase alignment changes between the two antennas/receivers.");
  gtk_widget_show(phase_coarse_scale);
  gtk_grid_attach(GTK_GRID(grid), phase_coarse_scale, 1, 4, 1, 1);
  g_signal_connect(G_OBJECT(phase_coarse_scale), "value_changed", G_CALLBACK(phase_coarse_changed_cb), NULL);
  GtkWidget *phase_fine_label = gtk_label_new("Phase (fine):");
  gtk_widget_set_name(phase_fine_label, "boldlabel");
  gtk_widget_set_halign(phase_fine_label, GTK_ALIGN_END);
  gtk_misc_set_alignment(GTK_MISC(phase_fine_label), 0, 0);
  gtk_widget_show(phase_fine_label);
  gtk_grid_attach(GTK_GRID(grid), phase_fine_label, 0, 5, 1, 1);
  phase_fine_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -5.0, 5.0, 0.1);
  gtk_widget_set_size_request(phase_fine_scale, 300, 25);
  gtk_range_set_value(GTK_RANGE(phase_fine_scale), phase_fine);
  diversity_set_tooltip(phase_fine_label, phase_fine_scale,
                        "Fine phase rotation for the ADC1 diversity path. Use this for precise nulling or peak combining after coarse phase is close.");
  gtk_widget_show(phase_fine_scale);
  gtk_grid_attach(GTK_GRID(grid), phase_fine_scale, 1, 5, 1, 1);
  g_signal_connect(G_OBJECT(phase_fine_scale), "value_changed", G_CALLBACK(phase_fine_changed_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
