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
#include <stdio.h>
#include <string.h>

#include "radio.h"
#include "new_menu.h"
#include "actions.h"
#include "action_dialog.h"
#include "controller_mapping.h"
#include "toolbar.h"
#include "main.h"

static GtkWidget *dialog = NULL;
static GtkWidget *toolbar_menu_headerbar = NULL;
static GtkWidget *toolbar_menu_stack = NULL;
static GtkWidget *toolbar_menu_fnc_buttons[MAX_FUNCTIONS] = { NULL };
static gboolean toolbar_menu_updating_buttons = FALSE;

enum {
  TOOLBAR_MENU_BUTTONS_PER_ROW = 11,
  TOOLBAR_MENU_FIRST_ROW_CONFIG_BUTTONS = TOOLBAR_MENU_BUTTONS_PER_ROW - 1,
  TOOLBAR_MENU_LABEL_COL = 0,
  TOOLBAR_MENU_FIRST_BUTTON_COL = 1
};

static void cleanup(void) {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    toolbar_menu_headerbar = NULL;
    toolbar_menu_stack = NULL;
    for (int i = 0; i < MAX_FUNCTIONS; i++) {
      toolbar_menu_fnc_buttons[i] = NULL;
    }
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static void set_toolbar_menu_title(GtkWidget *headerbar, int lfunction) {
  char title[96];
  snprintf(title, sizeof(title), "%s - Toolbar configuration - FNC(%d)", PGNAME, lfunction);
  if (dialog != NULL) {
    gtk_window_set_title(GTK_WINDOW(dialog), title);
  }
  if (GTK_IS_HEADER_BAR(headerbar)) {
    gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), title);
  }
}

static const char *toolbar_menu_stack_name(int lfunction) {
  static char names[MAX_FUNCTIONS][16];
  if (lfunction < 0 || lfunction >= MAX_FUNCTIONS) {
    lfunction = 0;
  }
  snprintf(names[lfunction], sizeof(names[lfunction]), "fnc%d", lfunction);
  return names[lfunction];
}

static void toolbar_menu_select_function(int lfunction) {
  if (lfunction < 0 || lfunction >= MAX_FUNCTIONS) {
    lfunction = 0;
  }
  toolbar_menu_updating_buttons = TRUE;
  if (toolbar_menu_stack != NULL) {
    gtk_stack_set_visible_child_name(GTK_STACK(toolbar_menu_stack), toolbar_menu_stack_name(lfunction));
  }
  for (int i = 0; i < MAX_FUNCTIONS; i++) {
    if (toolbar_menu_fnc_buttons[i] != NULL) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toolbar_menu_fnc_buttons[i]), i == lfunction);
    }
  }
  toolbar_menu_updating_buttons = FALSE;
  set_toolbar_menu_title(toolbar_menu_headerbar, lfunction);
}

static void toolbar_menu_fnc_button_cb(GtkToggleButton *button, gpointer data) {
  int lfunction = GPOINTER_TO_INT(data);
  if (toolbar_menu_updating_buttons) {
    return;
  }
  if (gtk_toggle_button_get_active(button)) {
    toolbar_menu_select_function(lfunction);
  } else {
    toolbar_menu_updating_buttons = TRUE;
    gtk_toggle_button_set_active(button, TRUE);
    toolbar_menu_updating_buttons = FALSE;
  }
}

static GtkWidget *toolbar_menu_fnc_button_new(int lfunction) {
  char label[16];
  snprintf(label, sizeof(label), "FNC(%d)", lfunction);
  GtkWidget *button = gtk_toggle_button_new_with_label(label);
  gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(button), FALSE);
  gtk_widget_set_size_request(button, 92, 32);
  gtk_widget_set_halign(button, GTK_ALIGN_START);
  g_signal_connect(button, "toggled", G_CALLBACK(toolbar_menu_fnc_button_cb), GINT_TO_POINTER(lfunction));
  return button;
}

static gboolean switch_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
  SWITCH *sw = (SWITCH *) data;
  int action = action_dialog(dialog, MIDI_KEY, sw->switch_function);
  gtk_button_set_label(GTK_BUTTON(widget), ActionTable[action].button_str);
  sw->switch_function = action;
  update_toolbar_labels();
  return TRUE;
}

static GtkWidget *toolbar_page_new(int lfunction, int visible_rows) {
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
  SWITCH *sw = switches_toolbar[lfunction];
  for (int toolbar_row = 0; toolbar_row < visible_rows; toolbar_row++) {
    int config_buttons = (toolbar_row == 0) ? TOOLBAR_MENU_FIRST_ROW_CONFIG_BUTTONS : TOOLBAR_MENU_BUTTONS_PER_ROW;
    char row_label[32];
    snprintf(row_label, sizeof(row_label), "Toolbar %d", toolbar_row + 1);
    GtkWidget *label = gtk_label_new(row_label);
    gtk_widget_set_name(label, "boldlabel");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, TOOLBAR_MENU_LABEL_COL, toolbar_row, 1, 1);
    for (int i = 0; i < config_buttons; i++) {
      int switch_index = (toolbar_row * TOOLBAR_MENU_BUTTONS_PER_ROW) + i;
      GtkWidget *widget = gtk_button_new_with_label(ActionTable[sw[switch_index].switch_function].button_str);
      gtk_widget_set_name(widget, "small_button");
      gtk_grid_attach(GTK_GRID(grid), widget, TOOLBAR_MENU_FIRST_BUTTON_COL + i, toolbar_row, 1, 1);
      g_signal_connect(widget, "button-press-event", G_CALLBACK(switch_cb), (gpointer) &sw[switch_index]);
    }
    if (toolbar_row == 0) {
      char fnc_label[16];
      snprintf(fnc_label, sizeof(fnc_label), "FNC(%d)", lfunction);
      GtkWidget *fnc = gtk_button_new_with_label(fnc_label);
      gtk_widget_set_name(fnc, "small_button");
      gtk_widget_set_sensitive(fnc, FALSE);
      gtk_grid_attach(GTK_GRID(grid), fnc, TOOLBAR_MENU_FIRST_BUTTON_COL + TOOLBAR_MENU_FIRST_ROW_CONFIG_BUTTONS,
                      toolbar_row, 1, 1);
    }
  }
  return grid;
}

void toolbar_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &radio_bgcolor);
  toolbar_menu_headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), toolbar_menu_headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(toolbar_menu_headerbar), TRUE);
  set_toolbar_menu_title(toolbar_menu_headerbar, function);
  g_signal_connect(dialog, "delete_event", G_CALLBACK(close_cb), NULL);
  g_signal_connect(dialog, "destroy", G_CALLBACK(close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width(GTK_CONTAINER(box), 8);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect(close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_box_pack_start(GTK_BOX(box), close_b, FALSE, FALSE, 0);
  int my_width = full_screen ? screen_width : display_width;
  int my_height = full_screen ? screen_height : display_height;
  int visible_rows = toolbar_get_visible_rows(my_width, my_height);
  for (int i = 0; i < MAX_FUNCTIONS; i++) {
    toolbar_menu_fnc_buttons[i] = NULL;
  }
  GtkWidget *fnc_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_halign(fnc_bar, GTK_ALIGN_START);
  for (int lfunction = 0; lfunction < MAX_FUNCTIONS; lfunction++) {
    toolbar_menu_fnc_buttons[lfunction] = toolbar_menu_fnc_button_new(lfunction);
    gtk_box_pack_start(GTK_BOX(fnc_bar), toolbar_menu_fnc_buttons[lfunction], FALSE, FALSE, 0);
  }
  gtk_box_pack_start(GTK_BOX(box), fnc_bar, FALSE, FALSE, 0);
  toolbar_menu_stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(toolbar_menu_stack), GTK_STACK_TRANSITION_TYPE_NONE);
  gtk_widget_set_hexpand(toolbar_menu_stack, TRUE);
  gtk_widget_set_vexpand(toolbar_menu_stack, TRUE);
  for (int lfunction = 0; lfunction < MAX_FUNCTIONS; lfunction++) {
    GtkWidget *page = toolbar_page_new(lfunction, visible_rows);
    gtk_stack_add_named(GTK_STACK(toolbar_menu_stack), page, toolbar_menu_stack_name(lfunction));
  }
  gtk_box_pack_start(GTK_BOX(box), toolbar_menu_stack, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(content), box);
  sub_menu = dialog;
  toolbar_menu_select_function(function);
  gtk_widget_show_all(dialog);
}
