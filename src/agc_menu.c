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

static GtkWidget *agc_hang_threshold_label;
static GtkWidget *agc_hang_threshold_scale;
static gulong threshold_scale_signal_id;
static GtkWidget *dialog = NULL;

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

static void update_agc_hang_threshold_scale(void) {
  g_signal_handler_block(G_OBJECT(agc_hang_threshold_scale), threshold_scale_signal_id);

  if (active_receiver->agc == AGC_LONG || active_receiver->agc == AGC_SLOW) {
    gtk_widget_show(agc_hang_threshold_label);
    gtk_widget_show(agc_hang_threshold_scale);
  } else {
    gtk_widget_hide(agc_hang_threshold_label);
    gtk_widget_hide(agc_hang_threshold_scale);
  }

  g_signal_handler_unblock(G_OBJECT(agc_hang_threshold_scale), threshold_scale_signal_id);
}

static void agc_hang_threshold_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->agc_hang_threshold = (int)gtk_range_get_value(GTK_RANGE(widget));
  rx_set_agc(active_receiver);
}

static void agc_cb (GtkToggleButton *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  active_receiver->agc = val;
  rx_set_agc(active_receiver);
  g_idle_add(ext_vfo_update, NULL);
  update_slider_agc_btn();
  update_agc_hang_threshold_scale();
}

void agc_menu(GtkWidget *parent) {
  int box_width = 400;
  int widget_heigth = 50;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  char title[64];
  snprintf(title, 64, "%s - AGC (RX%d VFO-%s)", PGNAME, active_receiver->id + 1, active_receiver->id == 0 ? "A" : "B");
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_widget_set_margin_top(grid, 0);    // Kein Abstand oben
  gtk_widget_set_margin_bottom(grid, 0); // Kein Abstand unten
  gtk_widget_set_margin_start(grid, 3);  // Kein Abstand am Anfang
  gtk_widget_set_margin_end(grid, 3);    // Kein Abstand am Ende
  //----------------------------------------------------------------------------------------------------------
  GtkWidget *box_Z0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(box_Z0, box_width, widget_heigth);
  gtk_box_set_spacing(GTK_BOX(box_Z0), 5);
  //----------------------------------------------------------------------------------------------------------
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  gtk_widget_set_size_request(close_b, 90, -1);  // z.B. 100px
  gtk_widget_set_margin_top(close_b, 0);
  gtk_widget_set_margin_bottom(close_b, 0);
  gtk_widget_set_margin_end(close_b, 0);    // rechter Rand (Ende)
  gtk_widget_set_halign(close_b, GTK_ALIGN_START);
  gtk_widget_set_valign(close_b, GTK_ALIGN_CENTER);
  gtk_widget_show(close_b);
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_box_pack_start(GTK_BOX(box_Z0), close_b, FALSE, FALSE, 0);
  // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
  gtk_grid_attach(GTK_GRID(grid), box_Z0, 0, 0, 1, 1);  // Zeile 0 Spalte 0
  //----------------------------------------------------------------------------------------------------------
  GtkWidget *box_Z1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(box_Z1, box_width, widget_heigth);
  gtk_box_set_spacing(GTK_BOX(box_Z1), 5);
  //----------------------------------------------------------------------------------------------------------
  GtkWidget *agc_title = gtk_label_new("AGC");
  gtk_widget_set_name(agc_title, "boldlabel_border_black");
  gtk_widget_set_size_request(agc_title, box_width / 2, -1);  // z.B. 100px
  gtk_widget_set_margin_top(agc_title, 0);
  gtk_widget_set_margin_bottom(agc_title, 0);
  gtk_widget_set_margin_end(agc_title, 0);    // rechter Rand (Ende)
  gtk_widget_set_halign(agc_title, GTK_ALIGN_START);
  gtk_widget_set_valign(agc_title, GTK_ALIGN_CENTER);
  gtk_widget_show(agc_title);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(box_Z1), agc_title, FALSE, FALSE, 0);
  //----------------------------------------------------------------------------------------------------------
  GtkWidget *agc_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Off");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Long");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Slow");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Medium");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(agc_combo), NULL, "Fast");
  gtk_combo_box_set_active(GTK_COMBO_BOX(agc_combo), active_receiver->agc);
  gtk_widget_set_size_request(agc_combo, box_width / 2, -1);  // z.B. 100px
  gtk_widget_set_margin_top(agc_combo, 0);
  gtk_widget_set_margin_bottom(agc_combo, 0);
  gtk_widget_set_margin_end(agc_combo, 0);    // rechter Rand (Ende)
  gtk_widget_set_halign(agc_combo, GTK_ALIGN_START);
  gtk_widget_set_valign(agc_combo, GTK_ALIGN_CENTER);
  g_signal_connect(agc_combo, "changed", G_CALLBACK(agc_cb), NULL);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(box_Z1), agc_combo, FALSE, FALSE, 0);
  //----------------------------------------------------------------------------------------------------------
  // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
  gtk_grid_attach(GTK_GRID(grid), box_Z1, 0, 1, 1, 1);  // Zeile 0 Spalte 0
  //----------------------------------------------------------------------------------------------------------
  GtkWidget *box_Z2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(box_Z2, box_width, widget_heigth);
  gtk_box_set_spacing(GTK_BOX(box_Z2), 5);
  //----------------------------------------------------------------------------------------------------------
  agc_hang_threshold_label = gtk_label_new("Hang Threshold");
  gtk_widget_set_name(agc_hang_threshold_label, "boldlabel_border_black");
  // gtk_widget_set_halign(agc_hang_threshold_label, GTK_ALIGN_END);
  gtk_widget_set_size_request(agc_hang_threshold_label, box_width / 2, -1);  // z.B. 100px
  gtk_widget_set_margin_top(agc_hang_threshold_label, 0);
  gtk_widget_set_margin_bottom(agc_hang_threshold_label, 0);
  gtk_widget_set_margin_end(agc_hang_threshold_label, 0);    // rechter Rand (Ende)
  gtk_widget_set_halign(agc_hang_threshold_label, GTK_ALIGN_START);
  gtk_widget_set_valign(agc_hang_threshold_label, GTK_ALIGN_CENTER);
  gtk_widget_show(agc_hang_threshold_label);
  gtk_box_pack_start(GTK_BOX(box_Z2), agc_hang_threshold_label, FALSE, FALSE, 0);
  //----------------------------------------------------------------------------------------------------------
  agc_hang_threshold_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  gtk_range_set_increments (GTK_RANGE(agc_hang_threshold_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(agc_hang_threshold_scale), active_receiver->agc_hang_threshold);

  for (float i = 0.0; i <= 100.0; i += 50.0) {
    gtk_scale_add_mark(GTK_SCALE(agc_hang_threshold_scale), i, GTK_POS_TOP, NULL);
  }

  gtk_widget_set_size_request(agc_hang_threshold_scale, box_width / 2, -1);  // z.B. 100px
  gtk_widget_set_margin_top(agc_hang_threshold_scale, 0);
  gtk_widget_set_margin_bottom(agc_hang_threshold_scale, 0);
  gtk_widget_set_margin_end(agc_hang_threshold_scale, 0);    // rechter Rand (Ende)
  gtk_widget_set_halign(agc_hang_threshold_scale, GTK_ALIGN_START);
  gtk_widget_set_valign(agc_hang_threshold_scale, GTK_ALIGN_CENTER);
  gtk_widget_show(agc_hang_threshold_scale);
  threshold_scale_signal_id = g_signal_connect(G_OBJECT(agc_hang_threshold_scale), "value_changed",
                              G_CALLBACK(agc_hang_threshold_value_changed_cb),
                              NULL);
  gtk_box_pack_start(GTK_BOX(box_Z2), agc_hang_threshold_scale, FALSE, FALSE, 0);
  //----------------------------------------------------------------------------------------------------------
  // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
  gtk_grid_attach(GTK_GRID(grid), box_Z2, 0, 2, 1, 1);  // Zeile 0 Spalte 0
  //----------------------------------------------------------------------------------------------------------
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
