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

#include "new_protocol.h"
#include "old_protocol.h"
#include "vfo.h"
#include "ext.h"

static GtkWidget *dialog = NULL;
static GtkWidget *wide_b = NULL;
static GtkWidget *height_b = NULL;
static GtkWidget *full_b = NULL;
// static GtkWidget *vfo_b = NULL;
// static gulong vfo_signal_id;
static guint apply_timeout = 0;
static GtkWidget *display_extras_btn;
static GtkWidget *b_display_af_peak = NULL;
static gulong b_af_peak_signal_id;
static GtkWidget *b_use_levels_popup = NULL;
static gulong b_use_levels_popup_signal_id;
#ifdef __linux__
  static GtkWidget *b_inner_levels_popup = NULL;
  static gulong b_inner_levels_popup_signal_id;
#endif

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
    // g_signal_handler_block(G_OBJECT(vfo_b), vfo_signal_id);
    // gtk_combo_box_set_active(GTK_COMBO_BOX(vfo_b), my_vfo_layout);
    // g_signal_handler_unblock(G_OBJECT(vfo_b), vfo_signal_id);
  }

  return G_SOURCE_REMOVE;
}

void schedule_apply() {
  if (apply_timeout != 0) {
    g_source_remove(apply_timeout);
    apply_timeout = 0;
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


static void chkbtn_toggle_cb(GtkWidget *widget, gpointer data) {
  int *value = (int *) data;
  *value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_general();
  schedule_transmit_specific();
  schedule_high_priority();
  g_idle_add(ext_vfo_update, NULL);
  radio_reconfigure_screen();
}

static void width_cb(GtkWidget *widget, gpointer data) {
  my_display_width = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  schedule_apply();
}

static void height_cb(GtkWidget *widget, gpointer data) {
  my_display_height = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  schedule_apply();
}

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

static void display_levels_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit) {
    transmitter->show_levels = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

    if (!transmitter->show_levels) {
      g_signal_handler_block(b_display_af_peak, b_af_peak_signal_id);
      transmitter->show_af_peak = 0;
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_af_peak), transmitter->show_af_peak);
      g_signal_handler_unblock(b_display_af_peak, b_af_peak_signal_id);
      gtk_widget_set_sensitive(b_display_af_peak, FALSE);
      gtk_widget_set_sensitive(b_use_levels_popup, FALSE);
#ifdef __linux__
      gtk_widget_set_sensitive(b_inner_levels_popup, FALSE);
#endif
    } else {
      gtk_widget_set_sensitive(b_display_af_peak, TRUE);
      gtk_widget_set_sensitive(b_use_levels_popup, TRUE);
#ifdef __linux__
      gtk_widget_set_sensitive(b_inner_levels_popup, TRUE);
#endif
    }
  }
}

static void display_levels_af_peak_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit) {
    transmitter->show_af_peak = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  }
}

static void b_use_levels_popup_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit) {
    if (use_wayland) {
      transmitter->use_levels_popup = 1;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), transmitter->use_levels_popup);
    } else {
      transmitter->use_levels_popup = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    }
  }
}

#ifdef __linux__
static void b_inner_levels_popup_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit) {
    if (full_screen) {
      transmitter->inner_levels_popup = 1;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), transmitter->inner_levels_popup);
    } else {
      transmitter->inner_levels_popup = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    }
  }
}
#endif

static void bg_colour_set(GtkColorButton *btn, gpointer user_data) {
  cairo_rgba_t *c = (cairo_rgba_t *)user_data;
  GdkRGBA g;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &g);
  c->r = g.red;
  c->g = g.green;
  c->b = g.blue;
  c->a = g.alpha;
  win_set_bgcolor(top_window, &g);
}

static void win_bg_colour_set(GtkColorButton *btn, gpointer user_data) {
  cairo_rgba_t *c = (cairo_rgba_t *)user_data;
  GdkRGBA g;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &g);
  c->r = g.red;
  c->g = g.green;
  c->b = g.blue;
  c->a = g.alpha;
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
  my_display_width       = display_width;
  my_display_height      = display_height;
  my_full_screen         = full_screen;
  my_vfo_layout          = vfo_layout;
  my_rx_stack_horizontal = rx_stack_horizontal;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[32];
  snprintf(_title, 32, "%s - Screen Layout", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
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
  // Optimize for touchscreen
  GtkWidget *ChkBtn_tscreen = gtk_check_button_new_with_label("Optimize for TouchScreen");
  gtk_widget_set_name(ChkBtn_tscreen, "boldlabel");
  gtk_widget_set_margin_start(ChkBtn_tscreen, 20);    // linker Rand (Anfang)
  gtk_widget_set_tooltip_text(ChkBtn_tscreen,
                              "Change the design of some buttons and\nsliders for easier use with a touch screen");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ChkBtn_tscreen), optimize_for_touchscreen);
  gtk_grid_attach(GTK_GRID(grid), ChkBtn_tscreen, 2, 0, 2, 1);
  g_signal_connect(ChkBtn_tscreen, "toggled", G_CALLBACK(chkbtn_toggle_cb), &optimize_for_touchscreen);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  row++;
  label = gtk_label_new("Window Width:");
  gtk_widget_set_name(label, "boldlabel");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  wide_b = gtk_spin_button_new_with_range(1280.0, (double) screen_width, 32.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(wide_b), (double) my_display_width);
  gtk_grid_attach(GTK_GRID(grid), wide_b, col, row, 1, 1);
  g_signal_connect(wide_b, "value-changed", G_CALLBACK(width_cb), NULL);
  col++;
  label = gtk_label_new("Window Height:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  height_b = gtk_spin_button_new_with_range(600.0, (double) screen_height, 16.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(height_b), (double) my_display_height);
  gtk_grid_attach(GTK_GRID(grid), height_b, col, row, 1, 1);
  g_signal_connect(height_b, "value-changed", G_CALLBACK(height_cb), NULL);
  row++;
  col = 0;
  label = gtk_label_new("deskHPSDR is made for Desktops,\ntherefore no VFO bar layouts available !");
  gtk_widget_set_name(label, "boldlabel_red");
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 2, 1);
  gtk_widget_set_margin_start(label, 10);  // Abstand am Anfang
  col += 2;
  label = gtk_label_new("Minimum Screensize must\n be 1280x600 or higher !");
  gtk_widget_set_name(label, "boldlabel_red");
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 2, 1);
  gtk_widget_set_margin_start(label, 10);  // Abstand am Anfang
  col = 0;
  row++;
  full_b = gtk_check_button_new_with_label("Full Screen Mode");
  gtk_widget_set_name(full_b, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(full_b), my_full_screen);
  gtk_grid_attach(GTK_GRID(grid), full_b, col, row, 1, 1);
  // gtk_box_pack_start(GTK_BOX(Z3_box), full_b, FALSE, FALSE, 0);
  g_signal_connect(full_b, "toggled", G_CALLBACK(full_cb), NULL);
  col++;
  //---------------------------------------------------------------------------------------------------
  // Zeile als Box ab Spalte 1
  GtkWidget *Z3_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *bgcolor_label = gtk_label_new("Slider Background Color");
  gtk_label_set_justify(GTK_LABEL(bgcolor_label), GTK_JUSTIFY_CENTER);
  gtk_widget_set_name(bgcolor_label, "boldlabel_blue");
  gtk_widget_set_halign(bgcolor_label, GTK_ALIGN_END);
  gtk_widget_set_margin_start(bgcolor_label, 5);
  gtk_widget_set_margin_end(bgcolor_label, 5);
  gtk_box_pack_start(GTK_BOX(Z3_box), bgcolor_label, FALSE, FALSE, 0);
  //---
  GtkWidget *bg_col_btn = gtk_color_button_new();
  gtk_color_button_set_use_alpha(GTK_COLOR_BUTTON(bg_col_btn), TRUE);
  GdkRGBA bg_col_init = { radio_bgcolor.r, radio_bgcolor.g, radio_bgcolor.b, radio_bgcolor.a };
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(bg_col_btn), &bg_col_init);
  g_signal_connect(bg_col_btn, "color-set", G_CALLBACK(bg_colour_set), &radio_bgcolor);
  gtk_widget_set_tooltip_text(bg_col_btn, "Set background color of Slider front surface\n\n"
                                          "Default color is RGB #E6E6FA");
  gtk_widget_set_margin_top(bg_col_btn, 5);
  gtk_widget_set_margin_bottom(bg_col_btn, 5);
  gtk_box_pack_start(GTK_BOX(Z3_box), bg_col_btn, FALSE, FALSE, 0);
  //---
  GtkWidget *w_bgcolor_label = gtk_label_new("Menu Background Color");
  gtk_label_set_justify(GTK_LABEL(w_bgcolor_label), GTK_JUSTIFY_CENTER);
  gtk_widget_set_name(w_bgcolor_label, "boldlabel_blue");
  gtk_widget_set_halign(w_bgcolor_label, GTK_ALIGN_END);
  gtk_widget_set_margin_start(w_bgcolor_label, 5);
  gtk_widget_set_margin_end(w_bgcolor_label, 5);
  gtk_box_pack_start(GTK_BOX(Z3_box), w_bgcolor_label, FALSE, FALSE, 0);
  //---
  GtkWidget *win_bg_col_btn = gtk_color_button_new();
  gtk_color_button_set_use_alpha(GTK_COLOR_BUTTON(win_bg_col_btn), TRUE);
  GdkRGBA win_bg_col_init = { mwin_bgcolor.r, mwin_bgcolor.g, mwin_bgcolor.b, mwin_bgcolor.a };
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(win_bg_col_btn), &win_bg_col_init);
  g_signal_connect(win_bg_col_btn, "color-set", G_CALLBACK(win_bg_colour_set), &mwin_bgcolor);
  gtk_widget_set_margin_top(win_bg_col_btn, 5);
  gtk_widget_set_margin_bottom(win_bg_col_btn, 5);
  gtk_box_pack_start(GTK_BOX(Z3_box), win_bg_col_btn, FALSE, FALSE, 0);
  //---------------------------------------------------------------------------------------------------
  // Box ins Grid
  gtk_grid_attach(GTK_GRID(grid), Z3_box, col, row, 3, 1);
  //---------------------------------------------------------------------------------------------------
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
  //------------------------------------------------------------------------------------------
  GtkWidget *b_display_pacurr = gtk_check_button_new_with_label("Display PA current");
  gtk_widget_set_name (b_display_pacurr, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_pacurr), display_pacurr);
  gtk_widget_show(b_display_pacurr);
  gtk_grid_attach(GTK_GRID(grid), b_display_pacurr, 1, row, 1, 1);
  g_signal_connect(b_display_pacurr, "toggled", G_CALLBACK(display_pacurr_cb), NULL);

  //------------------------------------------------------------------------------------------
  if (can_transmit) {
    row++;
    GtkWidget *label_levels = gtk_label_new(NULL);
    gtk_label_set_use_markup(GTK_LABEL(label_levels), TRUE);

    if (use_wayland) {
      gtk_label_set_markup(GTK_LABEL(label_levels), "<u>TX Audio Levels Display Settings</u> [Wayland compatibility mode]");
    } else {
      gtk_label_set_markup(GTK_LABEL(label_levels), "<u>TX Audio Levels Display Settings</u>");
    }

    gtk_widget_set_name (label_levels, "boldlabel_blue");
    gtk_widget_set_halign(label_levels, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label_levels, 5);
    gtk_grid_attach(GTK_GRID(grid), label_levels, 0, row, 2, 1);
    gtk_widget_show(label_levels);
    row++;
    GtkWidget *b_display_levels = gtk_check_button_new_with_label("Display TX AF Levels");
    gtk_widget_set_name (b_display_levels, "boldlabel_blue");
    gtk_widget_set_tooltip_text(b_display_levels,
                                "ONLY DURING TX = ACTIVE\n(except selected mode CW-L/CW-U/DIGI-U/DIGI-L):\n"
                                "Show an additional window displaying all critical\nTX audio chain levels in realtime.\n"
                                "- levels are displayed in dBV, except ALC which is shown in dB\n"
                                "- GREEN segment covers the range from -5dBV to 0dBV\n"
                                "- RED segment covers the range from 0dBV to +10dBV\n\n"
                                "0 dbV = 1.0 Veff\n\n"
                                "All audio levels must remain below the red segment to avoid distortion,"
                                "the maximum level should be kept within the green segment.\n\n"
                                "It's essential to correctly adjust EVERY part of the TX audio chain !");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_levels), transmitter->show_levels);
    gtk_widget_show(b_display_levels);
    gtk_grid_attach(GTK_GRID(grid), b_display_levels, 0, row, 1, 1);
    g_signal_connect(b_display_levels, "toggled", G_CALLBACK(display_levels_cb), NULL);
    //----------------------------------------------------------------------------------------
    b_display_af_peak = gtk_check_button_new_with_label("Show as Peak");
    gtk_widget_set_name (b_display_af_peak, "boldlabel_blue");
    gtk_widget_set_tooltip_text(b_display_af_peak, "Show TX Audio AF levels as Peak (default is Average)");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_display_af_peak), transmitter->show_af_peak);
    gtk_widget_show(b_display_af_peak);
    gtk_grid_attach(GTK_GRID(grid), b_display_af_peak, 1, row, 1, 1);
    b_af_peak_signal_id = g_signal_connect(b_display_af_peak, "toggled", G_CALLBACK(display_levels_af_peak_cb), NULL);

    if (!transmitter->show_levels) {
      gtk_widget_set_sensitive(b_display_af_peak, FALSE);
    } else {
      gtk_widget_set_sensitive(b_display_af_peak, TRUE);
    }

    //----------------------------------------------------------------------------------------
    b_use_levels_popup = gtk_check_button_new_with_label("Use Popup");
    gtk_widget_set_name (b_use_levels_popup, "boldlabel_blue");
    gtk_widget_set_tooltip_text(b_use_levels_popup, "If ENABLED,\n"
                                                    "show the TX Audio AF levels as Popup instead\n"
                                                    "of a separate, detached window.\n\n"
                                                    "REQUIRED if X11 backend running with Wayland !\n\n"
                                                    "Note: If Wayland is used, this option\n"
                                                    "cannot be modified and appears unresponsive.");

    if (use_wayland) {
      transmitter->use_levels_popup = 1;
    }

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_use_levels_popup), transmitter->use_levels_popup);
    gtk_widget_show(b_use_levels_popup);
    gtk_grid_attach(GTK_GRID(grid), b_use_levels_popup, 2, row, 1, 1);
    b_use_levels_popup_signal_id = g_signal_connect(b_use_levels_popup, "toggled", G_CALLBACK(b_use_levels_popup_cb), NULL);

    if (!transmitter->show_levels || use_wayland) {
      gtk_widget_set_sensitive(b_use_levels_popup, FALSE);
    } else {
      gtk_widget_set_sensitive(b_use_levels_popup, TRUE);
    }

#ifdef __linux__

    //----------------------------------------------------------------------------------------
    if (use_wayland) {
      b_inner_levels_popup = gtk_check_button_new_with_label("Show Popup inside");
      gtk_widget_set_name (b_inner_levels_popup, "boldlabel_blue");
      gtk_widget_set_tooltip_text(b_inner_levels_popup, "Show TX Audio Levels Popup:\n"
                                                        "ENABLED: inside deskHPSDR main window [right in the middle]\n"
                                                        "DISABLED: outside deskHPSDR main window [right in the middle]\n\n"
                                                        "If Fullscreen selected, TX Audio Levels Popup is ever\n"
                                                        "inside deskHPSDR main window [right in the middle]");

      if (full_screen) {
        transmitter->inner_levels_popup = 1;
      }

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b_inner_levels_popup), transmitter->inner_levels_popup);
      gtk_widget_show(b_inner_levels_popup);
      gtk_grid_attach(GTK_GRID(grid), b_inner_levels_popup, 3, row, 1, 1);
      b_inner_levels_popup_signal_id = g_signal_connect(b_inner_levels_popup, "toggled", G_CALLBACK(b_inner_levels_popup_cb),
                                       NULL);

      if (!transmitter->show_levels) {
        gtk_widget_set_sensitive(b_inner_levels_popup, FALSE);
      } else {
        gtk_widget_set_sensitive(b_inner_levels_popup, TRUE);
      }
    }

    //----------------------------------------------------------------------------------------
#endif
  }

  //------------------------------------------------------------------------------------------
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
