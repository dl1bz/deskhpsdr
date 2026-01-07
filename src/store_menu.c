/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT, * 2016 - Steve Wilson, KA6S
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "radio.h"
#include "new_menu.h"
#include "store_menu.h"
#include "store.h"
#include "mode.h"
#include "filter.h"
#include "message.h"

static GtkWidget *dialog = NULL;

GtkWidget *store_button[NUM_OF_MEMORYS];

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

static gboolean store_select_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  int ind = GPOINTER_TO_INT(data);
  char label_str[40];
  char fw[16];
  char sm[16];
  store_memory_slot(ind);
  int mode = mem[ind].mode;

  if (mode == modeFMN) {
    snprintf(fw, 16, "%s", mem[ind].deviation == 2500 ? "11k" : "16k");
  } else {
    int filter = mem[ind].filter;
    snprintf(fw, 16, "%s", filters[mode][filter].title);
  }

  switch (mem[ind].sat_mode) {
  case SAT_MODE:
    snprintf(sm, 16, "%s", "SAT, ");
    break;

  case RSAT_MODE:
    snprintf(sm, 16, "%s", "RSAT, ");
    break;

  default:
    sm[0] = 0;
    break;
  }

  snprintf(label_str, 40, "M%d=%8.3f MHz (%s%s, %s)", ind,
           mem[ind].ctun ? (double) mem[ind].ctun_frequency * 1E-6 : (double) mem[ind].frequency * 1E-6,
           sm, mode_string[mode], fw);
  gtk_button_set_label(GTK_BUTTON(store_button[ind]), label_str);
  return FALSE;
}

// cppcheck-suppress constParameterCallback
static gboolean recall_select_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  int ind = GPOINTER_TO_INT(data);
  recall_memory_slot(ind);
  return FALSE;
}

void store_menu(GtkWidget *parent) {
  GtkWidget *b;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[32];
  snprintf(_title, 32, "%s - Memories", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);

  for (int ind = 0; ind < NUM_OF_MEMORYS; ind++) {
    char label_str[50];
    char fw[16];
    char sm[16];
    snprintf(label_str, 50, "Store M%d", ind);
    int mode = mem[ind].mode;
    b = gtk_button_new_with_label(label_str);
    g_signal_connect(b, "button-press-event", G_CALLBACK(store_select_cb), GINT_TO_POINTER(ind));
    gtk_grid_attach(GTK_GRID(grid), b, 0, ind + 1, 1, 1);

    if (mode == modeFMN) {
      snprintf(fw, 16, "%s", mem[ind].deviation == 2500 ? "11k" : "16k");
    } else {
      int filter = mem[ind].filter;
      snprintf(fw, 16, "%s", filters[mode][filter].title);
    }

    switch (mem[ind].sat_mode) {
    case SAT_MODE:
      snprintf(sm, 16, "%s", "SAT, ");
      break;

    case RSAT_MODE:
      snprintf(sm, 16, "%s", "RSAT, ");
      break;

    default:
      sm[0] = 0;
      break;
    }

    snprintf(label_str, 50, "M%d=%8.3f MHz (%s%s, %s)", ind,
             mem[ind].ctun ? (double) mem[ind].ctun_frequency * 1E-6 : (double) mem[ind].frequency * 1E-6,
             sm, mode_string[mode], fw);
    b = gtk_button_new_with_label(label_str);
    store_button[ind] = b;
    g_signal_connect(b, "button-press-event", G_CALLBACK(recall_select_cb), GINT_TO_POINTER(ind));
    gtk_grid_attach(GTK_GRID(grid), b, 1, ind + 1, 3, 1);
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
