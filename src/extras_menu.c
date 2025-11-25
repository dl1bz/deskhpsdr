/* Copyright (C)
*
*   2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
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
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "new_menu.h"
#include "radio.h"
#include "message.h"
#include "rx_panadapter.h"

static GtkWidget *dialog = NULL;
static gulong dxc_login_box_signal_id;
static gulong dxc_address_box_signal_id;
static gulong atuwin_title_box_signal_id;
static gulong atuwin_url_box_signal_id;
static gulong atuwin_action_box_signal_id;

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

static void atuwin_action_button_clicked(GtkWidget *widget, gpointer data) {
  GtkEntry *atuwin_action_box = GTK_ENTRY(data);
  const gchar *text = gtk_entry_get_text(atuwin_action_box);
  gsize len = text ? strlen(text) : 0;

  if (text && len >= 1 && len < sizeof(atuwin_ACTION)) {
    gchar *upper = g_ascii_strup(text, -1);  // oder g_strup(text)
    g_strlcpy(atuwin_ACTION, upper, sizeof(atuwin_ACTION));
    g_free(upper);
  } else {
    g_signal_handler_block(atuwin_action_box, atuwin_action_box_signal_id);
    gtk_entry_set_text(atuwin_action_box, atuwin_ACTION);
    g_signal_handler_unblock(atuwin_action_box, atuwin_action_box_signal_id);
  }

  gtk_widget_queue_draw(GTK_WIDGET(atuwin_action_box));
  // optional: cleanup() / close_cb() wenn Save + Close gewünscht ist
  cleanup();
}

static void atuwin_url_button_clicked(GtkWidget *widget, gpointer data) {
  GtkEntry *atuwin_url_box = GTK_ENTRY(data);
  const gchar *text = gtk_entry_get_text(atuwin_url_box);
  gsize len = text ? strlen(text) : 0;

  if (text && len >= 1 && len < sizeof(atuwin_URL)) {
    g_strlcpy(atuwin_URL, text, sizeof(atuwin_URL));
  } else {
    g_signal_handler_block(atuwin_url_box, atuwin_url_box_signal_id);
    gtk_entry_set_text(atuwin_url_box, atuwin_URL);
    g_signal_handler_unblock(atuwin_url_box, atuwin_url_box_signal_id);
  }

  gtk_widget_queue_draw(GTK_WIDGET(atuwin_url_box));
  // optional: cleanup() / close_cb() wenn Save + Close gewünscht ist
  cleanup();
}

static void atuwin_title_button_clicked(GtkWidget *widget, gpointer data) {
  GtkEntry *atuwin_title_box = GTK_ENTRY(data);
  const gchar *text = gtk_entry_get_text(atuwin_title_box);
  gsize len = text ? strlen(text) : 0;

  if (text && len >= 1 && len < sizeof(atuwin_TITLE)) {
    g_strlcpy(atuwin_TITLE, text, sizeof(atuwin_TITLE));
  } else {
    g_signal_handler_block(atuwin_title_box, atuwin_title_box_signal_id);
    gtk_entry_set_text(atuwin_title_box, atuwin_TITLE);
    g_signal_handler_unblock(atuwin_title_box, atuwin_title_box_signal_id);
  }

  gtk_widget_queue_draw(GTK_WIDGET(atuwin_title_box));
  // optional: cleanup() / close_cb() wenn Save + Close gewünscht ist
  cleanup();
}

static void dxc_address_button_clicked(GtkWidget *widget, gpointer data) {
  GtkEntry *dxc_address_box = GTK_ENTRY(data);
  const gchar *text = gtk_entry_get_text(dxc_address_box);
  gsize len = text ? strlen(text) : 0;

  if (text && len >= 1 && len < sizeof(dxc_address)) {
    g_strlcpy(dxc_address, text, sizeof(dxc_address));
  } else {
    g_signal_handler_block(dxc_address_box, dxc_address_box_signal_id);
    gtk_entry_set_text(dxc_address_box, dxc_address);
    g_signal_handler_unblock(dxc_address_box, dxc_address_box_signal_id);
  }

  gtk_widget_queue_draw(GTK_WIDGET(dxc_address_box));
  // optional: cleanup() / close_cb() wenn Save + Close gewünscht ist
  cleanup();
}

static void dxc_login_button_clicked(GtkWidget *widget, gpointer data) {
  GtkEntry *dxc_login_box = GTK_ENTRY(data);
  const gchar *text = gtk_entry_get_text(dxc_login_box);
  gsize len = text ? strlen(text) : 0;

  if (text && len >= 3 && len < sizeof(dxc_login)) {
    gchar *upper = g_ascii_strup(text, -1);  // oder g_strup(text)
    g_strlcpy(dxc_login, upper, sizeof(dxc_login));
    g_free(upper);
  } else {
    g_signal_handler_block(dxc_login_box, dxc_login_box_signal_id);
    gtk_entry_set_text(dxc_login_box, dxc_login);
    g_signal_handler_unblock(dxc_login_box, dxc_login_box_signal_id);
  }

  gtk_widget_queue_draw(GTK_WIDGET(dxc_login_box));
  // optional: cleanup() / close_cb() wenn Save + Close gewünscht ist
  cleanup();
}

static void dxc_port_spin_btn_changed_cb(GtkSpinButton *spin, gpointer user_data) {
  dxc_port = gtk_spin_button_get_value_as_int(spin);

  /* Sicherheitsnetz */
  if (dxc_port < 1) {
    dxc_port = 1;
  } else if (dxc_port > 65535) {
    dxc_port = 65535;
  }
}

static void dxspot_lifetime_spin_btn_cb(GtkSpinButton *spin, gpointer user_data) {
  pan_spot_lifetime_min = gtk_spin_button_get_value_as_int(spin);

  /* Sicherheitsnetz */
  if (pan_spot_lifetime_min < 1) {
    pan_spot_lifetime_min = 1;
  } else if (pan_spot_lifetime_min > 720) {
    pan_spot_lifetime_min = 720;
  }
}

static void dxspot_max_rows_spin_btn_cb(GtkSpinButton *spin, gpointer user_data) {
  int val = gtk_spin_button_get_value_as_int(spin);
  panadapter_set_max_label_rows(val);
}

static void atuwin_w_spin_btn_changed_cb(GtkSpinButton *spin, gpointer user_data) {
  atuwin_wv_w = gtk_spin_button_get_value_as_int(spin);

  /* Sicherheitsnetz */
  if (atuwin_wv_w < 400) {
    atuwin_wv_w = 400;
  } else if (atuwin_wv_w > 9999) {
    atuwin_wv_w = 9999;
  }
}

static void atuwin_h_spin_btn_changed_cb(GtkSpinButton *spin, gpointer user_data) {
  atuwin_wv_h = gtk_spin_button_get_value_as_int(spin);

  /* Sicherheitsnetz */
  if (atuwin_wv_h < 400) {
    atuwin_wv_h = 400;
  } else if (atuwin_wv_h > 9999) {
    atuwin_wv_h = 9999;
  }
}

void extras_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[64];
  snprintf(_title, 64, "%s - Extras", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  //--------------------------------------------------------------------------------
  int row = 0;
  int col = 0;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, col, row, 2, 1);
  row++;
  col = 0;
  GtkWidget *t_label = gtk_label_new(NULL);
  gtk_label_set_use_markup(GTK_LABEL(t_label), TRUE);
  gtk_label_set_markup(GTK_LABEL(t_label), "<u>Telnet DX Cluster Window Setup</u>");
  gtk_widget_set_name(t_label, "boldlabel_blue");
  gtk_widget_set_margin_top(t_label, 10);
  gtk_widget_set_margin_bottom(t_label, 10);
  gtk_widget_set_halign(t_label, GTK_ALIGN_START);
  gtk_widget_set_margin_start(t_label, 5);
  gtk_grid_attach(GTK_GRID(grid), t_label, col, row, 3, 1);
  row++;
  col = 0;
  GtkWidget *label = gtk_label_new("DXC Login:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  GtkWidget *dxc_login_box = gtk_entry_new();
  col++;
  gtk_entry_set_max_length(GTK_ENTRY(dxc_login_box), sizeof(dxc_login) - 1);
  gtk_entry_set_text(GTK_ENTRY(dxc_login_box), dxc_login);
  gtk_grid_attach(GTK_GRID(grid), dxc_login_box, col, row, 2, 1);
  dxc_login_box_signal_id = g_signal_connect(dxc_login_box, "activate", G_CALLBACK(dxc_login_button_clicked),
                            dxc_login_box);
  col += 2;
  GtkWidget *dxc_login_box_btn = gtk_button_new_with_label("Set");
  gtk_widget_set_halign(dxc_login_box_btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), dxc_login_box_btn, col, row, 1, 1);
  g_signal_connect(dxc_login_box_btn, "clicked", G_CALLBACK(dxc_login_button_clicked), dxc_login_box);
  gtk_widget_show(dxc_login_box_btn);
  //--------------------------------------------------------------------------------
  col++;
  label = gtk_label_new("DXC address:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  GtkWidget *dxc_address_box = gtk_entry_new();
  col++;
  gtk_entry_set_max_length(GTK_ENTRY(dxc_address_box), sizeof(dxc_address) - 1);
  gtk_entry_set_text(GTK_ENTRY(dxc_address_box), dxc_address);
  gtk_grid_attach(GTK_GRID(grid), dxc_address_box, col, row, 2, 1);
  dxc_address_box_signal_id = g_signal_connect(dxc_address_box, "activate", G_CALLBACK(dxc_address_button_clicked),
                              dxc_address_box);
  col += 2;
  GtkWidget *dxc_address_box_btn = gtk_button_new_with_label("Set");
  gtk_widget_set_halign(dxc_address_box_btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), dxc_address_box_btn, col, row, 1, 1);
  g_signal_connect(dxc_address_box_btn, "clicked", G_CALLBACK(dxc_address_button_clicked), dxc_address_box);
  gtk_widget_show(dxc_address_box_btn);
  //--------------------------------------------------------------------------------
  col++;
  label = gtk_label_new("DXC Port:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkAdjustment *dxc_port_adj = gtk_adjustment_new(
                                  dxc_port,     // initialer Portwert
                                  1,            // Minimum
                                  65535,        // Maximum
                                  1,            // Schrittweite
                                  10,           // Page-Increment (Pfeiltasten halten)
                                  0
                                );
  GtkWidget *dxc_port_spin_btn = gtk_spin_button_new(dxc_port_adj, 1.0, 0);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(dxc_port_spin_btn), TRUE);
  gtk_grid_attach(GTK_GRID(grid), dxc_port_spin_btn, col, row, 1, 1);
  g_signal_connect(dxc_port_spin_btn, "value-changed", G_CALLBACK(dxc_port_spin_btn_changed_cb), NULL);
  //--------------------------------------------------------------------------------
  //--------------------------------------------------------------------------------
  row++;
  col = 0;
  t_label = gtk_label_new(NULL);
  gtk_label_set_use_markup(GTK_LABEL(t_label), TRUE);
  gtk_label_set_markup(GTK_LABEL(t_label), "<u>DX Spots on RX Panadapter Setup</u>");
  gtk_widget_set_name(t_label, "boldlabel_blue");
  gtk_widget_set_margin_top(t_label, 10);
  gtk_widget_set_margin_bottom(t_label, 10);
  gtk_widget_set_halign(t_label, GTK_ALIGN_START);
  gtk_widget_set_margin_start(t_label, 5);
  gtk_grid_attach(GTK_GRID(grid), t_label, col, row, 3, 1);
  //--------------------------------------------------------------------------------
  //--------------------------------------------------------------------------------
  row++;
  col = 0;
  label = gtk_label_new("DX Spots\nLifetime:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkAdjustment *dxspot_lifetime_adj = gtk_adjustment_new(
                                         pan_spot_lifetime_min,     // initialer Portwert
                                         1,            // Minimum
                                         720,        // Maximum
                                         1,            // Schrittweite
                                         10,           // Page-Increment (Pfeiltasten halten)
                                         0
                                       );
  GtkWidget *dxspot_lifetime_spin_btn = gtk_spin_button_new(dxspot_lifetime_adj, 1.0, 0);
  gtk_widget_set_tooltip_text(dxspot_lifetime_spin_btn,
                              "DX spot lifetime (minutes) before removal from the RX panadapter.\n\n"
                              "min: 1min\n"
                              "max: 720min (12h)\n"
                              "default: 15min");
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(dxspot_lifetime_spin_btn), TRUE);
  gtk_grid_attach(GTK_GRID(grid), dxspot_lifetime_spin_btn, col, row, 1, 1);
  g_signal_connect(dxspot_lifetime_spin_btn, "value-changed", G_CALLBACK(dxspot_lifetime_spin_btn_cb), NULL);
  //--------------------------------------------------------------------------------
  col++;
  label = gtk_label_new("DX Spots\nmax. rows:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkAdjustment *dxspot_max_rows_adj = gtk_adjustment_new(
                                         max_pan_label_rows,     // initialer Portwert
                                         1,            // Minimum
                                         32,           // Maximum
                                         1,            // Schrittweite
                                         10,           // Page-Increment (Pfeiltasten halten)
                                         0
                                       );
  GtkWidget *dxspot_max_rows_spin_btn = gtk_spin_button_new(dxspot_max_rows_adj, 1.0, 0);
  gtk_widget_set_tooltip_text(dxspot_max_rows_spin_btn, "Max stacked label rows (RX panadapter).\n\n"
                                                        "min: 1 row\n"
                                                        "max: 32 rows\n"
                                                        "default: 6 rows");
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(dxspot_max_rows_spin_btn), TRUE);
  gtk_grid_attach(GTK_GRID(grid), dxspot_max_rows_spin_btn, col, row, 1, 1);
  g_signal_connect(dxspot_max_rows_spin_btn, "value-changed", G_CALLBACK(dxspot_max_rows_spin_btn_cb), NULL);

  if (dxcwin_open) {
    gtk_widget_set_sensitive(dxc_login_box, FALSE);
    gtk_widget_set_sensitive(dxc_login_box_btn, FALSE);
    gtk_widget_set_sensitive(dxc_address_box, FALSE);
    gtk_widget_set_sensitive(dxc_address_box_btn, FALSE);
    gtk_widget_set_sensitive(dxc_port_spin_btn, FALSE);
    // gtk_widget_set_sensitive(dxspot_lifetime_spin_btn, FALSE);
    // gtk_widget_set_sensitive(dxspot_max_rows_spin_btn, FALSE);
  }

  //--------------------------------------------------------------------------------
  row++;
  col = 0;
  GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(sep, -1, 3);
  gtk_widget_set_margin_top(sep, 10);
  gtk_widget_set_margin_bottom(sep, 10);
  gtk_grid_attach(GTK_GRID(grid), sep, col, row, 10, 1);
  //--------------------------------------------------------------------------------
  //--------------------------------------------------------------------------------
  row++;
  col = 0;
  t_label = gtk_label_new(NULL);
  gtk_label_set_use_markup(GTK_LABEL(t_label), TRUE);
  gtk_label_set_markup(GTK_LABEL(t_label), "<u>User defined Window with Webaccess</u>");
  gtk_widget_set_name(t_label, "boldlabel_blue");
  gtk_widget_set_margin_top(t_label, 10);
  gtk_widget_set_margin_bottom(t_label, 10);
  gtk_widget_set_halign(t_label, GTK_ALIGN_START);
  gtk_widget_set_margin_start(t_label, 5);
  gtk_grid_attach(GTK_GRID(grid), t_label, col, row, 4, 1);
  row++;
  col = 0;
  label = gtk_label_new("Titlebar:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  GtkWidget *atuwin_title_box = gtk_entry_new();
  col++;
  gtk_entry_set_max_length(GTK_ENTRY(atuwin_title_box), sizeof(atuwin_TITLE) - 1);
  gtk_entry_set_text(GTK_ENTRY(atuwin_title_box), atuwin_TITLE);
  gtk_grid_attach(GTK_GRID(grid), atuwin_title_box, col, row, 2, 1);
  atuwin_title_box_signal_id = g_signal_connect(atuwin_title_box, "activate", G_CALLBACK(atuwin_title_button_clicked),
                               atuwin_title_box);
  col += 2;
  GtkWidget *atuwin_title_box_btn = gtk_button_new_with_label("Set");
  gtk_widget_set_halign(atuwin_title_box_btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), atuwin_title_box_btn, col, row, 1, 1);
  g_signal_connect(atuwin_title_box_btn, "clicked", G_CALLBACK(atuwin_title_button_clicked), atuwin_title_box);
  gtk_widget_show(atuwin_title_box_btn);
  //--------------------------------------------------------------------------------
  col++;
  label = gtk_label_new("URL:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  GtkWidget *atuwin_url_box = gtk_entry_new();
  col++;
  gtk_entry_set_max_length(GTK_ENTRY(atuwin_url_box), sizeof(atuwin_URL) - 1);
  gtk_entry_set_text(GTK_ENTRY(atuwin_url_box), atuwin_URL);
  gtk_grid_attach(GTK_GRID(grid), atuwin_url_box, col, row, 3, 1);
  atuwin_url_box_signal_id = g_signal_connect(atuwin_url_box, "activate", G_CALLBACK(atuwin_url_button_clicked),
                             atuwin_url_box);
  col += 3;
  GtkWidget *atuwin_url_box_btn = gtk_button_new_with_label("Set");
  gtk_widget_set_halign(atuwin_url_box_btn, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), atuwin_url_box_btn, col, row, 1, 1);
  g_signal_connect(atuwin_url_box_btn, "clicked", G_CALLBACK(atuwin_url_button_clicked), atuwin_url_box);
  gtk_widget_show(atuwin_url_box_btn);
  //--------------------------------------------------------------------------------
  row++;
  col = 0;
  label = gtk_label_new("Window\nWidth:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_widget_set_margin_top(label, 10);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkAdjustment *atuwin_w_adj = gtk_adjustment_new(
                                  atuwin_wv_w,     // initialer Portwert
                                  400,          // Minimum
                                  9999,         // Maximum
                                  5,            // Schrittweite
                                  50,           // Page-Increment (Pfeiltasten halten)
                                  0
                                );
  GtkWidget *atuwin_w_spin_btn = gtk_spin_button_new(atuwin_w_adj, 1.0, 0);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(atuwin_w_spin_btn), TRUE);
  gtk_widget_set_margin_top(atuwin_w_spin_btn, 10);
  gtk_grid_attach(GTK_GRID(grid), atuwin_w_spin_btn, col, row, 1, 1);
  g_signal_connect(atuwin_w_spin_btn, "value-changed", G_CALLBACK(atuwin_w_spin_btn_changed_cb), NULL);
  //--------------------------------------------------------------------------------
  col++;
  label = gtk_label_new("Window\nHeight:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_widget_set_margin_top(label, 10);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  col++;
  GtkAdjustment *atuwin_h_adj = gtk_adjustment_new(
                                  atuwin_wv_h,     // initialer Portwert
                                  400,          // Minimum
                                  9999,         // Maximum
                                  5,            // Schrittweite
                                  50,           // Page-Increment (Pfeiltasten halten)
                                  0
                                );
  GtkWidget *atuwin_h_spin_btn = gtk_spin_button_new(atuwin_h_adj, 1.0, 0);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(atuwin_h_spin_btn), TRUE);
  gtk_widget_set_margin_top(atuwin_h_spin_btn, 10);
  gtk_grid_attach(GTK_GRID(grid), atuwin_h_spin_btn, col, row, 1, 1);
  g_signal_connect(atuwin_h_spin_btn, "value-changed", G_CALLBACK(atuwin_h_spin_btn_changed_cb), NULL);
  //--------------------------------------------------------------------------------
  col++;
  label = gtk_label_new("Action\nButton:");
  gtk_widget_set_name(label, "boldlabel_blue");
  gtk_widget_set_margin_top(label, 10);
  gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
  GtkWidget *atuwin_action_box = gtk_entry_new();
  col++;
  gtk_entry_set_max_length(GTK_ENTRY(atuwin_action_box), sizeof(atuwin_ACTION) - 1);
  gtk_entry_set_text(GTK_ENTRY(atuwin_action_box), atuwin_ACTION);
  gtk_widget_set_margin_top(atuwin_action_box, 10);
  gtk_grid_attach(GTK_GRID(grid), atuwin_action_box, col, row, 1, 1);
  atuwin_action_box_signal_id = g_signal_connect(atuwin_action_box, "activate", G_CALLBACK(atuwin_action_button_clicked),
                                atuwin_action_box);
  col += 1;
  GtkWidget *atuwin_action_btn = gtk_button_new_with_label("Set");
  gtk_widget_set_halign(atuwin_action_btn, GTK_ALIGN_START);
  gtk_widget_set_margin_top(atuwin_action_btn, 10);
  gtk_grid_attach(GTK_GRID(grid), atuwin_action_btn, col, row, 1, 1);
  g_signal_connect(atuwin_action_btn, "clicked", G_CALLBACK(atuwin_action_button_clicked), atuwin_action_box);
  gtk_widget_show(atuwin_action_btn);
  //--------------------------------------------------------------------------------
  //--------------------------------------------------------------------------------
  row++;
  col = 0;
  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(sep, -1, 3);
  gtk_widget_set_margin_top(sep, 10);
  gtk_widget_set_margin_bottom(sep, 10);
  gtk_grid_attach(GTK_GRID(grid), sep, col, row, 10, 1);
  //--------------------------------------------------------------------------------
  //--------------------------------------------------------------------------------
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}