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
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "appearance.h"
#include "led.h"
#include "new_menu.h"
#include "radio.h"
#include "transmitter.h"
#include "vfo.h"
#include "vox_menu.h"
#include "vox.h"
#include "ext.h"
#include "message.h"

static GtkWidget *dialog = NULL;

static GtkWidget *level;

static GtkWidget *led;
static GdkRGBA led_color = {COLOUR_OK};
static GdkRGBA led_red  = {COLOUR_ALARM};
static GdkRGBA led_green = {COLOUR_OK};

static GThread *level_thread_id;
static int run_level = 0;
static double peak = 0.0;
static guint vox_timeout;
static int hold = 0;

static int vox_timeout_cb(gpointer data) {
  hold = 0;
  return FALSE;
}

static int level_update(void *data) {
  if (run_level) {
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(level), peak);

    if (peak > vox_threshold) {
      // red indicator
      led_color = led_red;
      led_set_color(led);

      if (hold == 0) {
        hold = 1;
      } else {
        g_source_remove(vox_timeout);
      }

      vox_timeout = g_timeout_add((int)vox_hang, vox_timeout_cb, NULL);
    } else {
      // green indicator
      if (hold == 0) {
        led_color = led_green;
        led_set_color(led);
      }
    }
  }

  return 0;
}

static gpointer level_thread(gpointer arg) {
  while (run_level) {
    peak = vox_get_peak();
    g_idle_add(level_update, NULL);
    usleep(100000); // 100ms
  }

  return NULL;
}

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

static gboolean enable_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  vox_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  g_idle_add(ext_vfo_update, GINT_TO_POINTER(0));
  return TRUE;
}

static void start_level_thread() {
  run_level = 1;
  level_thread_id = g_thread_new( "VOX level", level_thread, NULL);
  t_print("level_thread: id=%p\n", level_thread_id);
}

// cppcheck-suppress constParameterCallback
static void destroy_cb(GtkWidget *widget, gpointer data) {
  run_level = 0;
}

static void vox_value_changed_cb(GtkWidget *widget, gpointer data) {
  vox_threshold = gtk_range_get_value(GTK_RANGE(widget)) / 1000.0;
}

static void vox_hang_value_changed_cb(GtkWidget *widget, gpointer data) {
  vox_hang = gtk_range_get_value(GTK_RANGE(widget));
}

void vox_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  g_signal_connect (dialog, "destroy", G_CALLBACK(destroy_cb), NULL);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[32];
  snprintf(_title, 32, "%s - VOX", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  led = create_led(10, 10, &led_color);
  gtk_grid_attach(GTK_GRID(grid), led, 2, 0, 1, 1);
  GtkWidget *enable_b = gtk_check_button_new_with_label("VOX Enable");
  gtk_widget_set_name(enable_b, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enable_b), vox_enabled);
  g_signal_connect (enable_b, "toggled", G_CALLBACK(enable_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), enable_b, 3, 0, 1, 1);
  GtkWidget *level_label = gtk_label_new("Mic Level:");
  gtk_widget_set_name(level_label, "boldlabel");
  gtk_widget_set_halign(level_label, GTK_ALIGN_END);
  gtk_widget_show(level_label);
  gtk_grid_attach(GTK_GRID(grid), level_label, 0, 1, 1, 1);
  level = gtk_progress_bar_new();
  gtk_widget_show(level);
  gtk_grid_attach(GTK_GRID(grid), level, 1, 1, 3, 1);
  gtk_widget_set_valign(level, GTK_ALIGN_CENTER);
  GtkWidget *threshold_label = gtk_label_new("VOX Threshold:");
  gtk_widget_set_name(threshold_label, "boldlabel");
  gtk_widget_set_halign(threshold_label, GTK_ALIGN_END);
  gtk_widget_show(threshold_label);
  gtk_grid_attach(GTK_GRID(grid), threshold_label, 0, 2, 1, 1);
  GtkWidget *vox_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1000.0, 1.0);
  gtk_widget_set_valign(vox_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(vox_scale), 1.0, 1.0);
  gtk_range_set_value(GTK_RANGE(vox_scale), vox_threshold * 1000.0);
  gtk_widget_show(vox_scale);
  gtk_grid_attach(GTK_GRID(grid), vox_scale, 1, 2, 3, 1);
  g_signal_connect(G_OBJECT(vox_scale), "value_changed", G_CALLBACK(vox_value_changed_cb), NULL);
  GtkWidget *hang_label = gtk_label_new("VOX Hang (ms):");
  gtk_widget_set_name(hang_label, "boldlabel");
  gtk_widget_set_halign(hang_label, GTK_ALIGN_END);
  gtk_widget_show(hang_label);
  gtk_grid_attach(GTK_GRID(grid), hang_label, 0, 4, 1, 1);
  GtkWidget *vox_hang_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1000.0, 1.0);
  gtk_widget_set_valign(vox_hang_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(vox_hang_scale), 1.0, 1.0);
  gtk_range_set_value(GTK_RANGE(vox_hang_scale), vox_hang);
  gtk_widget_show(vox_hang_scale);
  gtk_grid_attach(GTK_GRID(grid), vox_hang_scale, 1, 4, 3, 1);
  g_signal_connect(G_OBJECT(vox_hang_scale), "value_changed", G_CALLBACK(vox_hang_value_changed_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  start_level_thread();
}
