/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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
#include "main.h"
#include "receiver.h"
#include "radio.h"
#include "vfo.h"
#include "sliders.h"
#include "zoompan.h"
#include "actions.h"
#include "ext.h"
#include "message.h"
#include "toolset.h"
#include "rx_panadapter.h"

static int width;
static int height;

static GtkWidget *zoompan;
static GtkWidget *zoom_label;
static GtkWidget *zoom_scale;
static gulong zoom_signal_id;
static GtkWidget *pan_label;
static GtkWidget *pan_scale;
static gulong pan_signal_id;
static GMutex pan_zoom_mutex;
static GtkWidget *peak_btn;
static GtkWidget *peak_label;
static gulong peak_btn_signal_id;
static GMutex peak_mutex;

int zoompan_active_receiver_changed(void *data) {
  if (display_zoompan) {
    g_mutex_lock(&pan_zoom_mutex);
    g_signal_handler_block(G_OBJECT(zoom_scale), zoom_signal_id);
    g_signal_handler_block(G_OBJECT(pan_scale), pan_signal_id);
    gtk_range_set_value(GTK_RANGE(zoom_scale), active_receiver->zoom);
    gtk_range_set_range(GTK_RANGE(pan_scale), 0.0,
                        (double)(active_receiver->zoom == 1 ? active_receiver->pixels : active_receiver->pixels - active_receiver->width));
    gtk_range_set_value (GTK_RANGE(pan_scale), active_receiver->pan);

    if (active_receiver->zoom == 1) {
      gtk_widget_set_sensitive(pan_scale, FALSE);
    }

    g_signal_handler_unblock(G_OBJECT(pan_scale), pan_signal_id);
    g_signal_handler_unblock(G_OBJECT(zoom_scale), zoom_signal_id);
    g_mutex_unlock(&pan_zoom_mutex);
  }

  return FALSE;
}

static void zoom_value_changed_cb(GtkWidget *widget, gpointer data) {
  //t_print("zoom_value_changed_cb\n");
  g_mutex_lock(&pan_zoom_mutex);
  g_mutex_lock(&active_receiver->display_mutex);
  active_receiver->zoom = (int)(gtk_range_get_value(GTK_RANGE(zoom_scale)) + 0.5);
  rx_update_zoom(active_receiver);
  g_signal_handler_block(G_OBJECT(pan_scale), pan_signal_id);
  gtk_range_set_range(GTK_RANGE(pan_scale), 0.0,
                      (double)(active_receiver->zoom == 1 ? active_receiver->pixels : active_receiver->pixels - active_receiver->width));
  gtk_range_set_value (GTK_RANGE(pan_scale), active_receiver->pan);
  g_signal_handler_unblock(G_OBJECT(pan_scale), pan_signal_id);

  if (active_receiver->zoom == 1) {
    gtk_widget_set_sensitive(pan_scale, FALSE);
  } else {
    gtk_widget_set_sensitive(pan_scale, TRUE);
  }

  g_mutex_unlock(&active_receiver->display_mutex);
  g_mutex_unlock(&pan_zoom_mutex);
  g_idle_add(ext_vfo_update, NULL);
}

void set_zoom(int rx, double value) {
  //t_print("set_zoom: %f\n",value);
  if (rx >= receivers) { return; }

  int ival = (int) value;

  if (ival > MAX_ZOOM) { ival = MAX_ZOOM; }

  if (ival < 1       ) { ival = 1; }

  receiver[rx]->zoom = ival;
  rx_update_zoom(receiver[rx]);

  if (display_zoompan && active_receiver->id == rx) {
    gtk_range_set_value (GTK_RANGE(zoom_scale), receiver[rx]->zoom);
  } else {
    char title[64];
    snprintf(title, 64, "Zoom RX%d", rx + 1);
    show_popup_slider(ZOOM, rx, 1.0, MAX_ZOOM, 1.0, receiver[rx]->zoom, title);
  }

  g_idle_add(ext_vfo_update, NULL);
}

void remote_set_zoom(int rx, double value) {
  //t_print("remote_set_zoom: rx=%d zoom=%f\n",rx,value);
  g_mutex_lock(&pan_zoom_mutex);
  g_signal_handler_block(G_OBJECT(zoom_scale), zoom_signal_id);
  g_signal_handler_block(G_OBJECT(pan_scale), pan_signal_id);
  set_zoom(rx, value);
  g_signal_handler_unblock(G_OBJECT(pan_scale), pan_signal_id);
  g_signal_handler_unblock(G_OBJECT(zoom_scale), zoom_signal_id);
  g_mutex_unlock(&pan_zoom_mutex);
  //t_print("remote_set_zoom: EXIT\n");
}

static void toggle_cb(GtkWidget *widget, gpointer data) {
  int *value = (int *) data;

  if (active_receiver) {
    rx_panadapter_peak_hold_clear(active_receiver);
  }

  g_mutex_lock(&peak_mutex);
  *value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  g_mutex_unlock(&peak_mutex);
}

static void pan_value_changed_cb(GtkWidget *widget, gpointer data) {
  //t_print("pan_value_changed_cb\n");
  g_mutex_lock(&pan_zoom_mutex);

  if (active_receiver->zoom > 1) {
    active_receiver->pan = (int)(gtk_range_get_value(GTK_RANGE(pan_scale)) + 0.5);
  }

  g_mutex_unlock(&pan_zoom_mutex);
}

void set_pan(int rx, double value) {
  //t_print("set_pan: value=%f\n",value);
  if (rx >= receivers) { return; }

  if (receiver[rx]->zoom == 1) {
    receiver[rx]->pan = 0;
    return;
  }

  int ival = (int) value;

  if (ival < 0) { ival = 0; }

  if (ival > (receiver[rx]->pixels - receiver[rx]->width)) { ival = receiver[rx]->pixels - receiver[rx]->width; }

  receiver[rx]->pan = ival;

  if (display_zoompan && rx == active_receiver->id) {
    gtk_range_set_value (GTK_RANGE(pan_scale), receiver[rx]->pan);
  } else {
    char title[64];
    snprintf(title, 64, "Pan RX%d", rx + 1);
    show_popup_slider(PAN, rx, 0.0, receiver[rx]->pixels - receiver[rx]->width, 1.00, receiver[rx]->pan, title);
  }
}

void remote_set_pan(int rx, double value) {
  //t_print("remote_set_pan: rx=%d pan=%f\n",rx,value);
  if (rx >= receivers) { return; }

  g_mutex_lock(&pan_zoom_mutex);
  g_signal_handler_block(G_OBJECT(pan_scale), pan_signal_id);
  gtk_range_set_range(GTK_RANGE(pan_scale), 0.0,
                      (double)(receiver[rx]->zoom == 1 ? receiver[rx]->pixels : receiver[rx]->pixels - receiver[rx]->width));
  set_pan(rx, value);
  g_signal_handler_unblock(G_OBJECT(pan_scale), pan_signal_id);
  g_mutex_unlock(&pan_zoom_mutex);
  //t_print("remote_set_pan: EXIT\n");
}

GtkWidget *zoompan_init(int my_width, int my_height) {
  if (zoompan) {
    g_clear_pointer(&zoompan, gtk_widget_destroy);
  }

  // width = my_width - 50;
  width = my_width;
  height = my_height;
  t_print("%s: width=%d height=%d\n", __FUNCTION__, width, height);
  int widget_height = 0;
  widget_height = height;
  // int zoombox_width = (int)floor(width / 2.95); // Breite zoom_box
  int zoombox_width = (int)floor(width / 3); // Breite zoom_box
  // int panbox_width = width / 2.68; // Breite pan_box
  int panbox_width = width - zoombox_width; // Breite pan_box: Gesamtbreite - Breite zoom_box
  t_print("%s: zoombox_width=%d panbox_width=%d summe=%d\n", __FUNCTION__, zoombox_width, panbox_width,
          zoombox_width + panbox_width);
  zoompan = gtk_grid_new();
  WEAKEN(zoompan);
  gtk_widget_set_size_request (zoompan, width, height);
  gtk_grid_set_row_homogeneous(GTK_GRID(zoompan), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(zoompan), FALSE);
  gtk_widget_set_margin_top(zoompan, 0);     // Abstand oben
  gtk_widget_set_margin_bottom(zoompan, 0);  // Abstand unten
  gtk_widget_set_margin_start(zoompan, 3);  // Abstand am Anfang
  gtk_widget_set_margin_end(zoompan, 3);     // Abstand am Ende
  //-----------------------------------------------------------------------------------------------------------
  // Hauptcontainer: horizontale Box für Zoom
  GtkWidget *zoom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(zoom_box, zoombox_width, widget_height);
  //-----------------------------------------------------------------------------------------------------------
  zoom_label = gtk_label_new("Zoom");
  WEAKEN(zoom_label);
  gtk_widget_set_name(zoom_label, "boldlabel_border_blue");
  // Label breiter erzwingen
  gtk_widget_set_size_request(zoom_label, 105, -1);  // z.B. 100px
  gtk_widget_set_margin_top(zoom_label, 5);
  gtk_widget_set_margin_bottom(zoom_label, 5);
  gtk_widget_set_margin_end(zoom_label, 5);    // rechter Rand (Ende)
  gtk_widget_set_halign(zoom_label, GTK_ALIGN_START);
  gtk_widget_set_valign(zoom_label, GTK_ALIGN_CENTER);
  //-----------------------------------------------------------------------------------------------------------
  zoom_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, MAX_ZOOM, 1.00);
  WEAKEN(zoom_scale);
  gtk_widget_set_tooltip_text(zoom_scale, "Zoom into the Panadapter");
  gtk_widget_set_margin_end(zoom_scale, 0);  // rechter Rand (Ende)
  gtk_range_set_increments (GTK_RANGE(zoom_scale), 1.0, 1.0);
  gtk_widget_set_hexpand(zoom_scale, FALSE);  // fülle Box nicht nach rechts
  gtk_range_set_value (GTK_RANGE(zoom_scale), active_receiver->zoom);

  for (float i = 1.0; i <= 8.0; i += 1.0) {
    gtk_scale_add_mark(GTK_SCALE(zoom_scale), i, GTK_POS_TOP, NULL);
  }

  zoom_signal_id = g_signal_connect(G_OBJECT(zoom_scale), "value_changed", G_CALLBACK(zoom_value_changed_cb), NULL);
  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(zoom_box), zoom_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(zoom_box), zoom_scale, TRUE, TRUE, 0);
  // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
  gtk_grid_attach(GTK_GRID(zoompan), zoom_box, /* column */ 0, /* row */ 0, /* width */ 1, /* height */ 1);
  gtk_widget_show_all(zoompan);
  //-----------------------------------------------------------------------------------------------------------
  // Hauptcontainer: horizontale Box für Pan
  GtkWidget *pan_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 5px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(pan_box, panbox_width, widget_height);
  //-------------------------------------------------------------------------------------------
  peak_btn = gtk_toggle_button_new_with_label("PEAK");
  WEAKEN(peak_btn);
  gtk_widget_set_name(peak_btn, "medium_toggle_button");
  // gtk_widget_set_name(binaural_btn, "front_toggle_button");
  gtk_widget_set_tooltip_text(peak_btn, "Toggle Peak & Hold in RX Panadapter");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(peak_btn), pan_peak_hold_enabled);
  // begin label definition inside button
  peak_label = gtk_bin_get_child(GTK_BIN(peak_btn));
  gtk_label_set_justify(GTK_LABEL(peak_label), GTK_JUSTIFY_CENTER);
  // end label definition
  gtk_widget_set_size_request(peak_btn, 90, -1);  // z.B. 100px
  gtk_widget_set_margin_top(peak_btn, 5);
  gtk_widget_set_margin_bottom(peak_btn, 5);
  gtk_widget_set_margin_end(peak_btn, 5);    // rechter Rand (Ende)
  gtk_widget_set_margin_start(peak_btn, 0);    // linker Rand (Anfang)
  gtk_widget_set_halign(peak_btn, GTK_ALIGN_START);
  gtk_widget_set_valign(peak_btn, GTK_ALIGN_CENTER);
  peak_btn_signal_id = g_signal_connect(peak_btn, "toggled", G_CALLBACK(toggle_cb), &pan_peak_hold_enabled);
  //-------------------------------------------------------------------------------------------
  pan_label = gtk_label_new("Pan");
  WEAKEN(pan_label);
  gtk_widget_set_name(pan_label, "boldlabel_border_blue");
  // Label breiter erzwingen
  gtk_widget_set_size_request(pan_label, 90, -1);
  gtk_widget_set_margin_top(pan_label, 5);
  gtk_widget_set_margin_bottom(pan_label, 5);
  gtk_widget_set_margin_start(pan_label, 5);    // linker Rand (Anfang)
  gtk_widget_set_margin_end(pan_label, 0);  // rechter Rand (Ende)
  gtk_widget_set_halign(pan_label, GTK_ALIGN_START);
  gtk_widget_set_valign(pan_label, GTK_ALIGN_CENTER);
  //-----------------------------------------------------------------------------------------------------------
  pan_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0,
                                       active_receiver->zoom == 1 ? active_receiver->width : active_receiver->width * (active_receiver->zoom - 1), 1.0);
  WEAKEN(pan_scale);
  gtk_widget_set_tooltip_text(pan_scale, "Move the spectrum left or right\nif Zoom > 1");
  gtk_widget_set_margin_end(pan_scale, 10); // rechter Rand (Ende)
  gtk_widget_set_hexpand(pan_scale, TRUE);
  gtk_widget_set_halign(pan_scale, GTK_ALIGN_FILL);
  gtk_scale_set_draw_value (GTK_SCALE(pan_scale), FALSE);
  gtk_range_set_increments (GTK_RANGE(pan_scale), 10.0, 10.0);
  gtk_range_set_value (GTK_RANGE(pan_scale), active_receiver->pan);
  pan_signal_id = g_signal_connect(G_OBJECT(pan_scale), "value_changed", G_CALLBACK(pan_value_changed_cb), NULL);

  if (active_receiver->zoom == 1) {
    gtk_widget_set_sensitive(pan_scale, FALSE);
  }

  // Widgets in Box packen
  gtk_box_pack_start(GTK_BOX(pan_box), peak_btn, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(pan_box), pan_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(pan_box), pan_scale, TRUE, TRUE, 0);
  // In Grid einhängen → 1 Spalte, volle Kontrolle über Breite via Box
  gtk_grid_attach(GTK_GRID(zoompan), pan_box, /* column */ 1, /* row */ 0, /* width */ 1, /* height */ 1);
  gtk_widget_show_all(pan_box);
  g_mutex_init(&pan_zoom_mutex);
  g_mutex_init(&peak_mutex);
  return zoompan;
}
