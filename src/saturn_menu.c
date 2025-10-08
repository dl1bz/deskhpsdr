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
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdbool.h>

#include "new_menu.h"
#include "saturn_menu.h"
#include "saturnserver.h"
#include "radio.h"

static GtkWidget *dialog = NULL;
static GtkWidget *client_enable_tx_b;

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

static void server_enable_cb(GtkWidget *widget, gpointer data) {
  saturn_server_en = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (saturn_server_en) {
    start_saturn_server();
  } else {
    client_enable_tx = 0;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (client_enable_tx_b), 0);
    shutdown_saturn_server();
  }

  gtk_widget_set_sensitive(client_enable_tx_b, saturn_server_en);
}

#ifdef SATURNTEST
static void client_enable_tx_cb(GtkWidget *widget, gpointer data) {
  if (!saturn_server_en) { gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), 0); }
  else { client_enable_tx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)); }
}

#endif

void saturn_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();

  // paranoia is always good when programming
  if (!saturn_server_en) {
    client_enable_tx = FALSE;
  }

  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[32];
  snprintf(_title, 32, "%s - Saturn", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  g_signal_connect (close_b, "pressed", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  GtkWidget *server_enable_b = gtk_check_button_new_with_label("Saturn Server Enable");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (server_enable_b), saturn_server_en);
  gtk_widget_show(server_enable_b);
  gtk_grid_attach(GTK_GRID(grid), server_enable_b, 0, 1, 1, 1);
  g_signal_connect(server_enable_b, "toggled", G_CALLBACK(server_enable_cb), NULL);
#ifdef SATURNTEST
  client_enable_tx_b = gtk_check_button_new_with_label("Client Transmit Enable");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (client_enable_tx_b), client_enable_tx);
  gtk_widget_show(client_enable_tx_b);
  gtk_grid_attach(GTK_GRID(grid), client_enable_tx_b, 1, 1, 1, 1);
  g_signal_connect(client_enable_tx_b, "toggled", G_CALLBACK(client_enable_tx_cb), NULL);
#else
  client_enable_tx_b = gtk_label_new(" *Client is RX Only*");
  gtk_widget_set_name(client_enable_tx_b, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), client_enable_tx_b, 1, 1, 1, 1);
#endif
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  //
  // The client enable tx button will only be "sensitive" is the
  // saturn server is enabled
  //
#ifdef SATURNTEST
  gtk_widget_set_sensitive(client_enable_tx_b, saturn_server_en);
#endif
  gtk_widget_show_all(dialog);
}

