/* Copyright (C)
* 2023 - Christoph van Wüllen, DL1YCF
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
#include <ctype.h>   // Stellt isxdigit zur Verfügung

#include "radio.h"
#include "new_menu.h"
#include "main.h"
#include "appearance.h"
#include "message.h"
#include "sliders.h"
#include "css.h"
#include "toolset.h"

static GtkWidget *dialog = NULL;
static GtkWidget *wide_b = NULL;
static GtkWidget *height_b = NULL;
static GtkWidget *full_b = NULL;
static GtkWidget *vfo_b = NULL;
static gulong vfo_signal_id;
static guint apply_timeout = 0;
static GtkWidget *bgcolor_text_input;
static gulong bgcolor_text_input_signal_id;
static GtkWidget *display_extras_btn;

//
// local copies of global variables
//
static int my_display_width;
static int my_display_height;
static int my_full_screen;
static int my_vfo_layout;
static int my_rx_stack_horizontal;

//
// It has been reported (and I could reproduce)
// that hitting the width or heigth
// button in fast succession leads to internal GTK crashes
// Therefore, we delegate the GTK screen change operations to
// a timeout handler that is at most called every 500 msec
//
static int apply(gpointer data) {
  apply_timeout = 0;
  display_width       = my_display_width;
  display_height      = my_display_height;
  full_screen         = my_full_screen;
  vfo_layout          = my_vfo_layout;
  rx_stack_horizontal = my_rx_stack_horizontal;
  radio_reconfigure_screen();

  //
  // VFO layout may have been re-adjusted so update combo-box
  // (without letting it emit a signal)
  //
  if (vfo_layout != my_vfo_layout) {
    my_vfo_layout = vfo_layout;
    g_signal_handler_block(G_OBJECT(vfo_b), vfo_signal_id);
    gtk_combo_box_set_active(GTK_COMBO_BOX(vfo_b), my_vfo_layout);
    g_signal_handler_unblock(G_OBJECT(vfo_b), vfo_signal_id);
  }

  return G_SOURCE_REMOVE;
}

void schedule_apply() {
  if (apply_timeout > 0) {
    g_source_remove(apply_timeout);
  }

  apply_timeout = g_timeout_add(500, apply, NULL);
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

void screen_menu_cleanup(void) {
  cleanup();
}

static void reload_css_cb(GtkWidget *widget, gpointer data) {
  t_print("%s: Reload CSS...\n", __FUNCTION__);
  load_css();
}


#ifdef __USELESS__
static void vfo_cb(GtkWidget *widget, gpointer data) {
  my_vfo_layout = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  VFO_HEIGHT = vfo_layout_list[my_vfo_layout].height;
  int needed = vfo_layout_list[my_vfo_layout].width + MIN_METER_WIDTH + MENU_WIDTH;

  if (needed % 32 != 0) { needed = 32 * (needed / 32 + 1); }

  if (needed > screen_width) { needed = screen_width; }

  if (needed > my_display_width && wide_b) {
    my_display_width = needed;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(wide_b), (double) my_display_width);
  }

  schedule_apply();
}

#endif

static void width_cb(GtkWidget *widget, gpointer data) {
  my_display_width = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  schedule_apply();
}

static void height_cb(GtkWidget *widget, gpointer data) {
  my_display_height = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  schedule_apply();
}

#if defined (__USELESS__)
static void horizontal_cb(GtkWidget *widget, gpointer data) {
  my_rx_stack_horizontal = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_apply();
}

#endif

static void full_cb(GtkWidget *widget, gpointer data) {
  my_full_screen = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_apply();
}

static void display_zoompan_cb(GtkWidget *widget, gpointer data) {
  display_zoompan = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_apply();
}

static void display_sliders_cb(GtkWidget *widget, gpointer data) {
  display_sliders = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_apply();
}

static void display_toolbar_cb(GtkWidget *widget, gpointer data) {
  display_toolbar = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_apply();
}

static void display_warnings_cb(GtkWidget *widget, gpointer data) {
  display_warnings = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void display_pacurr_cb(GtkWidget *widget, gpointer data) {
  display_pacurr = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

// Funktion zur Überprüfung, ob der String ein gültiges Hex-Format hat
gboolean is_valid_hex(const char *str) {
  if (str[0] != '#' || strlen(str) != 7) {
    return FALSE;  // Muss mit # beginnen und 7 Zeichen lang sein
  }

  for (int i = 1; i < 7; i++) {
    if (!isxdigit(str[i])) {
      return FALSE;  // Überprüft, ob jedes Zeichen hexadezimal ist
    }
  }

  return TRUE;  // Gültig, wenn alle Zeichen Hexadezimal sind
}

gboolean is_valid_rgb(const char *str) {
  int r, g, b;
  return sscanf(str, "#%2x%2x%2x", &r, &g, &b) == 3 ? TRUE : FALSE;
}

static void bgcolor_button_clicked(GtkWidget *widget, gpointer data) {
  GtkEntry *bgcolor_text_input = GTK_ENTRY(data);  // Das GtkEntry-Widget
  const gchar *text = gtk_entry_get_text(bgcolor_text_input);  // Hole den eingegebenen Text
  gchar *mod_text = g_strdup(text); // Umkopieren weil text unveränderbar
  g_strup(mod_text); // in Grossbuchstaben konvertieren

  if (is_valid_hex(mod_text) && is_valid_rgb(mod_text)) {
    g_strlcpy(radio_bgcolor_rgb_hex, mod_text, sizeof(radio_bgcolor_rgb_hex));
    radio_set_bgcolor(top_window, NULL);
  } else {
    t_print("%s: ERROR: wrong RGB entry %s\n", __FUNCTION__, mod_text);
  }

  // bgcolor_text_input aktualisieren
  g_signal_handler_block(bgcolor_text_input, bgcolor_text_input_signal_id);
  gtk_entry_set_text(GTK_ENTRY(bgcolor_text_input), radio_bgcolor_rgb_hex);
  g_signal_handler_unblock(bgcolor_text_input, bgcolor_text_input_signal_id);
  gtk_widget_queue_draw(GTK_WIDGET(bgcolor_text_input));
}

static void bgcolor_entry_activate(GtkWidget *widget, gpointer data) {
  bgcolor_button_clicked(NULL, data);  // Simuliert den Klick des OK-Buttons
}

// Callback für den Button-Klick
static void display_extras_btn_cb(GtkWidget *widget, gpointer data) {
  int r = GPOINTER_TO_INT(data);
  display_extra_sliders = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (display_extra_sliders) {
    sliders_show_row(r);
  } else {
    sliders_hide_row(r);  // Versteckt die 4. Zeile des Grids
  }

  schedule_apply();
}


void screen_menu(GtkWidget *parent) {
  GtkWidget *label;
#if defined (__USELESS__)
  GtkWidget *button;
#endif
  my_display_width       = display_width;
  my_display_height      = display_height;
  my_full_screen         = full_screen;
  my_vfo_layout          = vfo_layout;
  my_rx_stack_horizontal = rx_stack_horizontal;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
#if defined (__LDESK__)
  char _title[32];
  snprintf(_title, 32, "%s - Screen Layout", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
#else
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Screen Layout");
#endif
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  int row = 0;
  int col = 0;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, col, row, 1, 1);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  GtkWidget *css_button_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(css_button_grid), TRUE);
  // gtk_grid_set_row_homogeneous(GTK_GRID(css_button_grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(css_button_grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(css_button_grid), 5);
  //--------------------------------------------------------------------------------------------
  GtkWidget *save_css_btn = gtk_button_new_with_label("Save CSS");
  gtk_widget_set_tooltip_text(save_css_btn,
                              "Save the hardcoded internal CSS to a file deskhpsdr.css\n"
                              "in the working directory for editing (e.g. change fonts, colors etc.)\n\n"
                              "You can edit this saved file and make your own adjustments\n"
                              "If deskhpsdr detect this file deskhpsdr.css, it will be used instead of the hardcoded CSS defintions\n\n"
                              "Be aware, a GTK CSS is not identical with a HTML CSS, you need to know, what you do !\n\n"
                              "There is NO SUPPORT for this special option");
  gtk_widget_set_name(save_css_btn, "boldlabel_blue");
  // gtk_grid_attach(GTK_GRID(css_button_grid), save_css_btn, 0, 0, 1, 1);
  g_signal_connect(save_css_btn, "clicked", G_CALLBACK(save_css), NULL);
  //--------------------------------------------------------------------------------------------
  GtkWidget *remove_css_btn = gtk_button_new_with_label("Remove CSS");
  gtk_widget_set_tooltip_text(remove_css_btn,
                              "Remove the deskhpsdr.css file from the working directory\n"
                              "Make first a backup of your deskhpsdr.css if exist and used\n"
                              "This bring back the use of internal, hardcoded CSS defintions\n"
                              "IF YOU DO THIS, ALL YOUR OWN CHANGES WILL BE LOST !\n\n"
                              "There is NO SUPPORT for this special option");
  gtk_widget_set_name(remove_css_btn, "boldlabel_blue");
  // gtk_grid_attach(GTK_GRID(css_button_grid), remove_css_btn, 1, 0, 1, 1);
  g_signal_connect(remove_css_btn, "clicked", G_CALLBACK(remove_css), NULL);
  //--------------------------------------------------------------------------------------------
  GtkWidget *reload_css_btn = gtk_button_new_with_label("Reload CSS");
  gtk_widget_set_tooltip_text(reload_css_btn,
                              "Reload the deskhpsdr.css file from the working directory\n"
                              "This allows you to apply edits to the CSS without restarting deskHPSDR\n\n"
                              "Be aware, a GTK CSS is not identical with a HTML CSS, you need to know, what you do!\n\n"
                              "There is NO SUPPORT for this special option");
  gtk_widget_set_name(reload_css_btn, "boldlabel_blue");
  g_signal_connect(reload_css_btn, "clicked", G_CALLBACK(reload_css_cb), NULL);

  //--------------------------------------------------------------------------------------------
  if (file_present(css_filename)) {
    t_print("%s: %s exist\n", __FUNCTION__, css_filename);
    // Remove CSS Button (Spalte 1, gleiche Zeile)
    gtk_grid_attach(GTK_GRID(css_button_grid), remove_css_btn, 1, 0, 1, 1);
    gtk_widget_set_hexpand(remove_css_btn, FALSE);
    gtk_widget_set_vexpand(remove_css_btn, TRUE);
    gtk_widget_set_halign(remove_css_btn, GTK_ALIGN_END);
    gtk_widget_set_valign(remove_css_btn, GTK_ALIGN_CENTER);
    gtk_widget_show(remove_css_btn);
    // Reload CSS Button (Spalte 0, gleiche Zeile)
    gtk_grid_attach(GTK_GRID(css_button_grid), reload_css_btn, 0, 0, 1, 1);
    gtk_widget_set_hexpand(reload_css_btn, FALSE);
    gtk_widget_set_vexpand(reload_css_btn, TRUE);
    gtk_widget_set_halign(reload_css_btn, GTK_ALIGN_END);
    gtk_widget_set_valign(reload_css_btn, GTK_ALIGN_CENTER);
    gtk_widget_show(reload_css_btn);
  } else {
    t_print("%s: %s don't exist\n", __FUNCTION__, css_filename);
    // Save CSS Button (Spalte 1, gleiche Zeile)
    gtk_grid_attach(GTK_GRID(css_button_grid), save_css_btn, 1, 0, 1, 1);
    gtk_widget_set_hexpand(save_css_btn, FALSE);
    gtk_widget_set_vexpand(save_css_btn, TRUE);
    gtk_widget_set_halign(save_css_btn, GTK_ALIGN_END);
    gtk_widget_set_valign(save_css_btn, GTK_ALIGN_CENTER);
    gtk_widget_show(save_css_btn);
  }

  // CSS button grid
  gtk_widget_show(css_button_grid);
  gtk_grid_insert_column(GTK_GRID(css_button_grid), 0);
  gtk_widget_set_hexpand(css_button_grid, TRUE);
  gtk_grid_attach(GTK_GRID(grid), css_button_grid, col + 1, row, 1, 1);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  GtkWidget *bgcolor_label = gtk_label_new("Backgrund Color\n#RRGGBB (hex)");
  gtk_label_set_justify(GTK_LABEL(bgcolor_label), GTK_JUSTIFY_CENTER);
  gtk_widget_set_name(bgcolor_label, "boldlabel_blue");
  gtk_widget_set_halign(bgcolor_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), bgcolor_label, 1, 3, 1, 1);
  bgcolor_text_input = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(bgcolor_text_input), 7);
  gtk_entry_set_text(GTK_ENTRY(bgcolor_text_input), radio_bgcolor_rgb_hex);
  gtk_grid_attach(GTK_GRID(grid), bgcolor_text_input, 2, 3, 1, 1);
  bgcolor_text_input_signal_id = g_signal_connect(bgcolor_text_input, "activate", G_CALLBACK(bgcolor_entry_activate),
                                 bgcolor_text_input);
  gtk_widget_show(bgcolor_text_input);
  GtkWidget *bgcolor_btn = gtk_button_new_with_label("Set");
  gtk_widget_set_halign(bgcolor_btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), bgcolor_btn, 3, 3, 1, 1);
  g_signal_connect(bgcolor_btn, "clicked", G_CALLBACK(bgcolor_button_clicked), bgcolor_text_input);
  gtk_widget_show(bgcolor_btn);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  row++;
  label = gtk_label_new("Window Width:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
#if defined (__LDESK__)
  wide_b = gtk_spin_button_new_with_range(1280.0, (double) screen_width, 32.0);
#else
  wide_b = gtk_spin_button_new_with_range(640.0, (double) screen_width, 32.0);
#endif
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(wide_b), (double) my_display_width);
  gtk_grid_attach(GTK_GRID(grid), wide_b, col, row, 1, 1);
  g_signal_connect(wide_b, "value-changed", G_CALLBACK(width_cb), NULL);
  col++;
  label = gtk_label_new("Window Height:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
#if defined (__LDESK__)
  height_b = gtk_spin_button_new_with_range(600.0, (double) screen_height, 16.0);
#else
  height_b = gtk_spin_button_new_with_range(400.0, (double) screen_height, 16.0);
#endif
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(height_b), (double) my_display_height);
  gtk_grid_attach(GTK_GRID(grid), height_b, col, row, 1, 1);
  g_signal_connect(height_b, "value-changed", G_CALLBACK(height_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("This version is for Desktops,\ntherefore no VFO bar layouts available !");
#ifdef __USELESS__
  label = gtk_label_new("Select VFO bar layout:");
#endif
  gtk_widget_set_name(label, "boldlabel_red");
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 2, 1);
  gtk_widget_set_margin_start(label, 10);  // Abstand am Anfang
  col++;
  label = gtk_label_new("Minimum Screensize must\n be 1280x600 or higher !");
  gtk_widget_set_name(label, "boldlabel_red");
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
  gtk_grid_attach(GTK_GRID(grid), label, 2, row, 2, 1);
  gtk_widget_set_margin_start(label, 10);  // Abstand am Anfang
  col++;
#ifdef __USELESS__
  vfo_b = gtk_combo_box_text_new();
  const VFO_BAR_LAYOUT *vfl = vfo_layout_list;

  for (;;) {
    if (vfl->width < 0) { break; }

    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(vfo_b), NULL, vfl->description);
    vfl++;
  }

  gtk_combo_box_set_active(GTK_COMBO_BOX(vfo_b), my_vfo_layout);
  // This combo-box spans three columns so the text may be really long
  my_combo_attach(GTK_GRID(grid), vfo_b, col, row, 3, 1);
  vfo_signal_id = g_signal_connect(vfo_b, "changed", G_CALLBACK(vfo_cb), NULL);
#endif
  row++;
#if defined (__USELESS__)
  button = gtk_check_button_new_with_label("Stack receivers horizontally");
  gtk_widget_set_name(button, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), my_rx_stack_horizontal);
  gtk_grid_attach(GTK_GRID(grid), button, 0, row, 2, 1);
  g_signal_connect(button, "toggled", G_CALLBACK(horizontal_cb), NULL);
#endif
  full_b = gtk_check_button_new_with_label("Full Screen Mode");
  gtk_widget_set_name(full_b, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(full_b), my_full_screen);
#if defined (__LDESK__)
  gtk_grid_attach(GTK_GRID(grid), full_b, 0, row, 2, 1);
#else
  gtk_grid_attach(GTK_GRID(grid), full_b, 2, row, 2, 1);
#endif
  g_signal_connect(full_b, "toggled", G_CALLBACK(full_cb), NULL);
  row++;
  GtkWidget *b_display_zoompan = gtk_check_button_new_with_label("Display Zoom/Pan");
  gtk_widget_set_name (b_display_zoompan, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_zoompan), display_zoompan);
  gtk_widget_show(b_display_zoompan);
  gtk_grid_attach(GTK_GRID(grid), b_display_zoompan, 0, row, 1, 1);
  g_signal_connect(b_display_zoompan, "toggled", G_CALLBACK(display_zoompan_cb), NULL);
  GtkWidget *b_display_sliders = gtk_check_button_new_with_label("Display Sliders");
  gtk_widget_set_name (b_display_sliders, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_sliders), display_sliders);
  gtk_widget_show(b_display_sliders);
  gtk_grid_attach(GTK_GRID(grid), b_display_sliders, 1, row, 1, 1);
  g_signal_connect(b_display_sliders, "toggled", G_CALLBACK(display_sliders_cb), NULL);
  GtkWidget *b_display_toolbar = gtk_check_button_new_with_label("Display Toolbar");
  gtk_widget_set_name (b_display_toolbar, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_toolbar), display_toolbar);
  gtk_widget_show(b_display_toolbar);
  gtk_grid_attach(GTK_GRID(grid), b_display_toolbar, 2, row, 1, 1);
  g_signal_connect(b_display_toolbar, "toggled", G_CALLBACK(display_toolbar_cb), NULL);

  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (can_transmit) {
    display_extras_btn = gtk_check_button_new_with_label("Display Extras");
    gtk_widget_set_name (display_extras_btn, "boldlabel_blue");
    gtk_grid_attach(GTK_GRID(grid), display_extras_btn, 3, row, 1, 1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(display_extras_btn), display_extra_sliders);
    int row_to_hide = 2; // from sliders.c
    g_signal_connect(display_extras_btn, "clicked", G_CALLBACK(display_extras_btn_cb), GINT_TO_POINTER(row_to_hide));
    gtk_widget_show(display_extras_btn);
  }

  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  row++;
  GtkWidget *b_display_warnings = gtk_check_button_new_with_label("Display Warnings");
  gtk_widget_set_name (b_display_warnings, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_warnings), display_warnings);
  gtk_widget_show(b_display_warnings);
  gtk_grid_attach(GTK_GRID(grid), b_display_warnings, 0, row, 1, 1);
  g_signal_connect(b_display_warnings, "toggled", G_CALLBACK(display_warnings_cb), NULL);
  GtkWidget *b_display_pacurr = gtk_check_button_new_with_label("Display PA current");
  gtk_widget_set_name (b_display_pacurr, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_pacurr), display_pacurr);
  gtk_widget_show(b_display_pacurr);
  gtk_grid_attach(GTK_GRID(grid), b_display_pacurr, 1, row, 1, 1);
  g_signal_connect(b_display_pacurr, "toggled", G_CALLBACK(display_pacurr_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
