/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT
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
#include "actions.h"
#include "action_dialog.h"
#include "gpio.h"
#include "i2c.h"

void encoder_menu(GtkWidget *parent);

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

static gboolean default_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
  gpio_default_encoder_actions(controller);
  cleanup();
  encoder_menu(top_window);
  return TRUE;
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

static gboolean encoder_bottom_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
  int en = GPOINTER_TO_INT(data);
  int action = action_dialog(dialog, CONTROLLER_ENCODER, encoders[en].bottom_encoder_function);
  gtk_button_set_label(GTK_BUTTON(widget), ActionTable[action].str);
  encoders[en].bottom_encoder_function = action;
  return TRUE;
}

static gboolean encoder_top_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
  int en = GPOINTER_TO_INT(data);
  int action = action_dialog(dialog, CONTROLLER_ENCODER, encoders[en].top_encoder_function);
  gtk_button_set_label(GTK_BUTTON(widget), ActionTable[action].str);
  encoders[en].top_encoder_function = action;
  return TRUE;
}

static gboolean encoder_switch_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
  int en = GPOINTER_TO_INT(data);
  int action = action_dialog(dialog, CONTROLLER_SWITCH, encoders[en].switch_function);
  gtk_button_set_label(GTK_BUTTON(widget), ActionTable[action].str);
  encoders[en].switch_function = action;
  return TRUE;
}

void encoder_menu(GtkWidget *parent) {
  int row = 0;
  int col = 0;
  GtkWidget *widget;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  char title[64];

  switch (controller) {
  case NO_CONTROLLER:
    snprintf(title, 64, "%s - No Encoders", PGNAME);
    break;

  case CONTROLLER1:
    snprintf(title, 64, "%s - Controller 1 Encoder Actions", PGNAME);
    break;

  case CONTROLLER2_V1:
    snprintf(title, 64, "%s - Controller 2 V1 Encoder Actions", PGNAME);
    break;

  case CONTROLLER2_V2:
    snprintf(title, 64, "%s - Controller 2 V2 Encoder Actions", PGNAME);
    break;

  case G2_FRONTPANEL:
    snprintf(title, 64, "%s - G2 FrontPanel Encoder Actions", PGNAME);
    break;
  }

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
  gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  GtkWidget *default_b = gtk_button_new_with_label("Defaults");
  gtk_widget_set_name(default_b, "close_button"); // same looks as Close button
  g_signal_connect (default_b, "button-press-event", G_CALLBACK(default_cb), NULL);

  switch (controller) {
  case CONTROLLER1:
    gtk_grid_attach(GTK_GRID(grid), default_b, 5, 0, 1, 1);
    break;

  case CONTROLLER2_V1:
  case CONTROLLER2_V2:
    gtk_grid_attach(GTK_GRID(grid), default_b, 6, 0, 2, 1);
    break;

  case G2_FRONTPANEL:
    gtk_grid_attach(GTK_GRID(grid), default_b, 3, 0, 2, 1);
    break;
  }

  switch (controller) {
  case NO_CONTROLLER:
    // should never happen
    break;

  case CONTROLLER1:
    // 3 vertical single encoders with switches plus VFO
    row = 1;

    for (int i = 0; i < 3; i++) {
      // vertical padding
      widget = gtk_label_new("  ");
      gtk_widget_set_size_request(widget, 0, 10);
      gtk_grid_attach(GTK_GRID(grid), widget, 4, row, 1, 1);
      row++;
      widget = gtk_label_new("Switch");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, 4, row, 1, 1);
      widget = gtk_button_new_with_label(ActionTable[encoders[i].switch_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_switch_cb), GINT_TO_POINTER(i));
      gtk_grid_attach(GTK_GRID(grid), widget, 5, row, 1, 1);
      row++;
      widget = gtk_label_new("Encoder");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, 4, row, 1, 1);
      widget = gtk_button_new_with_label(ActionTable[encoders[i].bottom_encoder_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_bottom_cb), GINT_TO_POINTER(i));
      gtk_grid_attach(GTK_GRID(grid), widget, 5, row, 1, 1);
      row++;
    }

    break;

  case CONTROLLER2_V1:
    // vertical padding
    widget = gtk_label_new("  ");
    gtk_widget_set_size_request(widget, 0, 10);
    gtk_grid_attach(GTK_GRID(grid), widget, 0, 1, 1, 1);
    // 3 horizontal single encoders with switches
    col = 0;

    for (int i = 0; i < 3; i++) {
      widget = gtk_label_new("Switch");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, col, 4, 1, 1);
      col++;
      widget = gtk_button_new_with_label(ActionTable[encoders[i].switch_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_switch_cb), GINT_TO_POINTER(i));
      gtk_grid_attach(GTK_GRID(grid), widget, col, 4, 1, 1);
      col++;
    }

    col = 0;

    for (int i = 0; i < 3; i++) {
      widget = gtk_label_new("Encoder");
      gtk_widget_set_halign(widget, GTK_ALIGN_END);
      gtk_widget_set_name(widget, "boldlabel");
      gtk_grid_attach(GTK_GRID(grid), widget, col, 5, 1, 1);
      col++;
      widget = gtk_button_new_with_label(ActionTable[encoders[i].bottom_encoder_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_bottom_cb), GINT_TO_POINTER(i));
      gtk_grid_attach(GTK_GRID(grid), widget, col, 5, 1, 1);
      col++;
    }

    // 1 vertical single encoder with switch
    widget = gtk_label_new("Switch");
    gtk_widget_set_halign(widget, GTK_ALIGN_END);
    gtk_widget_set_name(widget, "boldlabel");
    gtk_grid_attach(GTK_GRID(grid), widget, 6, 2, 1, 1);
    widget = gtk_button_new_with_label(ActionTable[encoders[3].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_switch_cb), GINT_TO_POINTER(3));
    gtk_grid_attach(GTK_GRID(grid), widget, 7, 2, 1, 1);
    widget = gtk_label_new("Encoder");
    gtk_widget_set_name(widget, "boldlabel");
    gtk_widget_set_halign(widget, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), widget, 6, 3, 1, 1);
    widget = gtk_button_new_with_label(ActionTable[encoders[3].bottom_encoder_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_bottom_cb), GINT_TO_POINTER(3));
    gtk_grid_attach(GTK_GRID(grid), widget, 7, 3, 1, 1);
    break;

  case CONTROLLER2_V2:
    // vertical padding
    widget = gtk_label_new("  ");
    gtk_widget_set_size_request(widget, 0, 10);
    gtk_grid_attach(GTK_GRID(grid), widget, 0, 1, 1, 1);
    // 3 horizontal double encoders with switches
    row = 6;
    col = 0;

    for (int i = 0; i < 3; i++) {
      widget = gtk_label_new("Switch");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
      col++;
      widget = gtk_button_new_with_label(ActionTable[encoders[i].switch_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_switch_cb), GINT_TO_POINTER(i));
      gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
      col++;
    }

    row++;
    col = 0;

    for (int i = 0; i < 3; i++) {
      widget = gtk_label_new("Top");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
      col++;
      widget = gtk_button_new_with_label(ActionTable[encoders[i].top_encoder_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_top_cb), GINT_TO_POINTER(i));
      gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
      col++;
    }

    row++;
    col = 0;

    for (int i = 0; i < 3; i++) {
      widget = gtk_label_new("Bottom");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
      col++;
      widget = gtk_button_new_with_label(ActionTable[encoders[i].bottom_encoder_function].str);
      gtk_widget_set_name(widget, "small_button_with_border");
      g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_bottom_cb), GINT_TO_POINTER(i));
      gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
      col++;
    }

    row = 2;
    col = 6;
    widget = gtk_label_new("Switch");
    gtk_widget_set_name(widget, "boldlabel");
    gtk_widget_set_halign(widget, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
    col++;
    widget = gtk_button_new_with_label(ActionTable[encoders[3].switch_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_switch_cb), GINT_TO_POINTER(3));
    gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
    row++;
    col = 6;
    widget = gtk_label_new("Top");
    gtk_widget_set_name(widget, "boldlabel");
    gtk_widget_set_halign(widget, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
    col++;
    widget = gtk_button_new_with_label(ActionTable[encoders[3].top_encoder_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_top_cb), GINT_TO_POINTER(3));
    gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
    row++;
    col = 6;
    widget = gtk_label_new("Bottom");
    gtk_widget_set_halign(widget, GTK_ALIGN_END);
    gtk_widget_set_name(widget, "boldlabel");
    gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
    col++;
    widget = gtk_button_new_with_label(ActionTable[encoders[3].bottom_encoder_function].str);
    gtk_widget_set_name(widget, "small_button_with_border");
    g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_bottom_cb), GINT_TO_POINTER(3));
    gtk_grid_attach(GTK_GRID(grid), widget, col, row, 1, 1);
    break;

  case G2_FRONTPANEL:
    // 2 vertical double encoders with switch, both on the left and on the right side
    // variable i = 0,1 for left, right
    widget = gtk_label_new("Left edge Encoders");
    gtk_widget_set_name(widget, "boldlabel");
    gtk_widget_set_size_request(widget, 0, 30);
    gtk_widget_set_halign(widget, GTK_ALIGN_START);
    gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), widget, 0, 1, 2, 1);
    widget = gtk_label_new("Right edge Encoders");
    gtk_widget_set_name(widget, "boldlabel");
    gtk_widget_set_size_request(widget, 0, 30);
    gtk_widget_set_halign(widget, GTK_ALIGN_END);
    gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), widget, 3, 1, 2, 1);

    for (int i = 0; i < 2; i++) {
      row = 4;
      widget = gtk_label_new("Switch");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, i == 0 ? GTK_ALIGN_START : GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, 2 * i + 1, row++, 1, 1);
      widget = gtk_label_new("Top");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, i == 0 ? GTK_ALIGN_START : GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, 2 * i + 1, row++, 1, 1);
      widget = gtk_label_new("Bottom");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, i == 0 ? GTK_ALIGN_START : GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, 2 * i + 1, row++, 1, 1);
      widget = gtk_label_new("  ");
      gtk_grid_attach(GTK_GRID(grid), widget, 2 * i + 1, row++, 1, 1);
      widget = gtk_label_new("Switch");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, i == 0 ? GTK_ALIGN_START : GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, 2 * i + 1, row++, 1, 1);
      widget = gtk_label_new("Top");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, i == 0 ? GTK_ALIGN_START : GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, 2 * i + 1, row++, 1, 1);
      widget = gtk_label_new("Bottom");
      gtk_widget_set_name(widget, "boldlabel");
      gtk_widget_set_halign(widget, i == 0 ? GTK_ALIGN_START : GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), widget, 2 * i + 1, row++, 1, 1);
      // j counts the encoders, 0...3. Encoders 0,1 are left and 2,3 are right
      row = 4;

      for (int j = 2 * i; j < 2 * i + 2; j++) {
        widget = gtk_button_new_with_label(ActionTable[encoders[j].switch_function].str);
        gtk_widget_set_name(widget, "small_button_with_border");
        g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_switch_cb), GINT_TO_POINTER(j));
        gtk_grid_attach(GTK_GRID(grid), widget, 4 * i, row++, 1, 1);
        widget = gtk_button_new_with_label(ActionTable[encoders[j].top_encoder_function].str);
        gtk_widget_set_name(widget, "small_button_with_border");
        g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_top_cb), GINT_TO_POINTER(j));
        gtk_grid_attach(GTK_GRID(grid), widget, 4 * i, row++, 1, 1);
        widget = gtk_button_new_with_label(ActionTable[encoders[j].bottom_encoder_function].str);
        gtk_widget_set_name(widget, "small_button_with_border");
        g_signal_connect(widget, "button-press-event", G_CALLBACK(encoder_bottom_cb), GINT_TO_POINTER(j));
        gtk_grid_attach(GTK_GRID(grid), widget, 4 * i, row++, 1, 1);

        if  (j == 2 * i) {
          widget = gtk_label_new("  ");
          gtk_grid_attach(GTK_GRID(grid), widget, 4 * i, row++, 1, 1);
        }
      }
    }

    break;
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
