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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "new_menu.h"
#include "noise_menu.h"
#include "band.h"
#include "bandstack.h"
#include "filter.h"
#include "mode.h"
#include "radio.h"
#include "vfo.h"
#include "ext.h"
#include "message.h"
#include "wdsp.h"
#include "sliders.h"

static GtkWidget *dialog = NULL;

static GtkWidget *nr_container;
static GtkWidget *nb_container;
#ifdef EXTNR
  static GtkWidget *nr4_container;
#endif

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

void update_noise(void) {
  int id = active_receiver->id;

  //
  // Update the mode settings
  //
  if (id == 0) {
    int mode = vfo[id].mode;
    mode_settings[mode].nr = active_receiver->nr;
    mode_settings[mode].nb = active_receiver->nb;
    mode_settings[mode].anf = active_receiver->anf;
    mode_settings[mode].snb = active_receiver->snb;
    mode_settings[mode].nr2_ae = active_receiver->nr2_ae;

    if ((GetWDSPVersion() % 100) > 26) {
      mode_settings[mode].nr2_post = active_receiver->nr2_post;
      mode_settings[mode].nr2_post_taper = active_receiver->nr2_post_taper;
      mode_settings[mode].nr2_post_nlevel = active_receiver->nr2_post_nlevel;
      mode_settings[mode].nr2_post_factor = active_receiver->nr2_post_factor;
      mode_settings[mode].nr2_post_rate = active_receiver->nr2_post_rate;
    }

    mode_settings[mode].nr_agc = active_receiver->nr_agc;
    mode_settings[mode].nb2_mode = active_receiver->nb2_mode;
    mode_settings[mode].nr2_gain_method = active_receiver->nr2_gain_method;
    mode_settings[mode].nr2_npe_method = active_receiver->nr2_npe_method;
    mode_settings[mode].nr2_trained_threshold = active_receiver->nr2_trained_threshold;
    mode_settings[mode].nr2_trained_t2 = active_receiver->nr2_trained_t2;
    mode_settings[mode].nb_tau = active_receiver->nb_tau;
    mode_settings[mode].nb_advtime = active_receiver->nb_advtime;
    mode_settings[mode].nb_hang = active_receiver->nb_hang;
    mode_settings[mode].nb_thresh = active_receiver->nb_thresh;
#ifdef EXTNR
    mode_settings[mode].nr4_reduction_amount = active_receiver->nr4_reduction_amount;
    mode_settings[mode].nr4_smoothing_factor = active_receiver->nr4_smoothing_factor;
    mode_settings[mode].nr4_whitening_factor = active_receiver->nr4_whitening_factor;
    mode_settings[mode].nr4_noise_rescale = active_receiver->nr4_noise_rescale;
    mode_settings[mode].nr4_post_filter_threshold = active_receiver->nr4_post_filter_threshold;
#endif
    copy_mode_settings(mode);
  }

  rx_set_noise(active_receiver);
  g_idle_add(ext_vfo_update, NULL);
}

static void nb_cb(GtkToggleButton *widget, gpointer data) {
  active_receiver->nb = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  update_noise();
}

static void nr_cb(GtkToggleButton *widget, gpointer data) {
  active_receiver->nr = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  update_noise();
}

static void anf_cb(GtkWidget *widget, gpointer data) {
  active_receiver->anf = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  update_noise();
}

static void snb_cb(GtkWidget *widget, gpointer data) {
  active_receiver->snb = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  update_noise();
  update_slider_snb_button(TRUE);
}

static void post_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr2_post = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  update_noise();
}

static void post_nlevel_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr2_post_nlevel = (int) (gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  update_noise();
}

static void post_rate_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr2_post_rate = (int) (gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  update_noise();
}

static void post_factor_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr2_post_factor = (int) (gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  update_noise();
}

static void post_taper_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr2_post_taper = (int) (gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  update_noise();
}

static void ae_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr2_ae = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  update_noise();
}

static void pos_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr_agc = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  update_noise();
}

static void mode_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nb2_mode = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  update_noise();
}

static void gain_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr2_gain_method = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  update_noise();
}

static void npe_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr2_npe_method = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  update_noise();
}

static void trained_thr_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr2_trained_threshold = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise();
}

static void trained_t2_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr2_trained_t2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise();
}

static void slew_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nb_tau = 0.001 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise();
}

static void lead_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nb_advtime = 0.001 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise();
}

static void lag_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nb_hang = 0.001 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise();
}

static void thresh_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nb_thresh = 0.165 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise();
}

static void nr_sel_changed(GtkWidget *widget, gpointer data) {
  // show or hide all controls for NR settings
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(nr_container);
  } else {
    gtk_widget_hide(nr_container);
  }
}

static void nb_sel_changed(GtkWidget *widget, gpointer data) {
  // show or hide all controls for NB settings
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(nb_container);
  } else {
    gtk_widget_hide(nb_container);
  }
}

#ifdef EXTNR
static void nr4_sel_changed(GtkWidget *widget, gpointer data) {
  // show or hide all controls for NR4 settings
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(nr4_container);
  } else {
    gtk_widget_hide(nr4_container);
  }
}

static void nr4_reduction_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr4_reduction_amount = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise();
}

static void nr4_smoothing_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr4_smoothing_factor = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise();
}

static void nr4_whitening_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr4_whitening_factor = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise();
}

static void nr4_rescale_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr4_noise_rescale = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise();
}

static void nr4_threshold_cb(GtkWidget *widget, gpointer data) {
  active_receiver->nr4_post_filter_threshold = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise();
}

#endif

void noise_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  char title[64];
#ifdef EXTNR
  snprintf(title, 64, "%s - Noise (RX%d VFO-%s) [NR1-NR4]", PGNAME, active_receiver->id + 1,
           active_receiver->id == 0 ? "A" : "B");
#else
  snprintf(title, 64, "%s - Noise (RX%d VFO-%s) [NR1-NR2]", PGNAME, active_receiver->id + 1,
           active_receiver->id == 0 ? "A" : "B");
#endif
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  //
  // First row: SNB/ANF/NR method
  //
  //---------------------------------------------------------------------------------
  GtkWidget *b_snb = gtk_check_button_new_with_label("SNB");
  gtk_widget_set_name(b_snb, "boldlabel");
  gtk_widget_set_tooltip_text(b_snb, "Spectral Noise Blanker");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_snb), active_receiver->snb);
  gtk_widget_show(b_snb);
  gtk_grid_attach(GTK_GRID(grid), b_snb, 0, 1, 1, 1);
  g_signal_connect(b_snb, "toggled", G_CALLBACK(snb_cb), NULL);
  //---------------------------------------------------------------------------------
  GtkWidget *b_anf = gtk_check_button_new_with_label("ANF");
  gtk_widget_set_name(b_anf, "boldlabel");
  gtk_widget_set_tooltip_text(b_anf, "Auto Notch Filter");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_anf), active_receiver->anf);
  gtk_widget_show(b_anf);
  gtk_grid_attach(GTK_GRID(grid), b_anf, 1, 1, 1, 1);
  g_signal_connect(b_anf, "toggled", G_CALLBACK(anf_cb), NULL);
  //---------------------------------------------------------------------------------
  GtkWidget *nr_title = gtk_label_new("Noise Reduction");
  gtk_widget_set_name(nr_title, "boldlabel");
  gtk_widget_set_halign(nr_title, GTK_ALIGN_END);
  gtk_widget_show(nr_title);
  gtk_grid_attach(GTK_GRID(grid), nr_title, 2, 1, 1, 1);
  GtkWidget *nr_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nr_combo), NULL, "NONE");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nr_combo), NULL, "NR");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nr_combo), NULL, "NR2");
#ifdef EXTNR
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nr_combo), NULL, "NR3");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nr_combo), NULL, "NR4");
#endif
  gtk_combo_box_set_active(GTK_COMBO_BOX(nr_combo), active_receiver->nr);
  gtk_widget_set_hexpand(GTK_WIDGET(nr_combo), FALSE);
  gtk_widget_set_halign(nr_combo, GTK_ALIGN_START);
  my_combo_attach(GTK_GRID(grid), nr_combo, 3, 1, 1, 1);
  g_signal_connect(nr_combo, "changed", G_CALLBACK(nr_cb), NULL);
  //---------------------------------------------------------------------------------
  //
  // Second row: NB selection
  //
  //---------------------------------------------------------------------------------
  GtkWidget *pos_title = gtk_label_new("NR/NR2/ANF Position");
  gtk_widget_set_name(pos_title, "boldlabel");
  gtk_widget_set_halign(pos_title, GTK_ALIGN_END);
  gtk_widget_show(pos_title);
  gtk_grid_attach(GTK_GRID(grid), pos_title, 0, 2, 1, 1);
  GtkWidget *pos_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(pos_combo), NULL, "Pre AGC");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(pos_combo), NULL, "Post AGC");
  gtk_combo_box_set_active(GTK_COMBO_BOX(pos_combo), active_receiver->nr_agc);
  gtk_widget_set_hexpand(GTK_WIDGET(pos_combo), FALSE);
  gtk_widget_set_halign(pos_combo, GTK_ALIGN_START);
  my_combo_attach(GTK_GRID(grid), pos_combo, 1, 2, 1, 1);
  g_signal_connect(pos_combo, "changed", G_CALLBACK(pos_cb), NULL);
  //---------------------------------------------------------------------------------
  GtkWidget *nb_title = gtk_label_new("Noise Blanker");
  gtk_widget_set_name(nb_title, "boldlabel");
  gtk_widget_set_halign(nb_title, GTK_ALIGN_END);
  gtk_widget_show(nb_title);
  gtk_grid_attach(GTK_GRID(grid), nb_title, 2, 2, 1, 1);
  GtkWidget *nb_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nb_combo), NULL, "NONE");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nb_combo), NULL, "NB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nb_combo), NULL, "NB2");
  gtk_combo_box_set_active(GTK_COMBO_BOX(nb_combo), active_receiver->nb);
  gtk_widget_set_hexpand(GTK_WIDGET(nb_combo), FALSE);
  gtk_widget_set_halign(nb_combo, GTK_ALIGN_START);
  my_combo_attach(GTK_GRID(grid), nb_combo, 3, 2, 1, 1);
  g_signal_connect(nb_combo, "changed", G_CALLBACK(nb_cb), NULL);
  GtkWidget *line = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(line, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), line, 0, 3, 4, 1);
  //---------------------------------------------------------------------------------
  //
  // Third row: select settings: NR, NB, NR4 settings
  //
  GtkWidget *nr_sel = gtk_radio_button_new_with_label_from_widget(NULL, "NR Settings");
  gtk_widget_set_name(nr_sel, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nr_sel), 1);
  gtk_widget_show(nr_sel);
  gtk_grid_attach(GTK_GRID(grid), nr_sel, 0, 4, 1, 1);
  g_signal_connect(nr_sel, "toggled", G_CALLBACK(nr_sel_changed), NULL);
  //
  GtkWidget *nb_sel = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(nr_sel), "NB Settings");
  gtk_widget_set_name(nb_sel, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nb_sel), 0);
  gtk_widget_show(nb_sel);
  gtk_grid_attach(GTK_GRID(grid), nb_sel, 1, 4, 1, 1);
  g_signal_connect(nb_sel, "toggled", G_CALLBACK(nb_sel_changed), NULL);
#ifdef EXTNR
  GtkWidget *nr4_sel = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(nr_sel), "NR4 Settings");
  gtk_widget_set_name(nr4_sel, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nr4_sel), 0);
  gtk_widget_show(nr4_sel);
  gtk_grid_attach(GTK_GRID(grid), nr4_sel, 2, 4, 1, 1);
  g_signal_connect(nr4_sel, "toggled", G_CALLBACK(nr4_sel_changed), NULL);
#endif
  //
  // Hiding/Showing ComboBoxes optimized for Touch-Screens does not
  // work. Therefore, we have to group the NR, NB, and NR4 controls
  // in a container, which then can be shown/hidden
  //
  //
  // NR controls
  //
  nr_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), nr_container, 0, 5, 4, 3);
  GtkWidget *nr_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(nr_grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(nr_grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(nr_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(nr_grid), 5);
  //
  GtkWidget *gain_title = gtk_label_new("NR2 Gain Method");
  gtk_widget_set_name(gain_title, "boldlabel");
  gtk_widget_set_halign(gain_title, GTK_ALIGN_END);
  gtk_widget_show(gain_title);
  gtk_grid_attach(GTK_GRID(nr_grid), gain_title, 0, 0, 1, 1);
  //
  GtkWidget *gain_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gain_combo), NULL, "Linear");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gain_combo), NULL, "Log");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gain_combo), NULL, "Gamma");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gain_combo), NULL, "Trained");
  gtk_combo_box_set_active(GTK_COMBO_BOX(gain_combo), active_receiver->nr2_gain_method);
  gtk_widget_set_hexpand(GTK_WIDGET(gain_combo), FALSE);
  gtk_widget_set_halign(gain_combo, GTK_ALIGN_START);
  my_combo_attach(GTK_GRID(nr_grid), gain_combo, 1, 0, 1, 1);
  g_signal_connect(gain_combo, "changed", G_CALLBACK(gain_cb), NULL);
  //
  GtkWidget *npe_title = gtk_label_new("NR2 NPE Method");
  gtk_widget_set_name(npe_title, "boldlabel");
  gtk_widget_set_halign(npe_title, GTK_ALIGN_END);
  gtk_widget_show(npe_title);
  gtk_grid_attach(GTK_GRID(nr_grid), npe_title, 2, 0, 1, 1);
  GtkWidget *npe_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(npe_combo), NULL, "OSMS");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(npe_combo), NULL, "MMSE");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(npe_combo), NULL, "NSTAT");
  gtk_combo_box_set_active(GTK_COMBO_BOX(npe_combo), active_receiver->nr2_npe_method);
  gtk_widget_set_hexpand(GTK_WIDGET(npe_combo), FALSE);
  gtk_widget_set_halign(npe_combo, GTK_ALIGN_START);
  my_combo_attach(GTK_GRID(nr_grid), npe_combo, 3, 0, 1, 1);
  g_signal_connect(npe_combo, "changed", G_CALLBACK(npe_cb), NULL);
  //
  GtkWidget *box_ae = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *l_ae = gtk_label_new("NR2 Artifact Elimination");
  gtk_widget_set_name(l_ae, "boldlabel");
  GtkWidget *b_ae = gtk_check_button_new();
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_ae), active_receiver->nr2_ae);
  g_signal_connect(b_ae, "toggled", G_CALLBACK(ae_cb), NULL);
  gtk_box_pack_start(GTK_BOX(box_ae), l_ae, FALSE, FALSE, 5);
  gtk_box_pack_start(GTK_BOX(box_ae), b_ae, FALSE, FALSE, 5);
  gtk_widget_show_all(box_ae);
  gtk_grid_attach(GTK_GRID(nr_grid), box_ae, 2, 2, 2, 1);

  //
  if ((GetWDSPVersion() % 100) > 26) {
    GtkWidget *box_nr2_post = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *l_nr2_post = gtk_label_new("NR2 Post-Processing");
    gtk_widget_set_name(l_nr2_post, "boldlabel");
    GtkWidget *b_nr2_post = gtk_check_button_new();
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_nr2_post), active_receiver->nr2_post);
    g_signal_connect(b_nr2_post, "toggled", G_CALLBACK(post_cb), NULL);
    gtk_box_pack_start(GTK_BOX(box_nr2_post), l_nr2_post, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(box_nr2_post), b_nr2_post, FALSE, FALSE, 5);
    gtk_widget_show_all(box_nr2_post);
    gtk_grid_attach(GTK_GRID(nr_grid), box_nr2_post, 0, 2, 2, 1);
    //
    GtkWidget *l_nr2_post_nlevel = gtk_label_new("NR2 Post Level");
    gtk_widget_set_name(l_nr2_post_nlevel, "boldlabel");
    gtk_widget_set_halign(l_nr2_post_nlevel, GTK_ALIGN_END);
    gtk_widget_show(l_nr2_post_nlevel);
    gtk_grid_attach(GTK_GRID(nr_grid), l_nr2_post_nlevel, 0, 3, 1, 1);
    GtkWidget *b_nr2_post_nlevel = gtk_spin_button_new_with_range(0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(b_nr2_post_nlevel), active_receiver->nr2_post_nlevel);
    gtk_widget_set_hexpand(GTK_WIDGET(b_nr2_post_nlevel), FALSE);
    gtk_widget_set_halign(b_nr2_post_nlevel, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(nr_grid), b_nr2_post_nlevel, 1, 3, 1, 1);
    g_signal_connect(b_nr2_post_nlevel, "changed", G_CALLBACK(post_nlevel_cb), NULL);
    //
    GtkWidget *l_nr2_post_rate = gtk_label_new("NR2 Post Rate");
    gtk_widget_set_name(l_nr2_post_rate, "boldlabel");
    gtk_widget_set_halign(l_nr2_post_rate, GTK_ALIGN_END);
    gtk_widget_show(l_nr2_post_rate);
    gtk_grid_attach(GTK_GRID(nr_grid), l_nr2_post_rate, 0, 4, 1, 1);
    GtkWidget *b_nr2_post_rate = gtk_spin_button_new_with_range(0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(b_nr2_post_rate), active_receiver->nr2_post_rate);
    gtk_widget_set_hexpand(GTK_WIDGET(b_nr2_post_rate), FALSE);
    gtk_widget_set_halign(b_nr2_post_rate, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(nr_grid), b_nr2_post_rate, 1, 4, 1, 1);
    g_signal_connect(b_nr2_post_rate, "changed", G_CALLBACK(post_rate_cb), NULL);
    //
    GtkWidget *l_nr2_post_factor = gtk_label_new("NR2 Post Factor");
    gtk_widget_set_name(l_nr2_post_factor, "boldlabel");
    gtk_widget_set_halign(l_nr2_post_factor, GTK_ALIGN_END);
    gtk_widget_show(l_nr2_post_factor);
    gtk_grid_attach(GTK_GRID(nr_grid), l_nr2_post_factor, 2, 3, 1, 1);
    GtkWidget *b_nr2_post_factor = gtk_spin_button_new_with_range(0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(b_nr2_post_factor), active_receiver->nr2_post_factor);
    gtk_widget_set_hexpand(GTK_WIDGET(b_nr2_post_factor), FALSE);
    gtk_widget_set_halign(b_nr2_post_factor, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(nr_grid), b_nr2_post_factor, 3, 3, 1, 1);
    g_signal_connect(b_nr2_post_factor, "changed", G_CALLBACK(post_factor_cb), NULL);
    //
    GtkWidget *l_nr2_post_taper = gtk_label_new("NR2 Post Taper");
    gtk_widget_set_name(l_nr2_post_taper, "boldlabel");
    gtk_widget_set_halign(l_nr2_post_taper, GTK_ALIGN_END);
    gtk_widget_show(l_nr2_post_taper);
    gtk_grid_attach(GTK_GRID(nr_grid), l_nr2_post_taper, 2, 4, 1, 1);
    GtkWidget *b_nr2_post_taper = gtk_spin_button_new_with_range(0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(b_nr2_post_taper), active_receiver->nr2_post_taper);
    gtk_widget_set_hexpand(GTK_WIDGET(b_nr2_post_taper), FALSE);
    gtk_widget_set_halign(b_nr2_post_taper, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(nr_grid), b_nr2_post_taper, 3, 4, 1, 1);
    g_signal_connect(b_nr2_post_taper, "changed", G_CALLBACK(post_taper_cb), NULL);
  }

  //
  GtkWidget *trained_thr_title = gtk_label_new("NR2 Trained Thresh");
  gtk_widget_set_name(trained_thr_title, "boldlabel");
  gtk_widget_set_halign(trained_thr_title, GTK_ALIGN_END);
  gtk_widget_show(trained_thr_title);
  gtk_grid_attach(GTK_GRID(nr_grid), trained_thr_title, 0, 1, 1, 1);
  GtkWidget *trained_thr_b = gtk_spin_button_new_with_range(-5.0, 5.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(trained_thr_b), active_receiver->nr2_trained_threshold);
  gtk_widget_set_hexpand(GTK_WIDGET(trained_thr_b), FALSE);
  gtk_widget_set_halign(trained_thr_b, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(nr_grid), trained_thr_b, 1, 1, 1, 1);
  g_signal_connect(trained_thr_b, "changed", G_CALLBACK(trained_thr_cb), NULL);
  //
  GtkWidget *trained_t2_title = gtk_label_new("NR2 Trained T2");
  gtk_widget_set_name(trained_t2_title, "boldlabel");
  gtk_widget_set_halign(trained_t2_title, GTK_ALIGN_END);
  gtk_widget_show(trained_t2_title);
  gtk_grid_attach(GTK_GRID(nr_grid), trained_t2_title, 2, 1, 1, 1);
  GtkWidget *trained_t2_b = gtk_spin_button_new_with_range(0.02, 0.3, 0.01);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(trained_t2_b), active_receiver->nr2_trained_t2);
  gtk_widget_set_hexpand(GTK_WIDGET(trained_t2_b), FALSE);
  gtk_widget_set_halign(trained_t2_b, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(nr_grid), trained_t2_b, 3, 1, 1, 1);
  g_signal_connect(trained_t2_b, "changed", G_CALLBACK(trained_t2_cb), NULL);
  //
  gtk_container_add(GTK_CONTAINER(nr_container), nr_grid);
  //
  // NB controls starting on row 4
  //
  nb_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), nb_container, 0, 5, 4, 3);
  GtkWidget *nb_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(nb_grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(nb_grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(nb_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(nb_grid), 5);
  //
  GtkWidget *mode_title = gtk_label_new("NB2 mode");
  gtk_widget_set_name(mode_title, "boldlabel");
  gtk_widget_set_halign(mode_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), mode_title, 0, 0, 1, 1);
  GtkWidget *mode_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Zero");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Sample&Hold");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Mean Hold");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Hold Sample");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Interpolate");
  gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), active_receiver->nb2_mode);
  my_combo_attach(GTK_GRID(nb_grid), mode_combo, 1, 0, 1, 1);
  g_signal_connect(mode_combo, "changed", G_CALLBACK(mode_cb), NULL);
  //
  GtkWidget *slew_title = gtk_label_new("NB Slew time (ms)");
  gtk_widget_set_name(slew_title, "boldlabel");
  gtk_widget_set_halign(slew_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), slew_title, 0, 1, 1, 1);
  GtkWidget *slew_b = gtk_spin_button_new_with_range(0.0, 0.1, 0.001);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(slew_b), active_receiver->nb_tau * 1000.0);
  gtk_grid_attach(GTK_GRID(nb_grid), slew_b, 1, 1, 1, 1);
  g_signal_connect(slew_b, "changed", G_CALLBACK(slew_cb), NULL);
  //
  GtkWidget *lead_title = gtk_label_new("NB Lead time (ms)");
  gtk_widget_set_name(lead_title, "boldlabel");
  gtk_widget_set_halign(lead_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), lead_title, 2, 1, 1, 1);
  GtkWidget *lead_b = gtk_spin_button_new_with_range(0.0, 0.1, 0.001);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(lead_b), active_receiver->nb_advtime * 1000.0);
  gtk_grid_attach(GTK_GRID(nb_grid), lead_b, 3, 1, 1, 1);
  g_signal_connect(lead_b, "changed", G_CALLBACK(lead_cb), NULL);
  //
  GtkWidget *lag_title = gtk_label_new("NB Lag time (ms)");
  gtk_widget_set_name(lag_title, "boldlabel");
  gtk_widget_set_halign(lag_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), lag_title, 0, 2, 1, 1);
  GtkWidget *lag_b = gtk_spin_button_new_with_range(0.0, 0.1, 0.001);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(lag_b), active_receiver->nb_hang * 1000.0);
  gtk_grid_attach(GTK_GRID(nb_grid), lag_b, 1, 2, 1, 1);
  g_signal_connect(lag_b, "changed", G_CALLBACK(lag_cb), NULL);
  //
  GtkWidget *thresh_title = gtk_label_new("NB Threshold");
  gtk_widget_set_name(thresh_title, "boldlabel");
  gtk_widget_set_halign(thresh_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), thresh_title, 2, 2, 1, 1);
  GtkWidget *thresh_b = gtk_spin_button_new_with_range(15.0, 500.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(thresh_b), active_receiver->nb_thresh * 6.0606060606); // 1.0/0.165
  gtk_grid_attach(GTK_GRID(nb_grid), thresh_b, 3, 2, 1, 1);
  g_signal_connect(thresh_b, "changed", G_CALLBACK(thresh_cb), NULL);
  gtk_container_add(GTK_CONTAINER(nb_container), nb_grid);
#ifdef EXTNR
  //
  // NR4 controls starting at row 4
  //
  nr4_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), nr4_container, 0, 5, 4, 3);
  GtkWidget *nr4_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(nr4_grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(nr4_grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(nr4_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(nr4_grid), 5);
  //
  GtkWidget *nr4_reduction_title = gtk_label_new("NR4 Reduction (dB)");
  gtk_widget_set_name(nr4_reduction_title, "boldlabel");
  gtk_widget_set_halign(nr4_reduction_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_reduction_title, 0, 0, 1, 1);
  GtkWidget *nr4_reduction_b = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(nr4_reduction_b), active_receiver->nr4_reduction_amount);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_reduction_b, 1, 0, 1, 1);
  g_signal_connect(G_OBJECT(nr4_reduction_b), "changed", G_CALLBACK(nr4_reduction_cb), NULL);
  //
  GtkWidget *nr4_smoothing_title = gtk_label_new("NR4 Smoothing (%)");
  gtk_widget_set_name(nr4_smoothing_title, "boldlabel");
  gtk_widget_set_halign(nr4_smoothing_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_smoothing_title, 2, 0, 1, 1);
  GtkWidget *nr4_smoothing_b = gtk_spin_button_new_with_range(0.0, 100.0, 1.0);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(nr4_smoothing_b), active_receiver->nr4_smoothing_factor);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_smoothing_b, 3, 0, 1, 1);
  g_signal_connect(G_OBJECT(nr4_smoothing_b), "changed", G_CALLBACK(nr4_smoothing_cb), NULL);
  //
  GtkWidget *nr4_whitening_title = gtk_label_new("NR4 Whitening (%)");
  gtk_widget_set_name(nr4_whitening_title, "boldlabel");
  gtk_widget_set_halign(nr4_whitening_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_whitening_title, 0, 1, 1, 1);
  GtkWidget *nr4_whitening_b = gtk_spin_button_new_with_range(0.0, 100.0, 1.0);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(nr4_whitening_b), active_receiver->nr4_whitening_factor);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_whitening_b, 1, 1, 1, 1);
  g_signal_connect(G_OBJECT(nr4_whitening_b), "changed", G_CALLBACK(nr4_whitening_cb), NULL);
  //
  GtkWidget *nr4_rescale_title = gtk_label_new("NR4 rescale (dB)");
  gtk_widget_set_name(nr4_rescale_title, "boldlabel");
  gtk_widget_set_halign(nr4_rescale_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_rescale_title, 2, 1, 1, 1);
  GtkWidget *nr4_rescale_b = gtk_spin_button_new_with_range(0.0, 12.0, 0.1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(nr4_rescale_b), active_receiver->nr4_noise_rescale);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_rescale_b, 3, 1, 1, 1);
  g_signal_connect(G_OBJECT(nr4_rescale_b), "changed", G_CALLBACK(nr4_rescale_cb), NULL);
  //
  GtkWidget *nr4_threshold_title = gtk_label_new("NR4 post filter threshold (dB)");
  gtk_widget_set_name(nr4_threshold_title, "boldlabel");
  gtk_widget_set_halign(nr4_threshold_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_threshold_title, 1, 2, 2, 1);
  GtkWidget *nr4_threshold_b = gtk_spin_button_new_with_range(-10.0, 10.0, 0.1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(nr4_threshold_b), active_receiver->nr4_post_filter_threshold);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_threshold_b, 3, 2, 1, 1);
  g_signal_connect(G_OBJECT(nr4_threshold_b), "changed", G_CALLBACK(nr4_threshold_cb), NULL);
  gtk_container_add(GTK_CONTAINER(nr4_container), nr4_grid);
#endif
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  //
  // The width of the main grid is the largest, since it contains all containers.
  // Determine this width and set the width of all containers to that value.
  // This ensures that the column widths of he main grid and the containers
  // line up so the whole menu looks well aligned.
  //
  int width = gtk_widget_get_allocated_width(grid);
  gtk_widget_set_size_request(nr_grid, width, -1);
  gtk_widget_set_size_request(nb_grid, width, -1);
#ifdef EXTNR
  gtk_widget_set_size_request(nr4_grid, width, -1);
#endif
  gtk_widget_hide(nb_container);
#ifdef EXTNR
  gtk_widget_hide(nr4_container);
#endif
  // Start Anpassung das NR4 angezeigt wird, wenn aktiv
#ifdef EXTNR

  if (active_receiver->nr == 4) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nr4_sel), TRUE);
    gtk_widget_show(nr4_container);
    gtk_widget_hide(nr_container);
    gtk_widget_hide(nb_container);
  } else
#endif
    if (active_receiver->nb > 0) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nb_sel), TRUE);
      gtk_widget_show(nb_container);
      gtk_widget_hide(nr_container);
#ifdef EXTNR
      gtk_widget_hide(nr4_container);
#endif
    } else {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nr_sel), TRUE);
      gtk_widget_show(nr_container);
#ifdef EXTNR
      gtk_widget_hide(nr4_container);
#endif
      gtk_widget_hide(nb_container);
    }

  // Ende Anpassung das NR4 angezeigt wird, wenn aktiv
}
