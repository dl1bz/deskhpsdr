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
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "new_menu.h"
#include "receiver.h"
#include "meter_menu.h"
#include "meter.h"
#include "radio.h"

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

static void smeter_select_cb (GtkToggleButton *widget, gpointer        data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
    active_receiver->smetermode = SMETER_PEAK;
    break;

  case 1:
    active_receiver->smetermode = SMETER_AVERAGE;
    break;
  }
}

static void analog_cb (GtkToggleButton *widget, gpointer        data) {
  analog_meter = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
}

static void alc_select_cb(GtkToggleButton *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
    transmitter->alcmode = ALC_PEAK;
    break;

  case 1:
    transmitter->alcmode = ALC_AVERAGE;
    break;

  case 2:
    transmitter->alcmode = ALC_GAIN;
    break;
  }
}

void meter_menu (GtkWidget *parent) {
  int box_width = 300;
  int widget_heigth = 50;
  GtkWidget *w;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[32];
  snprintf(_title, 32, "%s - Meter", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
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
  //---------------------------------------------------------------------------
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  gtk_widget_set_size_request(close_b, 90, -1);  // z.B. 100px
  gtk_widget_set_margin_top(close_b, 0);
  gtk_widget_set_margin_bottom(close_b, 0);
  gtk_widget_set_margin_start(close_b, 0);
  gtk_widget_set_margin_end(close_b, 0);    // rechter Rand (Ende)
  gtk_widget_set_halign(close_b, GTK_ALIGN_START);
  gtk_widget_set_valign(close_b, GTK_ALIGN_CENTER);
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_box_pack_start(GTK_BOX(box_Z0), close_b, FALSE, FALSE, 0);
  gtk_grid_attach(GTK_GRID(grid), box_Z0, 0, 0, 1, 1);
  //----------------------------------------------------------------------------------------------------------
  GtkWidget *box_Z1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(box_Z1, box_width, widget_heigth);
  gtk_box_set_spacing(GTK_BOX(box_Z1), 5);
  //----------------------------------------------------------------------------------------------------------
  w = gtk_label_new("Meter Type");
  gtk_widget_set_name(w, "boldlabel_border_black");
  gtk_widget_set_size_request(w, box_width * 2 / 3, -1);  // z.B. 100px
  gtk_widget_set_margin_top(w, 0);
  gtk_widget_set_margin_bottom(w, 0);
  gtk_widget_set_margin_end(w, 0);
  gtk_widget_set_margin_end(w, 0);    // rechter Rand (Ende)
  gtk_widget_set_halign(w, GTK_ALIGN_START);
  gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
  gtk_box_pack_start(GTK_BOX(box_Z1), w, FALSE, FALSE, 0);
  //---------------------------------------------------------------------------
  w = gtk_combo_box_text_new();
  // my_combo_attach(GTK_GRID(grid), w, 1, 1, 1, 1);
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, "Digital");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, "Analog");
  gtk_combo_box_set_active(GTK_COMBO_BOX(w), analog_meter ? 1 : 0);
  gtk_widget_set_size_request(w, box_width / 3, -1);  // z.B. 100px
  gtk_widget_set_margin_top(w, 0);
  gtk_widget_set_margin_bottom(w, 0);
  gtk_widget_set_margin_end(w, 0);
  gtk_widget_set_margin_end(w, 0);    // rechter Rand (Ende)
  gtk_widget_set_halign(w, GTK_ALIGN_START);
  gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
  g_signal_connect(w, "changed", G_CALLBACK(analog_cb), NULL);
  gtk_box_pack_start(GTK_BOX(box_Z1), w, FALSE, FALSE, 0);
  gtk_grid_attach(GTK_GRID(grid), box_Z1, 0, 1, 1, 1);
  //----------------------------------------------------------------------------------------------------------
  GtkWidget *box_Z2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
  gtk_widget_set_size_request(box_Z2, box_width, widget_heigth);
  gtk_box_set_spacing(GTK_BOX(box_Z2), 5);
  //----------------------------------------------------------------------------------------------------------
  w = gtk_label_new("S-Meter Reading");
  gtk_widget_set_name(w, "boldlabel_border_black");
  gtk_widget_set_size_request(w, box_width * 2 / 3, -1);  // z.B. 100px
  gtk_widget_set_margin_top(w, 0);
  gtk_widget_set_margin_bottom(w, 0);
  gtk_widget_set_margin_end(w, 0);
  gtk_widget_set_margin_end(w, 0);    // rechter Rand (Ende)
  gtk_widget_set_halign(w, GTK_ALIGN_START);
  gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
  gtk_box_pack_start(GTK_BOX(box_Z2), w, FALSE, FALSE, 0);
  // gtk_grid_attach(GTK_GRID(grid), w, 0, 2, 1, 1);
  //---------------------------------------------------------------------------
  w = gtk_combo_box_text_new();
  // my_combo_attach(GTK_GRID(grid), w, 1, 2, 1, 1);
  gtk_widget_set_size_request(w, box_width / 3, -1);  // z.B. 100px
  gtk_widget_set_margin_top(w, 0);
  gtk_widget_set_margin_bottom(w, 0);
  gtk_widget_set_margin_end(w, 0);
  gtk_widget_set_margin_end(w, 0);    // rechter Rand (Ende)
  gtk_widget_set_halign(w, GTK_ALIGN_START);
  gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, "Peak");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, "Average");

  switch (active_receiver->smetermode) {
  case SMETER_PEAK:
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);
    break;

  case SMETER_AVERAGE:
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), 1);
    break;
  }

  g_signal_connect(w, "changed", G_CALLBACK(smeter_select_cb), NULL);
  gtk_box_pack_start(GTK_BOX(box_Z2), w, FALSE, FALSE, 0);
  gtk_grid_attach(GTK_GRID(grid), box_Z2, 0, 2, 1, 1);
  //---------------------------------------------------------------------------

  if (can_transmit) {
    //----------------------------------------------------------------------------------------------------------
    GtkWidget *box_Z3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);   // 3px Abstand zwischen Label & Slider
    gtk_widget_set_size_request(box_Z3, box_width, widget_heigth);
    gtk_box_set_spacing(GTK_BOX(box_Z3), 5);
    //----------------------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------
    w = gtk_label_new("TX ALC Reading");
    gtk_widget_set_name(w, "boldlabel_border_black");
    gtk_widget_set_size_request(w, box_width * 2 / 3, -1);  // z.B. 100px
    gtk_widget_set_margin_top(w, 0);
    gtk_widget_set_margin_bottom(w, 0);
    gtk_widget_set_margin_end(w, 0);
    gtk_widget_set_margin_end(w, 0);    // rechter Rand (Ende)
    gtk_widget_set_halign(w, GTK_ALIGN_START);
    gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box_Z3), w, FALSE, FALSE, 0);
    // gtk_grid_attach(GTK_GRID(grid), w, 0, 3, 1, 1);
    //---------------------------------------------------------------------------
    w = gtk_combo_box_text_new();
    // my_combo_attach(GTK_GRID(grid), w, 1, 3, 1, 1);
    gtk_widget_set_size_request(w, box_width / 3, -1);  // z.B. 100px
    gtk_widget_set_margin_top(w, 0);
    gtk_widget_set_margin_bottom(w, 0);
    gtk_widget_set_margin_end(w, 0);
    gtk_widget_set_margin_end(w, 0);    // rechter Rand (Ende)
    gtk_widget_set_halign(w, GTK_ALIGN_START);
    gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, "Peak");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, "Average");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), NULL, "Gain");

    switch (transmitter->alcmode) {
    case ALC_PEAK:
      gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);
      break;

    case ALC_AVERAGE:
      gtk_combo_box_set_active(GTK_COMBO_BOX(w), 1);
      break;

    case ALC_GAIN:
      gtk_combo_box_set_active(GTK_COMBO_BOX(w), 2);
      break;
    }

    g_signal_connect(w, "changed", G_CALLBACK(alc_select_cb), NULL);
    gtk_box_pack_start(GTK_BOX(box_Z3), w, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), box_Z3, 0, 3, 1, 1);
    //---------------------------------------------------------------------------
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
