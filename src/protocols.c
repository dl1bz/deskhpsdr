/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
* 2024-2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
* SPDX-License-Identifier: GPL-3.0-or-later
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
#include <gdk/gdk.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "radio.h"
#include "protocols.h"
#include "css.h"
#include "new_menu.h"

static GtkWidget *dialog;

gboolean enable_protocol_1 = TRUE;
gboolean enable_protocol_2 = TRUE;
gboolean enable_stemlab = TRUE;
gboolean enable_usbozy = TRUE;
gboolean enable_saturn_xdma = TRUE;
gboolean autostart = FALSE;

static void protocolsSaveState(void) {
  StartConfigSave();
}

static gboolean delete_event_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
  (void)event;
  (void)data;
  gtk_widget_destroy(widget);
  return TRUE;
}

static void destroy_cb(GtkWidget *widget, gpointer data) {
  (void)widget;
  (void)data;
  dialog = NULL;
  protocolsSaveState();
}

static void close_button_cb(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  if (dialog != NULL) {
    gtk_widget_destroy(dialog);
  }
}

static void protocol_1_cb(GtkToggleButton *widget, gpointer data) {
  enable_protocol_1 = gtk_toggle_button_get_active(widget);
}

static void protocol_2_cb(GtkToggleButton *widget, gpointer data) {
  enable_protocol_2 = gtk_toggle_button_get_active(widget);
}

#ifdef STEMLAB_DISCOVERY
static void stemlab_cb(GtkToggleButton *widget, gpointer data) {
  enable_stemlab = gtk_toggle_button_get_active(widget);
}

#endif

#ifdef SATURN
static void saturn_xdma_cb(GtkToggleButton *widget, gpointer data) {
  enable_saturn_xdma = gtk_toggle_button_get_active(widget);
}

#endif

#ifdef USBOZY
static void usbozy_cb(GtkToggleButton *widget, gpointer data) {
  enable_usbozy = gtk_toggle_button_get_active(widget);
}

#endif

static void autostart_cb(GtkToggleButton *widget, gpointer data) {
  autostart = gtk_toggle_button_get_active(widget);
}

void configure_protocols(GtkWidget *parent) {
  int row;
  if (dialog != NULL) {
    gtk_window_present(GTK_WINDOW(dialog));
    return;
  }
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[32];
  snprintf(_title, 32, "%s - Protocols", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  g_signal_connect(dialog, "delete-event", G_CALLBACK(delete_event_cb), NULL);
  g_signal_connect(dialog, "destroy", G_CALLBACK(destroy_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  row = 0;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  g_signal_connect(close_b, "clicked", G_CALLBACK(close_button_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, row, 1, 1);
  row++;
  GtkWidget *b_enable_protocol_1 = gtk_check_button_new_with_label("Enable Protocol 1");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_enable_protocol_1), enable_protocol_1);
  gtk_widget_show(b_enable_protocol_1);
  g_signal_connect(b_enable_protocol_1, "toggled", G_CALLBACK(protocol_1_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_enable_protocol_1, 0, row, 1, 1);
  row++;
  GtkWidget *b_enable_protocol_2 = gtk_check_button_new_with_label("Enable Protocol 2");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_enable_protocol_2), enable_protocol_2);
  gtk_widget_show(b_enable_protocol_2);
  g_signal_connect(b_enable_protocol_2, "toggled", G_CALLBACK(protocol_2_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_enable_protocol_2, 0, row, 1, 1);
  row++;
#ifdef SATURN
  GtkWidget *b_saturn_xdma = gtk_check_button_new_with_label("Enable Saturn XDMA");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_saturn_xdma), enable_saturn_xdma);
  gtk_widget_show(b_saturn_xdma);
  g_signal_connect(b_saturn_xdma, "toggled", G_CALLBACK(saturn_xdma_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_saturn_xdma, 0, row, 1, 1);
  row++;
#endif
#ifdef USBOZY
  GtkWidget *b_usbozy = gtk_check_button_new_with_label("Enable USB OZY");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_usbozy), enable_usbozy);
  gtk_widget_show(b_usbozy);
  g_signal_connect(b_usbozy, "toggled", G_CALLBACK(usbozy_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_usbozy, 0, row, 1, 1);
  row++;
#endif
#ifdef STEMLAB_DISCOVERY
  GtkWidget *b_enable_stemlab = gtk_check_button_new_with_label("Enable STEMlab");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_enable_stemlab), enable_stemlab);
  gtk_widget_show(b_enable_stemlab);
  g_signal_connect(b_enable_stemlab, "toggled", G_CALLBACK(stemlab_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_enable_stemlab, 0, row, 1, 1);
  row++;
#endif
  GtkWidget *b_autostart = gtk_check_button_new_with_label("Auto start if only one device");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_autostart), autostart);
  gtk_widget_show(b_autostart);
  g_signal_connect(b_autostart, "toggled", G_CALLBACK(autostart_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_autostart, 0, row, 1, 1);
  gtk_container_add(GTK_CONTAINER(content), grid);
  gtk_widget_show_all(dialog);
  gtk_dialog_run(GTK_DIALOG(dialog));
}
