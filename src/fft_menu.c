/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
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

#include "new_menu.h"
#include "fft_menu.h"
#include "radio.h"
#include "message.h"

static GtkWidget *dialog = NULL;

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

static void binaural_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  RECEIVER *rx = receiver[id];
  rx->binaural = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  rx_set_af_binaural(rx);
}

static void filter_type_cb(GtkToggleButton *widget, gpointer data) {
  int type = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  int channel  = GPOINTER_TO_INT(data);

  switch (channel) {
  case 0:
  case 1:
    receiver[channel]->low_latency = type;
    rx_set_fft_latency(receiver[channel]);
    break;

  case 8:
    // NOTREACHED
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
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  #if defined (__LDESK__)
  char _title[32];
  snprintf(_title, 32, "%s - DSP", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  #else
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - DSP");
  #endif
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  w = gtk_button_new_with_label("Close");
  g_signal_connect (w, "button_press_event", G_CALLBACK(close_cb), NULL);
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
      ftype = 0;
    }

    gtk_grid_attach(GTK_GRID(grid), w, col, 1, 1, 1);

    if (chan == 8) {
      //
      // To enable CESSB overshoot correction with TX compression, we cannot
      // allow low latency filters for TX
      //
      w = gtk_label_new("Linear Phase");
      gtk_widget_set_name(w, "boldlabel");
      gtk_grid_attach(GTK_GRID(grid), w, col, 2, 1, 1);
    } else {
      w = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, "Linear Phase");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, "Low Latency");
      gtk_combo_box_set_active(GTK_COMBO_BOX(w), ftype);
      my_combo_attach(GTK_GRID(grid), w, col, 2, 1, 1);
      g_signal_connect(w, "changed", G_CALLBACK(filter_type_cb), GINT_TO_POINTER(chan));
    }

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
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), receiver[i]->binaural);
      gtk_grid_attach(GTK_GRID(grid), w, col, 4, 1, 1);
      g_signal_connect(w, "toggled", G_CALLBACK(binaural_cb), GINT_TO_POINTER(chan));
    }

    col++;
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}

