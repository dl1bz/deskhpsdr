/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#include "main.h"
#include "new_menu.h"
#include "display_menu.h"
#include "radio.h"

static GtkWidget *dialog = NULL;
static GtkWidget *waterfall_high_r = NULL;
static GtkWidget *waterfall_low_r = NULL;

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

static void detector_cb(GtkToggleButton *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
    active_receiver->display_detector_mode = DET_PEAK;
    break;

  case 1:
    active_receiver->display_detector_mode = DET_ROSENFELL;
    break;

  case 2:
    active_receiver->display_detector_mode = DET_AVERAGE;
    break;

  case 3:
    active_receiver->display_detector_mode = DET_SAMPLEHOLD;
    break;
  }

  rx_set_detector(active_receiver);
}

static void average_cb(GtkToggleButton *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
    active_receiver->display_average_mode = AVG_NONE;
    break;

  case 1:
    active_receiver->display_average_mode = AVG_RECURSIVE;
    break;

  case 2:
    active_receiver->display_average_mode = AVG_TIMEWINDOW;
    break;

  case 3:
    active_receiver->display_average_mode = AVG_LOGRECURSIVE;
    break;
  }

  rx_set_average(active_receiver);
}

static void time_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->display_average_time = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_average(active_receiver);
}

static void filled_cb(GtkWidget *widget, gpointer data) {
  active_receiver->display_filled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void gradient_cb(GtkWidget *widget, gpointer data) {
  active_receiver->display_gradient = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void frames_per_second_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  rx_set_framerate(active_receiver);
}

static void panadapter_high_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->panadapter_high = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void panadapter_low_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->panadapter_low = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void panadapter_step_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->panadapter_step = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void waterfall_high_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->waterfall_high = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void waterfall_low_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->waterfall_low = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void waterfall_automatic_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  active_receiver->waterfall_automatic = val;
  gtk_widget_set_sensitive(waterfall_high_r, !val);
  gtk_widget_set_sensitive(waterfall_low_r, !val);
}

static void display_waterfall_cb(GtkWidget *widget, gpointer data) {
  active_receiver->display_waterfall = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_reconfigure();
}

static void display_panadapter_cb(GtkWidget *widget, gpointer data) {
  active_receiver->display_panadapter = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_reconfigure();
}

void display_menu(GtkWidget *parent) {
  GtkWidget *label;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  #if defined (__LDESK__)
  char _title[32];
  snprintf(_title, 32, "%s - Display", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  #else
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Display");
  #endif
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  int col = 0;
  int row = 0;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, col, row, 1, 1);
  row++;
  col = 0;
  label = gtk_label_new("Frames Per Second:");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *frames_per_second_r = gtk_spin_button_new_with_range(1.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(frames_per_second_r), (double)active_receiver->fps);
  gtk_widget_show(frames_per_second_r);
  gtk_grid_attach(GTK_GRID(grid), frames_per_second_r, col, row, 1, 1);
  g_signal_connect(frames_per_second_r, "value_changed", G_CALLBACK(frames_per_second_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter High:");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_high_r = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_high_r), (double)active_receiver->panadapter_high);
  gtk_widget_show(panadapter_high_r);
  gtk_grid_attach(GTK_GRID(grid), panadapter_high_r, col, row, 1, 1);
  g_signal_connect(panadapter_high_r, "value_changed", G_CALLBACK(panadapter_high_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Low:");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_low_r = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_low_r), (double)active_receiver->panadapter_low);
  gtk_widget_show(panadapter_low_r);
  gtk_grid_attach(GTK_GRID(grid), panadapter_low_r, col, row, 1, 1);
  g_signal_connect(panadapter_low_r, "value_changed", G_CALLBACK(panadapter_low_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Panadapter Step:");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *panadapter_step_r = gtk_spin_button_new_with_range(1.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(panadapter_step_r), (double)active_receiver->panadapter_step);
  gtk_widget_show(panadapter_step_r);
  gtk_grid_attach(GTK_GRID(grid), panadapter_step_r, col, row, 1, 1);
  g_signal_connect(panadapter_step_r, "value_changed", G_CALLBACK(panadapter_step_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Waterfall High:");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  waterfall_high_r = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(waterfall_high_r), (double)active_receiver->waterfall_high);
  gtk_widget_show(waterfall_high_r);
  gtk_grid_attach(GTK_GRID(grid), waterfall_high_r, col, row, 1, 1);
  g_signal_connect(waterfall_high_r, "value_changed", G_CALLBACK(waterfall_high_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Waterfall Low:");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  waterfall_low_r = gtk_spin_button_new_with_range(-220.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(waterfall_low_r), (double)active_receiver->waterfall_low);
  gtk_widget_show(waterfall_low_r);
  gtk_grid_attach(GTK_GRID(grid), waterfall_low_r, col, row, 1, 1);
  g_signal_connect(waterfall_low_r, "value_changed", G_CALLBACK(waterfall_low_value_changed_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("Waterfall Automatic:");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkWidget *waterfall_automatic_b = gtk_check_button_new();
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (waterfall_automatic_b), active_receiver->waterfall_automatic);
  gtk_widget_show(waterfall_automatic_b);
  gtk_grid_attach(GTK_GRID(grid), waterfall_automatic_b, col, row, 1, 1);
  g_signal_connect(waterfall_automatic_b, "toggled", G_CALLBACK(waterfall_automatic_cb), NULL);
  col = 2;
  row = 1;
  label = gtk_label_new("Detector:");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  GtkWidget *detector_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(detector_combo), NULL, "Peak");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(detector_combo), NULL, "Rosenfell");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(detector_combo), NULL, "Average");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(detector_combo), NULL, "Sample");

  switch (active_receiver->display_detector_mode) {
  case DET_PEAK:
    gtk_combo_box_set_active(GTK_COMBO_BOX(detector_combo), 0);
    break;

  case DET_ROSENFELL:
    gtk_combo_box_set_active(GTK_COMBO_BOX(detector_combo), 1);
    break;

  case DET_AVERAGE:
    gtk_combo_box_set_active(GTK_COMBO_BOX(detector_combo), 2);
    break;

  case DET_SAMPLEHOLD:
    gtk_combo_box_set_active(GTK_COMBO_BOX(detector_combo), 3);
    break;
  }

  my_combo_attach(GTK_GRID(grid), detector_combo, col + 1, row, 1, 1);
  g_signal_connect(detector_combo, "changed", G_CALLBACK(detector_cb), NULL);
  row++;
  label = gtk_label_new("Averaging: ");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  GtkWidget *average_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(average_combo), NULL, "None");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(average_combo), NULL, "Recursive");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(average_combo), NULL, "Time Window");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(average_combo), NULL, "Log Recursive");

  switch (active_receiver->display_average_mode) {
  case AVG_NONE:
    gtk_combo_box_set_active(GTK_COMBO_BOX(average_combo), 0);
    break;

  case AVG_RECURSIVE:
    gtk_combo_box_set_active(GTK_COMBO_BOX(average_combo), 1);
    break;

  case AVG_TIMEWINDOW:
    gtk_combo_box_set_active(GTK_COMBO_BOX(average_combo), 2);
    break;

  case AVG_LOGRECURSIVE:
    gtk_combo_box_set_active(GTK_COMBO_BOX(average_combo), 3);
    break;
  }

  my_combo_attach(GTK_GRID(grid), average_combo, col + 1, row, 1, 1);
  g_signal_connect(average_combo, "changed", G_CALLBACK(average_cb), NULL);
  row++;
  label = gtk_label_new("Av. Time (ms):");
  gtk_widget_set_name (label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  GtkWidget *time_r = gtk_spin_button_new_with_range(1.0, 9999.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_r), (double)active_receiver->display_average_time);
  gtk_widget_show(time_r);
  gtk_grid_attach(GTK_GRID(grid), time_r, col + 1, row, 1, 1);
  g_signal_connect(time_r, "value_changed", G_CALLBACK(time_value_changed_cb), NULL);
  row++;
  GtkWidget *filled_b = gtk_check_button_new_with_label("Fill Panadapter");
  gtk_widget_set_name (filled_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (filled_b), active_receiver->display_filled);
  gtk_widget_show(filled_b);
  gtk_grid_attach(GTK_GRID(grid), filled_b, col, row, 1, 1);
  g_signal_connect(filled_b, "toggled", G_CALLBACK(filled_cb), NULL);
  GtkWidget *gradient_b = gtk_check_button_new_with_label("Gradient");
  gtk_widget_set_name (gradient_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gradient_b), active_receiver->display_gradient);
  gtk_widget_show(gradient_b);
  gtk_grid_attach(GTK_GRID(grid), gradient_b, col + 1, row, 1, 1);
  g_signal_connect(gradient_b, "toggled", G_CALLBACK(gradient_cb), NULL);
  row++;
  GtkWidget *b_display_panadapter = gtk_check_button_new_with_label("Display Panadapter");
  gtk_widget_set_name (b_display_panadapter, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_panadapter), active_receiver->display_panadapter);
  gtk_widget_show(b_display_panadapter);
  gtk_grid_attach(GTK_GRID(grid), b_display_panadapter, col, row, 1, 1);
  g_signal_connect(b_display_panadapter, "toggled", G_CALLBACK(display_panadapter_cb), NULL);
  GtkWidget *b_display_waterfall = gtk_check_button_new_with_label("Display Waterfall");
  gtk_widget_set_name (b_display_waterfall, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_waterfall), active_receiver->display_waterfall);
  gtk_widget_show(b_display_waterfall);
  gtk_grid_attach(GTK_GRID(grid), b_display_waterfall, col + 1, row, 1, 1);
  g_signal_connect(b_display_waterfall, "toggled", G_CALLBACK(display_waterfall_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;

  if (active_receiver->waterfall_automatic) {
    gtk_widget_set_sensitive(waterfall_high_r, FALSE);
    gtk_widget_set_sensitive(waterfall_low_r,  FALSE);
  }

  gtk_widget_show_all(dialog);
}
