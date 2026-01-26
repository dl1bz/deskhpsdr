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
#include <glib/gprintf.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "new_menu.h"
#include "agc_menu.h"
#include "agc.h"
#include "band.h"
#include "channel.h"
#include "radio.h"
#include "receiver.h"
#include "vfo.h"
#include "toolbar.h"
#include "actions.h"
#include "action_dialog.h"
#include "gpio.h"
#include "i2c.h"

void switch_menu(GtkWidget *parent);

static GtkWidget *dialog = NULL;

static void cleanup(void){
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean default_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
  gpio_default_switch_actions(controller);
  cleanup();
  switch_menu(top_window);
  return TRUE;
}

static gboolean close_cb(void){
  cleanup();
  return TRUE;
}

static gboolean switch_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
  int sw = GPOINTER_TO_INT(data);
  int action = action_dialog(dialog, CONTROLLER_SWITCH, switches[sw].switch_function);
  gtk_button_set_label(GTK_BUTTON(widget), ActionTable[action].str);
  switches[sw].switch_function = action;
  update_toolbar_labels();
  return TRUE;
}

void switch_menu(GtkWidget *parent) {
  GtkWidget *grid;
  GtkWidget *widget;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[32];
  snprintf(_title, 32, "%s - Switch Actions", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 0);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 0);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  GtkWidget *default_b = gtk_button_new_with_label("Defaults");
  gtk_widget_set_name(default_b, "close_button"); // same looks as Close button
  g_signal_connect (default_b, "button-press-event", G_CALLBACK(default_cb), NULL);

  switch (controller) {
  case CONTROLLER2_V1:
  case CONTROLLER2_V2:
    gtk_grid_attach(GTK_GRID(grid), default_b, 7, 0, 2, 1);
    break;

  case G2_FRONTPANEL:
    gtk_grid_attach(GTK_GRID(grid), default_b, 6, 0, 3, 1);
    break;
  }

  if (controller == CONTROLLER2_V1 || controller == CONTROLLER2_V2) {
    // 7 horizontal switches in row 8
    for (int i = 0; i < 7; i++ ) {
      widget = gtk_button_new_with_label(ActionTable[switches[i].switch_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(i));
      gtk_grid_attach(GTK_GRID(grid), widget, i, 8, 1, 1);
    }

    // vertical padding in row 1 and 7
    widget = gtk_label_new("  ");
    gtk_widget_set_size_request(widget, 0, 10);
    gtk_grid_attach(GTK_GRID(grid), widget, 8, 1, 1, 1);
    widget = gtk_label_new("  ");
    gtk_widget_set_size_request(widget, 0, 10);
    gtk_grid_attach(GTK_GRID(grid), widget, 8, 7, 1, 1);
    // one switch in row 2, then pairs of switches in row 3-6
    int row = 2;
    widget = gtk_button_new_with_label(ActionTable[switches[7].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(7));
    gtk_grid_attach(GTK_GRID(grid), widget, 8, row, 1, 1);
    row++;
    widget = gtk_button_new_with_label(ActionTable[switches[8].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(8));
    gtk_grid_attach(GTK_GRID(grid), widget, 7, row, 1, 1);
    widget = gtk_button_new_with_label(ActionTable[switches[9].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(9));
    gtk_grid_attach(GTK_GRID(grid), widget, 8, row, 1, 1);
    row++;
    widget = gtk_button_new_with_label(ActionTable[switches[10].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(10));
    gtk_grid_attach(GTK_GRID(grid), widget, 7, row, 1, 1);
    widget = gtk_button_new_with_label(ActionTable[switches[11].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(11));
    gtk_grid_attach(GTK_GRID(grid), widget, 8, row, 1, 1);
    row++;
    widget = gtk_button_new_with_label(ActionTable[switches[12].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(12));
    gtk_grid_attach(GTK_GRID(grid), widget, 7, row, 1, 1);
    widget = gtk_button_new_with_label(ActionTable[switches[13].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(13));
    gtk_grid_attach(GTK_GRID(grid), widget, 8, row, 1, 1);
    row++;
    widget = gtk_button_new_with_label(ActionTable[switches[14].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(14));
    gtk_grid_attach(GTK_GRID(grid), widget, 7, row, 1, 1);
    widget = gtk_button_new_with_label(ActionTable[switches[15].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(15));
    gtk_grid_attach(GTK_GRID(grid), widget, 8, row, 1, 1);
    gtk_container_add(GTK_CONTAINER(content), grid);
  }

  if (controller == G2_FRONTPANEL) {
    // vertical padding in row 1 and 6
    widget = gtk_label_new("  ");
    gtk_widget_set_size_request(widget, 0, 10);
    gtk_grid_attach(GTK_GRID(grid), widget, 8, 1, 1, 1);
    widget = gtk_label_new("  ");
    gtk_widget_set_size_request(widget, 0, 20);
    gtk_grid_attach(GTK_GRID(grid), widget, 8, 6, 1, 1);

    // horizontal padding in columns 1-5
    for (int i = 1; i < 6; i++) {
      widget = gtk_label_new("  ");
      gtk_grid_attach(GTK_GRID(grid), widget, i, 1, 1, 1);
    }

    widget = gtk_button_new_with_label(ActionTable[switches[11].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(11));
    gtk_grid_attach(GTK_GRID(grid), widget, 0, 4, 1, 1);
    widget = gtk_button_new_with_label(ActionTable[switches[13].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(13));
    gtk_grid_attach(GTK_GRID(grid), widget, 0, 5, 1, 1);
    int col = 6;

    for (int i = 10; i > 7; i--) {
      widget = gtk_button_new_with_label(ActionTable[switches[i].switch_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(i));
      gtk_grid_attach(GTK_GRID(grid), widget, col, 2, 1, 1);
      col++;
    }

    col = 6;
    int a[3] = {7, 15, 14};

    for (int i = 0; i < 3; i++) {
      widget = gtk_button_new_with_label(ActionTable[switches[a[i]].switch_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(a[i]));
      gtk_grid_attach(GTK_GRID(grid), widget, col, 3, 1, 1);
      col++;
    }

    col = 6;
    int b[3] = {6, 5, 3};

    for (int i = 0; i < 3; i++) {
      widget = gtk_button_new_with_label(ActionTable[switches[b[i]].switch_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(b[i]));
      gtk_grid_attach(GTK_GRID(grid), widget, col, 4, 1, 1);
      col++;
    }

    col = 6;

    for (int i = 2; i > -1; i--) {
      widget = gtk_button_new_with_label(ActionTable[switches[i].switch_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(i));
      gtk_grid_attach(GTK_GRID(grid), widget, col, 5, 1, 1);
      col++;
    }

    widget = gtk_button_new_with_label(ActionTable[switches[12].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(12));
    gtk_grid_attach(GTK_GRID(grid), widget, 6, 7, 1, 1);
    widget = gtk_button_new_with_label(ActionTable[switches[4].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), GINT_TO_POINTER(4));
    gtk_grid_attach(GTK_GRID(grid), widget, 8, 7, 1, 1);
    gtk_container_add(GTK_CONTAINER(content), grid);
  }

  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
