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
#if defined (__LDESK__)
  width = my_width - 50;
#else
  width = my_width;
#endif
  height = my_height;
  //t_print("%s: width=%d height=%d\n", __FUNCTION__,width,height);
  //
  // the horizontal layout changes a little if the total width changes
  //
  int twidth, swidth, bwidth;
  int t1pos, t2pos, t3pos;
  int s1pos, s2pos, s3pos, l_col;
  double l_scale_factor;
  int b1pos, b2pos, b3pos;
  int lbl_w_fix = width / 23; // Label_width fixed now
  slider_surface_scale = (slider_surface_scale < 1.0) ? 1.0 : (slider_surface_scale > 6.5 ? 6.5 : slider_surface_scale);
  l_scale_factor = slider_surface_scale;
  int sl_w_fix = width / l_scale_factor; // slider_width fixed now, default 5.0 if Linux and 4.1 if macOS
  t_print("%s: slider_surface_scale: %f l_scale_factor: %f\n", __FUNCTION__, slider_surface_scale, l_scale_factor);
  twidth = 2; // 2 Spalten,  Label
  swidth = 4; // 4 Spalten,  Slider
  bwidth = 1; // 1 Spalte,   Klick_Button if used
  t1pos  =  0;
  b1pos  =  t1pos + twidth;
  s1pos  =  b1pos + twidth;
  t2pos  =  s1pos + swidth;
  b2pos  =  t2pos + twidth;
  s2pos  =  b2pos + twidth;
  t3pos  =  s2pos + swidth;
  b3pos  =  t3pos + twidth;
  s3pos  =  b3pos + twidth;
  l_col  =  s3pos + swidth;
  int widget_height = 0;
  widget_height = height;
#ifdef __APPLE__
  int add_pixel = 400;
#else
  int add_pixel = 380;
#endif
  // some debug output for info
  t_print("%s: t1pos=%d s1pos=%d t2pos=%d s2pos=%d t3pos=%d s3pos=%d l_col=%d\n",
          __FUNCTION__, t1pos, s1pos, t2pos, s2pos, t3pos, s3pos, l_col);
  t_print("%s: max. slider surface column: %d\n", __FUNCTION__, s3pos + swidth);
  t_print("%s: twidth=%d swidth=%d bwidth=%d lbl_w_fix=%d sl_w_fix=%d\n",
          __FUNCTION__, twidth, swidth, bwidth, lbl_w_fix, sl_w_fix);
  zoompan = gtk_grid_new();
  gtk_widget_set_size_request (zoompan, width, height);
  gtk_grid_set_row_homogeneous(GTK_GRID(zoompan), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(zoompan), FALSE);
  gtk_widget_set_margin_top(zoompan, 0);    // Kein Abstand oben
  gtk_widget_set_margin_bottom(zoompan, 0); // Kein Abstand unten
  gtk_widget_set_margin_start(zoompan, 15);  // Kein Abstand am Anfang
  gtk_widget_set_margin_end(zoompan, 0);    // Kein Abstand am Ende
  //-----------------------------------------------------------------------------------------------------------
  zoom_label = gtk_label_new("Zoom");
  gtk_widget_set_size_request(zoom_label, 2 * widget_height - 3, widget_height - 15);
  gtk_widget_set_name(zoom_label, "boldlabel_border_blue");
  gtk_widget_set_margin_top(zoom_label, 5);
  gtk_widget_set_margin_bottom(zoom_label, 5);
  gtk_widget_set_margin_end(zoom_label, 5);    // rechter Rand (Ende)
  gtk_widget_set_halign(zoom_label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(zoom_label, GTK_ALIGN_CENTER);
  gtk_label_set_xalign(GTK_LABEL(zoom_label), 0.5);
  gtk_label_set_yalign(GTK_LABEL(zoom_label), 0.5);
  gtk_label_set_justify(GTK_LABEL(zoom_label), GTK_JUSTIFY_CENTER);
  gtk_grid_attach(GTK_GRID(zoompan), zoom_label, t1pos, 0, twidth, 1);
  gtk_widget_show(zoom_label);
  //-----------------------------------------------------------------------------------------------------------
  zoom_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, MAX_ZOOM, 1.00);
  gtk_widget_set_tooltip_text(zoom_scale, "Zoom into the Panadapter");
  gtk_widget_set_size_request(zoom_scale, sl_w_fix, height);
  gtk_widget_set_valign(zoom_scale, GTK_ALIGN_CENTER);
  gtk_widget_set_halign(zoom_scale, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(zoom_scale, FALSE);
  gtk_range_set_increments (GTK_RANGE(zoom_scale), 1.0, 1.0);
  gtk_widget_set_margin_end(zoom_scale, 0);    // rechter Rand (Ende)
  gtk_range_set_value (GTK_RANGE(zoom_scale), active_receiver->zoom);

  for (float i = 1.0; i <= 8.0; i += 1.0) {
    gtk_scale_add_mark(GTK_SCALE(zoom_scale), i, GTK_POS_TOP, NULL);
  }

  gtk_widget_show(zoom_scale);
  gtk_grid_attach(GTK_GRID(zoompan), zoom_scale, s1pos, 0, swidth, 1);
  zoom_signal_id = g_signal_connect(G_OBJECT(zoom_scale), "value_changed", G_CALLBACK(zoom_value_changed_cb), NULL);
  //-----------------------------------------------------------------------------------------------------------
  pan_label = gtk_label_new("Pan");
  gtk_widget_set_size_request(pan_label, 2 * widget_height - 18, widget_height - 15);
  gtk_widget_set_name(pan_label, "boldlabel_border_blue");
  gtk_widget_set_margin_top(pan_label, 5);
  gtk_widget_set_margin_bottom(pan_label, 5);
#ifdef __APPLE__
  gtk_widget_set_margin_start(pan_label, 0);  // linker Rand (Start)
#else
  gtk_widget_set_margin_start(pan_label, 0);  // linker Rand (Start)
#endif
  gtk_widget_set_margin_end(pan_label, 5);    // rechter Rand (Ende)
  gtk_widget_set_halign(pan_label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(pan_label, GTK_ALIGN_CENTER);
  gtk_label_set_xalign(GTK_LABEL(pan_label), 0.5);
  gtk_label_set_yalign(GTK_LABEL(pan_label), 0.5);
  gtk_label_set_justify(GTK_LABEL(pan_label), GTK_JUSTIFY_CENTER);
  gtk_grid_attach(GTK_GRID(zoompan), pan_label, b2pos, 0, twidth, 1);
  gtk_widget_show(pan_label);
  //-----------------------------------------------------------------------------------------------------------
  pan_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0,
                                       active_receiver->zoom == 1 ? active_receiver->width : active_receiver->width * (active_receiver->zoom - 1), 1.0);
  gtk_widget_set_size_request(pan_scale, sl_w_fix + add_pixel, height);
  gtk_widget_set_tooltip_text(pan_scale, "Move the spectrum left or right\nif Zoom > 1");
  gtk_widget_set_valign(pan_scale, GTK_ALIGN_CENTER);
  gtk_widget_set_halign(pan_scale, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(pan_scale, FALSE);
  gtk_scale_set_draw_value (GTK_SCALE(pan_scale), FALSE);
  gtk_range_set_increments (GTK_RANGE(pan_scale), 10.0, 10.0);
  gtk_range_set_value (GTK_RANGE(pan_scale), active_receiver->pan);
  gtk_widget_set_margin_end(pan_scale, 10);    // rechter Rand (Ende)
  gtk_widget_show(pan_scale);
  gtk_grid_attach(GTK_GRID(zoompan), pan_scale, s2pos, 0, swidth, 1);
  pan_signal_id = g_signal_connect(G_OBJECT(pan_scale), "value_changed", G_CALLBACK(pan_value_changed_cb), NULL);

  if (active_receiver->zoom == 1) {
    gtk_widget_set_sensitive(pan_scale, FALSE);
  }

  g_mutex_init(&pan_zoom_mutex);
  return zoompan;
}
