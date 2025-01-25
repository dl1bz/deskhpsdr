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
#include <stdio.h>
#include <string.h>

#include "radio.h"
#include "new_menu.h"
#include "actions.h"
#include "action_dialog.h"
#include "gpio.h"
#include "toolbar.h"

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

static gboolean switch_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
  SWITCH *sw = (SWITCH *) data;
  int action = action_dialog(dialog, CONTROLLER_SWITCH, sw->switch_function);
  gtk_button_set_label(GTK_BUTTON(widget), ActionTable[action].button_str);
  sw->switch_function = action;
  update_toolbar_labels();
  return TRUE;
}

void toolbar_menu(GtkWidget *parent) {
  GtkWidget *widget;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
#if defined (__LDESK__)
  char _title[64];
  snprintf(_title, 64, "%s - Toolbar configuration", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
#else
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Toolbar configuration");
#endif
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 3, 1);
  int lfunction = 0;
#if defined (__LDESK__)
  const int max_switches = 10;
#else
  const int max_switches = 8;
#endif

  for (lfunction = 0; lfunction < MAX_FUNCTIONS; lfunction++) {
    SWITCH *sw = switches_controller1[lfunction];

    for (int i = 0; i < max_switches; i++) {
      if (i == max_switches - 1) {
        // Rightmost switch is hardwired to FUNCTION
        sw[i].switch_function = FUNCTION;
        gchar text[16];
        snprintf(text, 16, "FNC(%d)", lfunction);
        widget = gtk_button_new_with_label(text);
        gtk_grid_attach(GTK_GRID(grid), widget, i, lfunction + 1, 1, 1);
      } else {
        widget = gtk_button_new_with_label(ActionTable[sw[i].switch_function].button_str);
        gtk_grid_attach(GTK_GRID(grid), widget, i, lfunction + 1, 1, 1);
        g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), (gpointer) &sw[i]);
      }
    }
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
