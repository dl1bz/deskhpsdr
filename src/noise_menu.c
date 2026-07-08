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
#include "tci.h"

static GtkWidget *dialog = NULL;

typedef struct _NOISE_MENU_PAGE {
  RECEIVER *rx;
  GtkWidget *snb_button;
  GtkWidget *nr_combo;
  GtkWidget *nr_position_combo;
  GtkWidget *nr_container;
  GtkWidget *nb_container;
  GtkWidget *nr4_container;
  GtkWidget *nr_sel;
  GtkWidget *nb_sel;
  GtkWidget *nr4_sel;
} NOISE_MENU_PAGE;

static NOISE_MENU_PAGE noise_menu_pages[2];
static GtkWidget *noise_menu_headerbar = NULL;
static GtkWidget *noise_menu_stack = NULL;
static GtkWidget *noise_menu_rx_buttons[2] = { NULL, NULL };
static gboolean noise_menu_updating_rx_buttons = FALSE;

static RECEIVER *noise_menu_get_rx(gpointer data) {
  RECEIVER *rx = (RECEIVER *) data;
  return rx != NULL ? rx : active_receiver;
}


static void cleanup(void) {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    noise_menu_headerbar = NULL;
    noise_menu_stack = NULL;
    noise_menu_rx_buttons[0] = NULL;
    noise_menu_rx_buttons[1] = NULL;
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static void nr_cb(GtkToggleButton *widget, gpointer data);
static void snb_cb(GtkWidget *widget, gpointer data);

static gboolean noise_nr_allowed(const RECEIVER *rx) {
  if (rx == NULL) {
    return FALSE;
  }
  int mode = vfo[rx->id].mode;
  return (mode != modeDIGL && mode != modeDIGU);
}

static NOISE_MENU_PAGE *noise_menu_page_for_rx(const RECEIVER *rx) {
  if (rx == NULL || rx->id < 0 || rx->id >= 2) {
    return NULL;
  }
  if (noise_menu_pages[rx->id].rx != rx) {
    return NULL;
  }
  return &noise_menu_pages[rx->id];
}

static void noise_menu_sync_page(NOISE_MENU_PAGE *page) {
  if (page == NULL || page->rx == NULL) {
    return;
  }
  RECEIVER *rx = page->rx;
  gboolean nr_allowed = noise_nr_allowed(rx);
  if (!nr_allowed) {
    rx->nr = 0;
    rx->snb = 0;
  }
  if (page->nr_combo != NULL) {
    g_signal_handlers_block_by_func(page->nr_combo, G_CALLBACK(nr_cb), rx);
    gtk_combo_box_set_active(GTK_COMBO_BOX(page->nr_combo), rx->nr);
    g_signal_handlers_unblock_by_func(page->nr_combo, G_CALLBACK(nr_cb), rx);
    gtk_widget_set_sensitive(page->nr_combo, nr_allowed);
  }
  if (page->snb_button != NULL) {
    g_signal_handlers_block_by_func(page->snb_button, G_CALLBACK(snb_cb), rx);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->snb_button), rx->snb);
    g_signal_handlers_unblock_by_func(page->snb_button, G_CALLBACK(snb_cb), rx);
    gtk_widget_set_sensitive(page->snb_button, nr_allowed);
  }
  if (page->nr_position_combo != NULL) {
    gtk_widget_set_sensitive(page->nr_position_combo, nr_allowed);
  }
  if (page->nr_sel != NULL) {
    gtk_widget_set_sensitive(page->nr_sel, nr_allowed);
  }
  if (page->nr4_sel != NULL) {
    gtk_widget_set_sensitive(page->nr4_sel, nr_allowed);
  }
  if (page->nr_container != NULL) {
    gtk_widget_set_sensitive(page->nr_container, nr_allowed);
  }
  if (page->nr4_container != NULL) {
    gtk_widget_set_sensitive(page->nr4_container, nr_allowed);
  }
  if (!nr_allowed && page->nb_sel != NULL) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->nb_sel), TRUE);
  }
}

void update_noise_menu(void) {
  if (dialog == NULL) {
    return;
  }
  noise_menu_sync_page(&noise_menu_pages[0]);
  if (receivers > 1) {
    noise_menu_sync_page(&noise_menu_pages[1]);
  }
}

static void update_noise_for_rx(RECEIVER *rx) {
  if (rx == NULL) {
    return;
  }
  int id = rx->id;
  int mode = vfo[id].mode;
  if (!noise_nr_allowed(rx)) {
    rx->nr = 0;
    rx->snb = 0;
  }
  //
  // Update the mode settings
  //
  if (id == 0) {
    mode_settings[mode].nr = rx->nr;
    mode_settings[mode].nb = rx->nb;
    mode_settings[mode].anf = rx->anf;
    mode_settings[mode].snb = rx->snb;
    mode_settings[mode].nr2_ae = rx->nr2_ae;
    if ((GetWDSPVersion() % 100) > 26) {
      mode_settings[mode].nr2_post = rx->nr2_post;
      mode_settings[mode].nr2_post_taper = rx->nr2_post_taper;
      mode_settings[mode].nr2_post_nlevel = rx->nr2_post_nlevel;
      mode_settings[mode].nr2_post_factor = rx->nr2_post_factor;
      mode_settings[mode].nr2_post_rate = rx->nr2_post_rate;
    }
    mode_settings[mode].nr_agc = rx->nr_agc;
    mode_settings[mode].nb2_mode = rx->nb2_mode;
    mode_settings[mode].nr2_gain_method = rx->nr2_gain_method;
    mode_settings[mode].nr2_npe_method = rx->nr2_npe_method;
    mode_settings[mode].nr2_trained_threshold = rx->nr2_trained_threshold;
    mode_settings[mode].nr2_trained_t2 = rx->nr2_trained_t2;
    mode_settings[mode].nb_tau = rx->nb_tau;
    mode_settings[mode].nb_advtime = rx->nb_advtime;
    mode_settings[mode].nb_hang = rx->nb_hang;
    mode_settings[mode].nb_thresh = rx->nb_thresh;
    mode_settings[mode].nr4_reduction_amount = rx->nr4_reduction_amount;
    mode_settings[mode].nr4_smoothing_factor = rx->nr4_smoothing_factor;
    mode_settings[mode].nr4_whitening_factor = rx->nr4_whitening_factor;
    mode_settings[mode].nr4_noise_rescale = rx->nr4_noise_rescale;
    mode_settings[mode].nr4_post_filter_threshold = rx->nr4_post_filter_threshold;
    copy_mode_settings(mode);
  }
  rx_set_noise(rx);
  g_idle_add(ext_vfo_update, NULL);
  if (display_sliders && rx == active_receiver) {
    update_slider_nr_btn(noise_nr_allowed(rx));
    update_slider_snb_button(noise_nr_allowed(rx));
  }
  noise_menu_sync_page(noise_menu_page_for_rx(rx));
}

void update_noise(void) {
  update_noise_for_rx(active_receiver);
}

static void update_notch_for_rx(RECEIVER *rx) {
  if (rx == NULL) {
    return;
  }
  rx_set_notch(rx);
  g_idle_add(ext_vfo_update, NULL);
}

void update_notch(void) {
  update_notch_for_rx(active_receiver);
}

static void update_anf_for_rx(RECEIVER *rx) {
  if (rx == NULL) {
    return;
  }
  if (!rx_anf_allowed(rx)) {
    rx->anf = 0;
  }
  if (rx->id == 0 && rx_anf_allowed(rx)) {
    int mode = vfo[rx->id].mode;
    mode_settings[mode].anf = rx->anf;
    copy_mode_settings(mode);
  }
  rx_set_anf(rx);
  g_idle_add(ext_vfo_update, NULL);
}

void update_anf(void) {
  update_anf_for_rx(active_receiver);
}

static void nb_cb(GtkToggleButton *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nb = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  update_noise_for_rx(rx);
}

static void nr_cb(GtkToggleButton *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  if (!noise_nr_allowed(rx) && rx->nr != 0) {
    rx->nr = 0;
    g_signal_handlers_block_by_func(widget, G_CALLBACK(nr_cb), data);
    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
    g_signal_handlers_unblock_by_func(widget, G_CALLBACK(nr_cb), data);
  }
  update_noise_for_rx(rx);
  tci_rx_nr_enable_changed(rx->id);
}

static void anf_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->anf = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  if (rx->anf && !rx_anf_allowed(rx)) {
    rx->anf = 0;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
    return;
  }
  update_anf_for_rx(rx);
  tci_rx_anf_enable_changed(rx->id);
}

static void snb_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->snb = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  if (!noise_nr_allowed(rx) && rx->snb) {
    rx->snb = 0;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
  }
  update_noise_for_rx(rx);
  if (!tci_is_applying()) {
    tci_rx_nb_enable_changed(rx->id);
  }
}

static void mnf_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->mnf = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  update_notch_for_rx(rx);
  tci_rx_nf_enable_changed(rx->id);
}

static void mnf_fbw_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->mnf_fbw = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_notch_for_rx(rx);
}

static void post_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr2_post = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void post_nlevel_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr2_post_nlevel = (int)(gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  update_noise_for_rx(rx);
}

static void post_rate_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr2_post_rate = (int)(gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  update_noise_for_rx(rx);
}

static void post_factor_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr2_post_factor = (int)(gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  update_noise_for_rx(rx);
}

static void post_taper_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr2_post_taper = (int)(gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)) + 0.5);
  update_noise_for_rx(rx);
}

static void ae_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr2_ae = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void pos_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr_agc = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  update_noise_for_rx(rx);
}

static void mode_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nb2_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  update_noise_for_rx(rx);
}

static void gain_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr2_gain_method = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  update_noise_for_rx(rx);
}

static void npe_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr2_npe_method = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  update_noise_for_rx(rx);
}

static void trained_thr_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr2_trained_threshold = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void trained_t2_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr2_trained_t2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void slew_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nb_tau = 0.001 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void lead_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nb_advtime = 0.001 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void lag_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nb_hang = 0.001 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void thresh_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nb_thresh = 0.165 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void nr_sel_changed(GtkWidget *widget, gpointer data) {
  NOISE_MENU_PAGE *page = (NOISE_MENU_PAGE *) data;
  if (page == NULL) {
    return;
  }
  // show or hide all controls for NR settings
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(page->nr_container);
  } else {
    gtk_widget_hide(page->nr_container);
  }
}

static void nb_sel_changed(GtkWidget *widget, gpointer data) {
  NOISE_MENU_PAGE *page = (NOISE_MENU_PAGE *) data;
  if (page == NULL) {
    return;
  }
  // show or hide all controls for NB settings
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(page->nb_container);
  } else {
    gtk_widget_hide(page->nb_container);
  }
}

static void nr4_sel_changed(GtkWidget *widget, gpointer data) {
  NOISE_MENU_PAGE *page = (NOISE_MENU_PAGE *) data;
  if (page == NULL) {
    return;
  }
  // show or hide all controls for NR4 settings
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_widget_show(page->nr4_container);
  } else {
    gtk_widget_hide(page->nr4_container);
  }
}

static void nr4_reduction_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr4_reduction_amount = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void nr4_smoothing_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr4_smoothing_factor = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void nr4_whitening_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr4_whitening_factor = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void nr4_rescale_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr4_noise_rescale = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void nr4_threshold_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = noise_menu_get_rx(data);
  if (rx == NULL) {
    return;
  }
  rx->nr4_post_filter_threshold = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  update_noise_for_rx(rx);
}

static void noise_menu_update_page_selection(NOISE_MENU_PAGE *page) {
  if (page == NULL || page->rx == NULL) {
    return;
  }
  if (page->rx->nr == 4) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->nr4_sel), TRUE);
    gtk_widget_show(page->nr4_container);
    gtk_widget_hide(page->nr_container);
    gtk_widget_hide(page->nb_container);
  } else if (page->rx->nb > 0) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->nb_sel), TRUE);
    gtk_widget_show(page->nb_container);
    gtk_widget_hide(page->nr_container);
    gtk_widget_hide(page->nr4_container);
  } else {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->nr_sel), TRUE);
    gtk_widget_show(page->nr_container);
    gtk_widget_hide(page->nb_container);
    gtk_widget_hide(page->nr4_container);
  }
}

static const char *noise_menu_stack_name(int rx_id) {
  return rx_id == 1 ? "rx2" : "rx1";
}

static void noise_menu_update_title(int rx_id) {
  char title[80];
  if (receivers > 1) {
    snprintf(title, sizeof(title), "%s - Noise [NR1-NR4] - RX%d", PGNAME, rx_id + 1);
  } else {
    snprintf(title, sizeof(title), "%s - Noise [NR1-NR4]", PGNAME);
  }
  if (dialog != NULL) {
    gtk_window_set_title(GTK_WINDOW(dialog), title);
  }
  if (GTK_IS_HEADER_BAR(noise_menu_headerbar)) {
    gtk_header_bar_set_title(GTK_HEADER_BAR(noise_menu_headerbar), title);
  }
}

static void noise_menu_select_rx(int rx_id) {
  if (rx_id < 0 || rx_id >= receivers || receiver[rx_id] == NULL) {
    rx_id = 0;
  }
  noise_menu_updating_rx_buttons = TRUE;
  if (noise_menu_stack != NULL) {
    gtk_stack_set_visible_child_name(GTK_STACK(noise_menu_stack), noise_menu_stack_name(rx_id));
  }
  for (int i = 0; i < 2; i++) {
    if (noise_menu_rx_buttons[i] != NULL) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(noise_menu_rx_buttons[i]), i == rx_id);
    }
  }
  noise_menu_updating_rx_buttons = FALSE;
  noise_menu_update_title(rx_id);
}

static void noise_menu_rx_button_cb(GtkToggleButton *button, gpointer data) {
  int rx_id = GPOINTER_TO_INT(data);
  if (noise_menu_updating_rx_buttons) {
    return;
  }
  if (gtk_toggle_button_get_active(button)) {
    noise_menu_select_rx(rx_id);
  } else {
    noise_menu_updating_rx_buttons = TRUE;
    gtk_toggle_button_set_active(button, TRUE);
    noise_menu_updating_rx_buttons = FALSE;
  }
}

static GtkWidget *noise_menu_rx_button_new(const char *label, int rx_id) {
  GtkWidget *button = gtk_toggle_button_new_with_label(label);
  gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(button), FALSE);
  gtk_widget_set_size_request(button, 110, 34);
  g_signal_connect(button, "toggled", G_CALLBACK(noise_menu_rx_button_cb), GINT_TO_POINTER(rx_id));
  return button;
}

static GtkWidget *build_noise_page(RECEIVER *rx, NOISE_MENU_PAGE *page) {
  page->rx = rx;
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
  int row = -1;
  //
  // First row: SNB/ANF/NR method
  //
  //---------------------------------------------------------------------------------
  row++;
  GtkWidget *b_snb = gtk_check_button_new_with_label("SNB");
  page->snb_button = b_snb;
  gtk_widget_set_name(b_snb, "boldlabel");
  gtk_widget_set_tooltip_text(b_snb, "Spectral Noise Blanker");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_snb), rx->snb);
  gtk_widget_show(b_snb);
  gtk_grid_attach(GTK_GRID(grid), b_snb, 0, row, 1, 1);
  g_signal_connect(b_snb, "toggled", G_CALLBACK(snb_cb), rx);
  //---------------------------------------------------------------------------------
  GtkWidget *b_anf = gtk_check_button_new_with_label("ANF");
  gtk_widget_set_name(b_anf, "boldlabel");
  gtk_widget_set_tooltip_text(b_anf, "Auto Notch Filter");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_anf), rx->anf);
  gtk_widget_show(b_anf);
  gtk_grid_attach(GTK_GRID(grid), b_anf, 1, row, 1, 1);
  g_signal_connect(b_anf, "toggled", G_CALLBACK(anf_cb), rx);
  //---------------------------------------------------------------------------------
  GtkWidget *nr_title = gtk_label_new("Noise Reduction");
  gtk_widget_set_name(nr_title, "boldlabel");
  gtk_widget_set_halign(nr_title, GTK_ALIGN_END);
  gtk_widget_show(nr_title);
  gtk_grid_attach(GTK_GRID(grid), nr_title, 2, row, 1, 1);
  GtkWidget *nr_combo = gtk_combo_box_text_new();
  page->nr_combo = nr_combo;
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nr_combo), NULL, "NONE");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nr_combo), NULL, "NR");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nr_combo), NULL, "NR2");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nr_combo), NULL, "NR3");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nr_combo), NULL, "NR4");
  gtk_combo_box_set_active(GTK_COMBO_BOX(nr_combo), rx->nr);
  gtk_widget_set_hexpand(GTK_WIDGET(nr_combo), FALSE);
  gtk_widget_set_halign(nr_combo, GTK_ALIGN_START);
  my_combo_attach(GTK_GRID(grid), nr_combo, 3, row, 1, 1);
  g_signal_connect(nr_combo, "changed", G_CALLBACK(nr_cb), rx);
  //---------------------------------------------------------------------------------
  //
  // Second row: NB selection
  //
  //---------------------------------------------------------------------------------
  row++;
  GtkWidget *pos_title = gtk_label_new("NR/NR2/ANF Position");
  gtk_widget_set_name(pos_title, "boldlabel");
  gtk_widget_set_halign(pos_title, GTK_ALIGN_END);
  gtk_widget_show(pos_title);
  gtk_grid_attach(GTK_GRID(grid), pos_title, 0, row, 1, 1);
  GtkWidget *pos_combo = gtk_combo_box_text_new();
  page->nr_position_combo = pos_combo;
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(pos_combo), NULL, "Pre AGC");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(pos_combo), NULL, "Post AGC");
  gtk_combo_box_set_active(GTK_COMBO_BOX(pos_combo), rx->nr_agc);
  gtk_widget_set_hexpand(GTK_WIDGET(pos_combo), FALSE);
  gtk_widget_set_halign(pos_combo, GTK_ALIGN_START);
  my_combo_attach(GTK_GRID(grid), pos_combo, 1, row, 1, 1);
  g_signal_connect(pos_combo, "changed", G_CALLBACK(pos_cb), rx);
  //---------------------------------------------------------------------------------
  GtkWidget *nb_title = gtk_label_new("Noise Blanker");
  gtk_widget_set_name(nb_title, "boldlabel");
  gtk_widget_set_halign(nb_title, GTK_ALIGN_END);
  gtk_widget_show(nb_title);
  gtk_grid_attach(GTK_GRID(grid), nb_title, 2, row, 1, 1);
  GtkWidget *nb_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nb_combo), NULL, "NONE");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nb_combo), NULL, "NB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(nb_combo), NULL, "NB2");
  gtk_combo_box_set_active(GTK_COMBO_BOX(nb_combo), rx->nb);
  gtk_widget_set_hexpand(GTK_WIDGET(nb_combo), FALSE);
  gtk_widget_set_halign(nb_combo, GTK_ALIGN_START);
  my_combo_attach(GTK_GRID(grid), nb_combo, 3, row, 1, 1);
  g_signal_connect(nb_combo, "changed", G_CALLBACK(nb_cb), rx);
  //---------------------------------------------------------------------------------
  row++;
  GtkWidget *b_mnf = gtk_check_button_new_with_label("MNF");
  gtk_widget_set_name(b_mnf, "boldlabel");
  gtk_widget_set_tooltip_text(b_mnf, "Enable Manual Notch Filter");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_mnf), rx->mnf);
  gtk_widget_show(b_mnf);
  gtk_grid_attach(GTK_GRID(grid), b_mnf, 0, row, 1, 1);
  g_signal_connect(b_mnf, "toggled", G_CALLBACK(mnf_cb), rx);
  GtkWidget *mnf_fbw_b = gtk_spin_button_new_with_range(10.0, 15000.0, 10.0);
  gtk_widget_set_tooltip_text(mnf_fbw_b, "Notch Filter Bandwidth (Hz)");
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(mnf_fbw_b), rx->mnf_fbw);
  gtk_widget_show(mnf_fbw_b);
  gtk_grid_attach(GTK_GRID(grid), mnf_fbw_b, 1, row, 1, 1);
  g_signal_connect(mnf_fbw_b, "changed", G_CALLBACK(mnf_fbw_cb), rx);
  //---------------------------------------------------------------------------------
  row++;
  GtkWidget *line = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(line, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), line, 0, row, 4, 1);
  //---------------------------------------------------------------------------------
  //
  // Third row: select settings: NR, NB, NR4 settings
  //
  row++;
  GtkWidget *nr_sel = gtk_radio_button_new_with_label_from_widget(NULL, "NR Settings");
  page->nr_sel = nr_sel;
  gtk_widget_set_name(nr_sel, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nr_sel), 1);
  gtk_widget_show(nr_sel);
  gtk_grid_attach(GTK_GRID(grid), nr_sel, 0, row, 1, 1);
  g_signal_connect(nr_sel, "toggled", G_CALLBACK(nr_sel_changed), page);
  //
  GtkWidget *nb_sel = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(nr_sel), "NB Settings");
  page->nb_sel = nb_sel;
  gtk_widget_set_name(nb_sel, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nb_sel), 0);
  gtk_widget_show(nb_sel);
  gtk_grid_attach(GTK_GRID(grid), nb_sel, 1, row, 1, 1);
  g_signal_connect(nb_sel, "toggled", G_CALLBACK(nb_sel_changed), page);
  GtkWidget *nr4_sel = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(nr_sel), "NR4 Settings");
  page->nr4_sel = nr4_sel;
  gtk_widget_set_name(nr4_sel, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nr4_sel), 0);
  gtk_widget_show(nr4_sel);
  gtk_grid_attach(GTK_GRID(grid), nr4_sel, 2, row, 1, 1);
  g_signal_connect(nr4_sel, "toggled", G_CALLBACK(nr4_sel_changed), page);
  //
  // Hiding/Showing ComboBoxes optimized for Touch-Screens does not
  // work. Therefore, we have to group the NR, NB, and NR4 controls
  // in a container, which then can be shown/hidden
  //
  //
  // NR controls
  //
  row++;
  page->nr_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), page->nr_container, 0, row, 4, 3);
  GtkWidget *nr_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(nr_grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(nr_grid), TRUE);
  gtk_grid_set_column_spacing(GTK_GRID(nr_grid), 5);
  gtk_grid_set_row_spacing(GTK_GRID(nr_grid), 5);
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
  gtk_combo_box_set_active(GTK_COMBO_BOX(gain_combo), rx->nr2_gain_method);
  gtk_widget_set_hexpand(GTK_WIDGET(gain_combo), FALSE);
  gtk_widget_set_halign(gain_combo, GTK_ALIGN_START);
  my_combo_attach(GTK_GRID(nr_grid), gain_combo, 1, 0, 1, 1);
  g_signal_connect(gain_combo, "changed", G_CALLBACK(gain_cb), rx);
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
  gtk_combo_box_set_active(GTK_COMBO_BOX(npe_combo), rx->nr2_npe_method);
  gtk_widget_set_hexpand(GTK_WIDGET(npe_combo), FALSE);
  gtk_widget_set_halign(npe_combo, GTK_ALIGN_START);
  my_combo_attach(GTK_GRID(nr_grid), npe_combo, 3, 0, 1, 1);
  g_signal_connect(npe_combo, "changed", G_CALLBACK(npe_cb), rx);
  //
  GtkWidget *box_ae = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *l_ae = gtk_label_new("NR2 Artifact Elimination");
  gtk_widget_set_name(l_ae, "boldlabel");
  GtkWidget *b_ae = gtk_check_button_new();
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_ae), rx->nr2_ae);
  g_signal_connect(b_ae, "toggled", G_CALLBACK(ae_cb), rx);
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
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_nr2_post), rx->nr2_post);
    g_signal_connect(b_nr2_post, "toggled", G_CALLBACK(post_cb), rx);
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
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(b_nr2_post_nlevel), rx->nr2_post_nlevel);
    gtk_widget_set_hexpand(GTK_WIDGET(b_nr2_post_nlevel), FALSE);
    gtk_widget_set_halign(b_nr2_post_nlevel, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(nr_grid), b_nr2_post_nlevel, 1, 3, 1, 1);
    g_signal_connect(b_nr2_post_nlevel, "changed", G_CALLBACK(post_nlevel_cb), rx);
    //
    GtkWidget *l_nr2_post_rate = gtk_label_new("NR2 Post Rate");
    gtk_widget_set_name(l_nr2_post_rate, "boldlabel");
    gtk_widget_set_halign(l_nr2_post_rate, GTK_ALIGN_END);
    gtk_widget_show(l_nr2_post_rate);
    gtk_grid_attach(GTK_GRID(nr_grid), l_nr2_post_rate, 0, 4, 1, 1);
    GtkWidget *b_nr2_post_rate = gtk_spin_button_new_with_range(0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(b_nr2_post_rate), rx->nr2_post_rate);
    gtk_widget_set_hexpand(GTK_WIDGET(b_nr2_post_rate), FALSE);
    gtk_widget_set_halign(b_nr2_post_rate, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(nr_grid), b_nr2_post_rate, 1, 4, 1, 1);
    g_signal_connect(b_nr2_post_rate, "changed", G_CALLBACK(post_rate_cb), rx);
    //
    GtkWidget *l_nr2_post_factor = gtk_label_new("NR2 Post Factor");
    gtk_widget_set_name(l_nr2_post_factor, "boldlabel");
    gtk_widget_set_halign(l_nr2_post_factor, GTK_ALIGN_END);
    gtk_widget_show(l_nr2_post_factor);
    gtk_grid_attach(GTK_GRID(nr_grid), l_nr2_post_factor, 2, 3, 1, 1);
    GtkWidget *b_nr2_post_factor = gtk_spin_button_new_with_range(0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(b_nr2_post_factor), rx->nr2_post_factor);
    gtk_widget_set_hexpand(GTK_WIDGET(b_nr2_post_factor), FALSE);
    gtk_widget_set_halign(b_nr2_post_factor, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(nr_grid), b_nr2_post_factor, 3, 3, 1, 1);
    g_signal_connect(b_nr2_post_factor, "changed", G_CALLBACK(post_factor_cb), rx);
    //
    GtkWidget *l_nr2_post_taper = gtk_label_new("NR2 Post Taper");
    gtk_widget_set_name(l_nr2_post_taper, "boldlabel");
    gtk_widget_set_halign(l_nr2_post_taper, GTK_ALIGN_END);
    gtk_widget_show(l_nr2_post_taper);
    gtk_grid_attach(GTK_GRID(nr_grid), l_nr2_post_taper, 2, 4, 1, 1);
    GtkWidget *b_nr2_post_taper = gtk_spin_button_new_with_range(0, 100.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(b_nr2_post_taper), rx->nr2_post_taper);
    gtk_widget_set_hexpand(GTK_WIDGET(b_nr2_post_taper), FALSE);
    gtk_widget_set_halign(b_nr2_post_taper, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(nr_grid), b_nr2_post_taper, 3, 4, 1, 1);
    g_signal_connect(b_nr2_post_taper, "changed", G_CALLBACK(post_taper_cb), rx);
  }
  //
  GtkWidget *trained_thr_title = gtk_label_new("NR2 Trained Thresh");
  gtk_widget_set_name(trained_thr_title, "boldlabel");
  gtk_widget_set_halign(trained_thr_title, GTK_ALIGN_END);
  gtk_widget_show(trained_thr_title);
  gtk_grid_attach(GTK_GRID(nr_grid), trained_thr_title, 0, 1, 1, 1);
  GtkWidget *trained_thr_b = gtk_spin_button_new_with_range(-5.0, 5.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(trained_thr_b), rx->nr2_trained_threshold);
  gtk_widget_set_hexpand(GTK_WIDGET(trained_thr_b), FALSE);
  gtk_widget_set_halign(trained_thr_b, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(nr_grid), trained_thr_b, 1, 1, 1, 1);
  g_signal_connect(trained_thr_b, "changed", G_CALLBACK(trained_thr_cb), rx);
  //
  GtkWidget *trained_t2_title = gtk_label_new("NR2 Trained T2");
  gtk_widget_set_name(trained_t2_title, "boldlabel");
  gtk_widget_set_halign(trained_t2_title, GTK_ALIGN_END);
  gtk_widget_show(trained_t2_title);
  gtk_grid_attach(GTK_GRID(nr_grid), trained_t2_title, 2, 1, 1, 1);
  GtkWidget *trained_t2_b = gtk_spin_button_new_with_range(0.02, 0.3, 0.01);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(trained_t2_b), rx->nr2_trained_t2);
  gtk_widget_set_hexpand(GTK_WIDGET(trained_t2_b), FALSE);
  gtk_widget_set_halign(trained_t2_b, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(nr_grid), trained_t2_b, 3, 1, 1, 1);
  g_signal_connect(trained_t2_b, "changed", G_CALLBACK(trained_t2_cb), rx);
  //
  gtk_container_add(GTK_CONTAINER(page->nr_container), nr_grid);
  //
  // NB controls starting on row 4
  //
  page->nb_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), page->nb_container, 0, row, 4, 3);
  GtkWidget *nb_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(nb_grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(nb_grid), TRUE);
  gtk_grid_set_column_spacing(GTK_GRID(nb_grid), 5);
  gtk_grid_set_row_spacing(GTK_GRID(nb_grid), 5);
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
  gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), rx->nb2_mode);
  my_combo_attach(GTK_GRID(nb_grid), mode_combo, 1, 0, 1, 1);
  g_signal_connect(mode_combo, "changed", G_CALLBACK(mode_cb), rx);
  //
  GtkWidget *slew_title = gtk_label_new("NB Slew time (ms)");
  gtk_widget_set_name(slew_title, "boldlabel");
  gtk_widget_set_halign(slew_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), slew_title, 0, 1, 1, 1);
  GtkWidget *slew_b = gtk_spin_button_new_with_range(0.0, 0.1, 0.001);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(slew_b), rx->nb_tau * 1000.0);
  gtk_grid_attach(GTK_GRID(nb_grid), slew_b, 1, 1, 1, 1);
  g_signal_connect(slew_b, "changed", G_CALLBACK(slew_cb), rx);
  //
  GtkWidget *lead_title = gtk_label_new("NB Lead time (ms)");
  gtk_widget_set_name(lead_title, "boldlabel");
  gtk_widget_set_halign(lead_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), lead_title, 2, 1, 1, 1);
  GtkWidget *lead_b = gtk_spin_button_new_with_range(0.0, 0.1, 0.001);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(lead_b), rx->nb_advtime * 1000.0);
  gtk_grid_attach(GTK_GRID(nb_grid), lead_b, 3, 1, 1, 1);
  g_signal_connect(lead_b, "changed", G_CALLBACK(lead_cb), rx);
  //
  GtkWidget *lag_title = gtk_label_new("NB Lag time (ms)");
  gtk_widget_set_name(lag_title, "boldlabel");
  gtk_widget_set_halign(lag_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), lag_title, 0, 2, 1, 1);
  GtkWidget *lag_b = gtk_spin_button_new_with_range(0.0, 0.1, 0.001);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(lag_b), rx->nb_hang * 1000.0);
  gtk_grid_attach(GTK_GRID(nb_grid), lag_b, 1, 2, 1, 1);
  g_signal_connect(lag_b, "changed", G_CALLBACK(lag_cb), rx);
  //
  GtkWidget *thresh_title = gtk_label_new("NB Threshold");
  gtk_widget_set_name(thresh_title, "boldlabel");
  gtk_widget_set_halign(thresh_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nb_grid), thresh_title, 2, 2, 1, 1);
  GtkWidget *thresh_b = gtk_spin_button_new_with_range(15.0, 500.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(thresh_b), rx->nb_thresh * 6.0606060606);   // 1.0/0.165
  gtk_grid_attach(GTK_GRID(nb_grid), thresh_b, 3, 2, 1, 1);
  g_signal_connect(thresh_b, "changed", G_CALLBACK(thresh_cb), rx);
  gtk_container_add(GTK_CONTAINER(page->nb_container), nb_grid);
  //
  // NR4 controls starting at row 4
  //
  page->nr4_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(grid), page->nr4_container, 0, row, 4, 3);
  GtkWidget *nr4_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(nr4_grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(nr4_grid), TRUE);
  gtk_grid_set_column_spacing(GTK_GRID(nr4_grid), 5);
  gtk_grid_set_row_spacing(GTK_GRID(nr4_grid), 5);
  //
  GtkWidget *nr4_reduction_title = gtk_label_new("NR4 Reduction (dB)");
  gtk_widget_set_name(nr4_reduction_title, "boldlabel");
  gtk_widget_set_halign(nr4_reduction_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_reduction_title, 0, 0, 1, 1);
  GtkWidget *nr4_reduction_b = gtk_spin_button_new_with_range(0.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(nr4_reduction_b), rx->nr4_reduction_amount);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_reduction_b, 1, 0, 1, 1);
  g_signal_connect(G_OBJECT(nr4_reduction_b), "changed", G_CALLBACK(nr4_reduction_cb), rx);
  //
  GtkWidget *nr4_smoothing_title = gtk_label_new("NR4 Smoothing (%)");
  gtk_widget_set_name(nr4_smoothing_title, "boldlabel");
  gtk_widget_set_halign(nr4_smoothing_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_smoothing_title, 2, 0, 1, 1);
  GtkWidget *nr4_smoothing_b = gtk_spin_button_new_with_range(0.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(nr4_smoothing_b), rx->nr4_smoothing_factor);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_smoothing_b, 3, 0, 1, 1);
  g_signal_connect(G_OBJECT(nr4_smoothing_b), "changed", G_CALLBACK(nr4_smoothing_cb), rx);
  //
  GtkWidget *nr4_whitening_title = gtk_label_new("NR4 Whitening (%)");
  gtk_widget_set_name(nr4_whitening_title, "boldlabel");
  gtk_widget_set_halign(nr4_whitening_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_whitening_title, 0, 1, 1, 1);
  GtkWidget *nr4_whitening_b = gtk_spin_button_new_with_range(0.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(nr4_whitening_b), rx->nr4_whitening_factor);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_whitening_b, 1, 1, 1, 1);
  g_signal_connect(G_OBJECT(nr4_whitening_b), "changed", G_CALLBACK(nr4_whitening_cb), rx);
  //
  GtkWidget *nr4_rescale_title = gtk_label_new("NR4 rescale (dB)");
  gtk_widget_set_name(nr4_rescale_title, "boldlabel");
  gtk_widget_set_halign(nr4_rescale_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_rescale_title, 2, 1, 1, 1);
  GtkWidget *nr4_rescale_b = gtk_spin_button_new_with_range(0.0, 12.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(nr4_rescale_b), rx->nr4_noise_rescale);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_rescale_b, 3, 1, 1, 1);
  g_signal_connect(G_OBJECT(nr4_rescale_b), "changed", G_CALLBACK(nr4_rescale_cb), rx);
  //
  GtkWidget *nr4_threshold_title = gtk_label_new("NR4 post filter threshold (dB)");
  gtk_widget_set_name(nr4_threshold_title, "boldlabel");
  gtk_widget_set_halign(nr4_threshold_title, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_threshold_title, 1, 2, 2, 1);
  GtkWidget *nr4_threshold_b = gtk_spin_button_new_with_range(-10.0, 10.0, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(nr4_threshold_b), rx->nr4_post_filter_threshold);
  gtk_grid_attach(GTK_GRID(nr4_grid), nr4_threshold_b, 3, 2, 1, 1);
  g_signal_connect(G_OBJECT(nr4_threshold_b), "changed", G_CALLBACK(nr4_threshold_cb), rx);
  gtk_container_add(GTK_CONTAINER(page->nr4_container), nr4_grid);
  return grid;
}

void noise_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  noise_menu_headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), noise_menu_headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(noise_menu_headerbar), TRUE);
  noise_menu_update_title(active_receiver != NULL ? active_receiver->id : 0);
  g_signal_connect(dialog, "delete_event", G_CALLBACK(close_cb), NULL);
  g_signal_connect(dialog, "destroy", G_CALLBACK(close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content), 0);
  GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top(outer, 8);
  gtk_widget_set_margin_bottom(outer, 8);
  gtk_widget_set_margin_start(outer, 8);
  gtk_widget_set_margin_end(outer, 8);
  memset(noise_menu_pages, 0, sizeof(noise_menu_pages));
  noise_menu_rx_buttons[0] = NULL;
  noise_menu_rx_buttons[1] = NULL;
  if (receivers > 1 && receiver[1] != NULL) {
    GtkWidget *buttonbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(buttonbar, GTK_ALIGN_START);
    noise_menu_rx_buttons[0] = noise_menu_rx_button_new("RX1", 0);
    gtk_box_pack_start(GTK_BOX(buttonbar), noise_menu_rx_buttons[0], FALSE, FALSE, 0);
    noise_menu_rx_buttons[1] = noise_menu_rx_button_new("RX2", 1);
    gtk_box_pack_start(GTK_BOX(buttonbar), noise_menu_rx_buttons[1], FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), buttonbar, FALSE, FALSE, 0);
    noise_menu_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(noise_menu_stack), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_widget_set_hexpand(noise_menu_stack, TRUE);
    gtk_widget_set_vexpand(noise_menu_stack, TRUE);
    gtk_stack_add_named(GTK_STACK(noise_menu_stack),
                        build_noise_page(receiver[0], &noise_menu_pages[0]),
                        "rx1");
    gtk_stack_add_named(GTK_STACK(noise_menu_stack),
                        build_noise_page(receiver[1], &noise_menu_pages[1]),
                        "rx2");
    gtk_box_pack_start(GTK_BOX(outer), noise_menu_stack, TRUE, TRUE, 0);
  } else {
    noise_menu_stack = NULL;
    gtk_box_pack_start(GTK_BOX(outer), build_noise_page(receiver[0], &noise_menu_pages[0]), TRUE, TRUE, 0);
  }
  GtkWidget *close_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign(close_box, GTK_ALIGN_CENTER);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  gtk_widget_set_size_request(close_b, 90, 32);
  g_signal_connect(close_b, "clicked", G_CALLBACK(close_cb), NULL);
  gtk_box_pack_start(GTK_BOX(close_box), close_b, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(outer), close_box, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(content), outer);
  sub_menu = dialog;
  if (receivers > 1 && receiver[1] != NULL) {
    noise_menu_select_rx(active_receiver != NULL ? active_receiver->id : 0);
  }
  gtk_widget_show_all(dialog);
  noise_menu_update_page_selection(&noise_menu_pages[0]);
  if (receivers > 1 && receiver[1] != NULL) {
    noise_menu_update_page_selection(&noise_menu_pages[1]);
    noise_menu_select_rx(active_receiver != NULL ? active_receiver->id : 0);
  } else {
    noise_menu_update_title(0);
  }
}
