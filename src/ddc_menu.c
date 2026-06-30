/* Copyright (C)
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

#include "ddc_menu.h"
#include "css.h"
#include "main.h"
#include "new_menu.h"
#include "radio.h"
#include "new_protocol.h"

int p2_ddc_adc_map[P2_MAX_DDCS] = { 0, 1, 0, 1, 0, 0, 0 };

static GtkWidget *dialog = NULL;
static GtkWidget *note = NULL;
static GtkWidget *adc0_buttons[P2_MAX_DDCS];
static GtkWidget *adc1_buttons[P2_MAX_DDCS];
static int ddc_menu_updating = 0;

void ddc_menu_set_defaults_hermes(void) {
  for (int i = 0; i < P2_MAX_DDCS; i++) {
    p2_ddc_adc_map[i] = 0;
  }
}

static void ddc_menu_set_defaults_anan100d(void) {
  static const int defaults[P2_MAX_DDCS] = { 0, 1, 0, 1, 0, 0, 0 };
  for (int i = 0; i < P2_MAX_DDCS; i++) {
    p2_ddc_adc_map[i] = defaults[i];
  }
}

static void ddc_menu_update_buttons(void) {
  ddc_menu_updating = 1;
  for (int i = 0; i < P2_MAX_DDCS; i++) {
    int adc = (p2_ddc_adc_map[i] != 0) ? 1 : 0;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(adc0_buttons[i]), adc == 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(adc1_buttons[i]), adc == 1);
  }
  ddc_menu_updating = 0;
}

static void adc_toggled_cb(GtkToggleButton *button, gpointer data) {
  if (!gtk_toggle_button_get_active(button)) {
    return;
  }
  int value = GPOINTER_TO_INT(data);
  int ddc = value / 2;
  int adc = value % 2;
  if (ddc < 0 || ddc >= P2_MAX_DDCS) {
    return;
  }
  p2_ddc_adc_map[ddc] = adc;
  if (!ddc_menu_updating) {
    StartConfigSave();
  }
}

static void cleanup(void) {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu = NO_MENU;
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static gboolean reset_hermes_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  (void)widget;
  (void)event;
  (void)data;
  ddc_menu_set_defaults_hermes();
  ddc_menu_update_buttons();
  StartConfigSave();
  return TRUE;
}

static gboolean reset_anan100d_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  (void)widget;
  (void)event;
  (void)data;
  ddc_menu_set_defaults_anan100d();
  ddc_menu_update_buttons();
  StartConfigSave();
  return TRUE;
}

void ddc_menu(GtkWidget *parent) {
  if (dialog != NULL) {
    gtk_window_present(GTK_WINDOW(dialog));
    return;
  }
  gboolean is_hermes = (radio != NULL && radio->device == NEW_DEVICE_HERMES);
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[64];
  snprintf(_title, sizeof(_title), "%s - OpenHPSDR P2 ADC/DDC Menu", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  g_signal_connect(dialog, "delete_event", G_CALLBACK(close_cb), NULL);
  g_signal_connect(dialog, "destroy", G_CALLBACK(close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect(close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 2, 1);
  GtkWidget *title = gtk_label_new(NULL);
  gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
  gtk_label_set_markup(GTK_LABEL(title), "<u>OpenHPSDR P2 ADC/DDC Assignment</u>");
  gtk_widget_set_name(title, "boldlabel_blue");
  gtk_widget_set_margin_top(title, 10);
  gtk_widget_set_margin_bottom(title, 5);
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), title, 0, 1, P2_MAX_DDCS + 1, 1);
  if (!is_hermes) {
    note = gtk_label_new("Advanced Protocol 2 setting:\nSelect which ADC feeds each DDC. "
                         "Diversity and PureSignal use fixed protocol assignments.");
  } else {
    note = gtk_label_new("Advanced Protocol 2 setting:\nIf HERMES no selection, has only one ADC.");
  }
  gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
  gtk_widget_set_halign(note, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), note, 0, 2, P2_MAX_DDCS + 1, 1);
  for (int i = 0; i < P2_MAX_DDCS; i++) {
    char label[16];
    snprintf(label, sizeof(label), "DDC%d", i);
    GtkWidget *ddc_label = gtk_label_new(label);
    gtk_widget_set_halign(ddc_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), ddc_label, i + 1, 3, 1, 1);
  }
  GtkWidget *adc0_label = gtk_label_new("ADC0");
  gtk_widget_set_halign(adc0_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), adc0_label, 0, 4, 1, 1);
  GtkWidget *adc1_label = gtk_label_new("ADC1");
  gtk_widget_set_halign(adc1_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), adc1_label, 0, 5, 1, 1);
  for (int i = 0; i < P2_MAX_DDCS; i++) {
    GSList *group = NULL;
    adc0_buttons[i] = gtk_radio_button_new(group);
    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(adc0_buttons[i]));
    adc1_buttons[i] = gtk_radio_button_new(group);
    gtk_grid_attach(GTK_GRID(grid), adc0_buttons[i], i + 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), adc1_buttons[i], i + 1, 5, 1, 1);
    g_signal_connect(adc0_buttons[i], "toggled", G_CALLBACK(adc_toggled_cb), GINT_TO_POINTER(i * 2));
    g_signal_connect(adc1_buttons[i], "toggled", G_CALLBACK(adc_toggled_cb), GINT_TO_POINTER(i * 2 + 1));
    if (is_hermes) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(adc0_buttons[i]), TRUE);
      gtk_widget_set_sensitive(adc0_buttons[i], FALSE);
      gtk_widget_set_sensitive(adc1_buttons[i], FALSE);
    }
  }
  GtkWidget *rx1_label = gtk_label_new(NULL);
  gtk_widget_set_name(rx1_label, "boldlabel_border_blue");
  gtk_label_set_use_markup(GTK_LABEL(rx1_label), TRUE);
  gtk_label_set_markup(GTK_LABEL(rx1_label), "<b>RX1</b>");
  gtk_widget_set_halign(rx1_label, GTK_ALIGN_START);
  gtk_widget_set_tooltip_text(rx1_label, "RX1 DDC routing marker");
  if (device == NEW_DEVICE_ANGELIA) {
    gtk_grid_attach(GTK_GRID(grid), rx1_label, 3, 6, 1, 1);
  } else if (device == NEW_DEVICE_HERMES) {
    gtk_grid_attach(GTK_GRID(grid), rx1_label, 1, 6, 1, 1);
  } else {
    rx1_label = NULL;
  }
  GtkWidget *rx2_label = gtk_label_new(NULL);
  gtk_widget_set_name(rx2_label, "boldlabel_border_blue");
  gtk_label_set_use_markup(GTK_LABEL(rx2_label), TRUE);
  gtk_label_set_markup(GTK_LABEL(rx2_label), "<b>RX2</b>");
  gtk_widget_set_halign(rx2_label, GTK_ALIGN_START);
  gtk_widget_set_tooltip_text(rx2_label, "RX2 DDC routing marker");
  if (device == NEW_DEVICE_ANGELIA) {
    gtk_grid_attach(GTK_GRID(grid), rx2_label, 4, 6, 1, 1);
  } else if (device == NEW_DEVICE_HERMES) {
    gtk_grid_attach(GTK_GRID(grid), rx2_label, 2, 6, 1, 1);
  } else {
    rx2_label = NULL;
  }
  if (is_hermes) {
    GtkWidget *reset_hermes_b = gtk_button_new_with_label("Reset HERMES");
    g_signal_connect(reset_hermes_b, "button-press-event", G_CALLBACK(reset_hermes_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), reset_hermes_b, 0, 7, 2, 1);
  } else if (!is_hermes && device == NEW_DEVICE_ANGELIA) {
    GtkWidget *reset_anan100d_b = gtk_button_new_with_label("Reset ANAN-100D");
    g_signal_connect(reset_anan100d_b, "button-press-event", G_CALLBACK(reset_anan100d_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), reset_anan100d_b, 2, 7, 3, 1);
  }
  ddc_menu_update_buttons();
  gtk_container_add(GTK_CONTAINER(content), grid);
  gtk_widget_show_all(dialog);
}
