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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "new_menu.h"
#include "agc_menu.h"
#include "agc.h"
#include "band.h"
#include "radio.h"
#include "receiver.h"
#include "vfo.h"
#include "ext.h"
#include "sliders.h"

#define MAX_AGC_MENU_RECEIVERS 8

static GtkWidget *agc_hang_threshold_label[MAX_AGC_MENU_RECEIVERS];
static GtkWidget *agc_hang_threshold_scale[MAX_AGC_MENU_RECEIVERS];
static gulong threshold_scale_signal_id[MAX_AGC_MENU_RECEIVERS];
static GtkWidget *dialog = NULL;

static int agc_menu_rx_index(const RECEIVER *rx) {
  if (rx == NULL || rx->id < 0 || rx->id >= MAX_AGC_MENU_RECEIVERS) {
    return -1;
  }
  return rx->id;
}

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

static void update_agc_hang_threshold_scale(RECEIVER *rx) {
  int id = agc_menu_rx_index(rx);
  if (id < 0 || agc_hang_threshold_label[id] == NULL || agc_hang_threshold_scale[id] == NULL) {
    return;
  }
  if (threshold_scale_signal_id[id] != 0) {
    g_signal_handler_block(G_OBJECT(agc_hang_threshold_scale[id]), threshold_scale_signal_id[id]);
  }
  gtk_widget_show(agc_hang_threshold_label[id]);
  gtk_widget_show(agc_hang_threshold_scale[id]);
  gtk_widget_set_sensitive(agc_hang_threshold_label[id],
                           rx->agc == AGC_LONG || rx->agc == AGC_SLOW);
  gtk_widget_set_sensitive(agc_hang_threshold_scale[id],
                           rx->agc == AGC_LONG || rx->agc == AGC_SLOW);
  if (threshold_scale_signal_id[id] != 0) {
    g_signal_handler_unblock(G_OBJECT(agc_hang_threshold_scale[id]), threshold_scale_signal_id[id]);
  }
}

static void agc_hang_threshold_value_changed_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = (RECEIVER *) data;
  if (rx == NULL) {
    return;
  }
  rx->agc_hang_threshold = (int) gtk_range_get_value(GTK_RANGE(widget));
  rx_set_agc(rx);
}

static void agc_cb(GtkComboBox *widget, gpointer data) {
  RECEIVER *rx = (RECEIVER *) data;
  if (rx == NULL) {
    return;
  }
  rx->agc = gtk_combo_box_get_active(widget);
  rx_set_agc(rx);
  if (rx == active_receiver) {
    g_idle_add(ext_vfo_update, NULL);
    update_slider_agc_btn();
  }
  update_agc_hang_threshold_scale(rx);
}

static void add_agc_rx_controls(GtkWidget *grid, int row, RECEIVER *rx, int show_rx_name, int box_width,
                                int widget_heigth) {
  int id = agc_menu_rx_index(rx);
  char label[64];
  if (id < 0) {
    return;
  }
  GtkWidget *box_agc = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
  gtk_widget_set_size_request(box_agc, box_width, widget_heigth);
  gtk_box_set_spacing(GTK_BOX(box_agc), 5);
  if (show_rx_name) {
    snprintf(label, sizeof(label), "AGC RX%d", rx->id + 1);
  } else {
    snprintf(label, sizeof(label), "AGC");
  }
  GtkWidget *agc_title = gtk_label_new(label);
  gtk_widget_set_name(agc_title, "boldlabel_border_black");
  gtk_widget_set_size_request(agc_title, box_width / 2, -1);
  gtk_widget_set_margin_top(agc_title, 0);
  gtk_widget_set_margin_bottom(agc_title, 0);
  gtk_widget_set_margin_end(agc_title, 0);
  gtk_widget_set_halign(agc_title, GTK_ALIGN_START);
  gtk_widget_set_valign(agc_title, GTK_ALIGN_CENTER);
  gtk_widget_show(agc_title);
  gtk_box_pack_start(GTK_BOX(box_agc), agc_title, FALSE, FALSE, 0);
  GtkWidget *agc_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Off");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Long");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Slow");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Medium");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Fast");
  gtk_combo_box_set_active(GTK_COMBO_BOX(agc_combo), rx->agc);
  gtk_widget_set_size_request(agc_combo, box_width / 2, -1);
  gtk_widget_set_margin_top(agc_combo, 0);
  gtk_widget_set_margin_bottom(agc_combo, 0);
  gtk_widget_set_margin_end(agc_combo, 0);
  gtk_widget_set_halign(agc_combo, GTK_ALIGN_START);
  gtk_widget_set_valign(agc_combo, GTK_ALIGN_CENTER);
  g_signal_connect(agc_combo, "changed", G_CALLBACK(agc_cb), rx);
  gtk_box_pack_start(GTK_BOX(box_agc), agc_combo, FALSE, FALSE, 0);
  gtk_grid_attach(GTK_GRID(grid), box_agc, 0, row, 1, 1);
  GtkWidget *box_threshold = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
  gtk_widget_set_size_request(box_threshold, box_width, widget_heigth);
  gtk_box_set_spacing(GTK_BOX(box_threshold), 5);
  if (show_rx_name) {
    snprintf(label, sizeof(label), "Hang Threshold RX%d", rx->id + 1);
  } else {
    snprintf(label, sizeof(label), "Hang Threshold");
  }
  agc_hang_threshold_label[id] = gtk_label_new(label);
  gtk_widget_set_name(agc_hang_threshold_label[id], "boldlabel_border_black");
  gtk_widget_set_size_request(agc_hang_threshold_label[id], box_width / 2, -1);
  gtk_widget_set_margin_top(agc_hang_threshold_label[id], 0);
  gtk_widget_set_margin_bottom(agc_hang_threshold_label[id], 0);
  gtk_widget_set_margin_end(agc_hang_threshold_label[id], 0);
  gtk_widget_set_halign(agc_hang_threshold_label[id], GTK_ALIGN_START);
  gtk_widget_set_valign(agc_hang_threshold_label[id], GTK_ALIGN_CENTER);
  gtk_widget_show(agc_hang_threshold_label[id]);
  gtk_box_pack_start(GTK_BOX(box_threshold), agc_hang_threshold_label[id], FALSE, FALSE, 0);
  agc_hang_threshold_scale[id] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  gtk_range_set_increments(GTK_RANGE(agc_hang_threshold_scale[id]), 1.0, 1.0);
  gtk_range_set_value(GTK_RANGE(agc_hang_threshold_scale[id]), rx->agc_hang_threshold);
  for (float i = 0.0; i <= 100.0; i += 50.0) {
    gtk_scale_add_mark(GTK_SCALE(agc_hang_threshold_scale[id]), i, GTK_POS_TOP, NULL);
  }
  gtk_widget_set_size_request(agc_hang_threshold_scale[id], box_width / 2, -1);
  gtk_widget_set_margin_top(agc_hang_threshold_scale[id], 0);
  gtk_widget_set_margin_bottom(agc_hang_threshold_scale[id], 0);
  gtk_widget_set_margin_end(agc_hang_threshold_scale[id], 0);
  gtk_widget_set_halign(agc_hang_threshold_scale[id], GTK_ALIGN_START);
  gtk_widget_set_valign(agc_hang_threshold_scale[id], GTK_ALIGN_CENTER);
  gtk_widget_show(agc_hang_threshold_scale[id]);
  threshold_scale_signal_id[id] = g_signal_connect(G_OBJECT(agc_hang_threshold_scale[id]), "value_changed",
                                  G_CALLBACK(agc_hang_threshold_value_changed_cb),
                                  rx);
  gtk_box_pack_start(GTK_BOX(box_threshold), agc_hang_threshold_scale[id], FALSE, FALSE, 0);
  gtk_grid_attach(GTK_GRID(grid), box_threshold, 0, row + 1, 1, 1);
}

void agc_menu(GtkWidget *parent) {
  int box_width = 400;
  int widget_heigth = 50;
  int row = 0;
  int show_rx_name = receivers > 1;
  for (int i = 0; i < MAX_AGC_MENU_RECEIVERS; i++) {
    agc_hang_threshold_label[i] = NULL;
    agc_hang_threshold_scale[i] = NULL;
    threshold_scale_signal_id[i] = 0;
  }
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  char title[64];
  if (show_rx_name) {
    snprintf(title, 64, "%s - AGC", PGNAME);
  } else {
    snprintf(title, 64, "%s - AGC (RX%d VFO-%s)", PGNAME, active_receiver->id + 1, active_receiver->id == 0 ? "A" : "B");
  }
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), title);
  g_signal_connect(dialog, "delete_event", G_CALLBACK(close_cb), NULL);
  g_signal_connect(dialog, "destroy", G_CALLBACK(close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_widget_set_margin_top(grid, 0);
  gtk_widget_set_margin_bottom(grid, 0);
  gtk_widget_set_margin_start(grid, 3);
  gtk_widget_set_margin_end(grid, 3);
  GtkWidget *box_Z0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
  gtk_widget_set_size_request(box_Z0, box_width, widget_heigth);
  gtk_box_set_spacing(GTK_BOX(box_Z0), 5);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  gtk_widget_set_size_request(close_b, 90, -1);
  gtk_widget_set_margin_top(close_b, 0);
  gtk_widget_set_margin_bottom(close_b, 0);
  gtk_widget_set_margin_end(close_b, 0);
  gtk_widget_set_halign(close_b, GTK_ALIGN_START);
  gtk_widget_set_valign(close_b, GTK_ALIGN_CENTER);
  gtk_widget_show(close_b);
  g_signal_connect(close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_box_pack_start(GTK_BOX(box_Z0), close_b, FALSE, FALSE, 0);
  gtk_grid_attach(GTK_GRID(grid), box_Z0, 0, row++, 1, 1);
  for (int i = 0; i < receivers; i++) {
    if (receiver[i] != NULL) {
      add_agc_rx_controls(grid, row, receiver[i], show_rx_name, box_width, widget_heigth);
      row += 2;
    }
  }
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  for (int i = 0; i < receivers; i++) {
    if (receiver[i] != NULL) {
      update_agc_hang_threshold_scale(receiver[i]);
    }
  }
}
