/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#include "new_menu.h"
#include "rigctl_menu.h"
#include "rigctl.h"
#include "band.h"
#include "radio.h"
#include "vfo.h"
#ifdef TCI
#include "tci.h"
#endif
#include "message.h"
#include "mystring.h"

static GtkWidget *dialog = NULL;
#if defined (__DVL__)
static GtkWidget *serial_baud[MAX_SERIAL+1];
static GtkWidget *serial_enable[MAX_SERIAL+1];
#else
static GtkWidget *serial_baud[MAX_SERIAL];
static GtkWidget *serial_enable[MAX_SERIAL];
#endif

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

static void tcp_autoreporting_cb(GtkWidget *widget, gpointer data) {
  rigctl_tcp_autoreporting = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void rigctl_value_changed_cb(GtkWidget *widget, gpointer data) {
  if (rigctl_tcp_enable) { shutdown_tcp_rigctl(); }

  rigctl_tcp_port = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  if (rigctl_tcp_enable) { launch_tcp_rigctl(); }
}

static void rigctl_debug_cb(GtkWidget *widget, gpointer data) {
  rigctl_debug = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  // t_print("---------- RIGCTL DEBUG %s ----------\n", rigctl_debug ? "ON" : "OFF");
}

#ifdef TCI
static void tci_enable_cb(GtkWidget *widget, gpointer data) {
  tci_enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  if (tci_enable) {
    launch_tci();
  } else {
    shutdown_tci();
  }
}
static void tci_port_changed_cb(GtkWidget *widget, gpointer data) {
  if (tci_enable) { shutdown_tci(); }
  tci_port = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  if (tci_enable) { launch_tci(); }
}
static void tci_txonly_changed_cb(GtkWidget *widget, gpointer data) {
  tci_txonly = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}
#endif

static void rigctl_tcp_enable_cb(GtkWidget *widget, gpointer data) {
  rigctl_tcp_enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (rigctl_tcp_enable) {
    launch_tcp_rigctl();
  } else {
    shutdown_tcp_rigctl();
  }
}

//
// Note that this call-back is invoked on each and every key stroke
// in the text field
//
static void serial_port_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  const char *cp = gtk_entry_get_text(GTK_ENTRY(widget));
  //
  // If the serial port is already running, do not allow changes.
  //
  // If the last serial port is marked as a G2-internal port,
  // and if the same port name is used, do not allow changes
  //
  if (SerialPorts[id].enable ||
     (SerialPorts[MAX_SERIAL-1].g2 && !strcmp(SerialPorts[MAX_SERIAL-1].port, cp))) {
    gtk_entry_set_text(GTK_ENTRY(widget), SerialPorts[id].port);
  } else {
    // const char *cp = gtk_entry_get_text(GTK_ENTRY(widget));
    // STRLCPY(SerialPorts[id].port, cp, sizeof(SerialPorts[id].port));
    snprintf(SerialPorts[id].port, sizeof(SerialPorts[id].port), "%s", cp);
  }
}

static void tcp_andromeda_cb(GtkWidget *widget, gpointer data) {
  rigctl_tcp_andromeda = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

//
// Note the following call-backs will never be called for G2-internal serial ports:
//
// - serial_autoreporting_cb
// - andromeda_cb
// - serial_enable_cb
// - baud_cb
//
//
static void serial_autoreporting_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  SerialPorts[id].autoreporting = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void andromeda_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  SerialPorts[id].andromeda = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (SerialPorts[id].andromeda) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(serial_baud[id]), 1);
    SerialPorts[id].baud = B9600;
  }
}

static void serial_enable_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  #if defined (__DVL__)
  if (id < MAX_SERIAL) {
  #endif
  if ((SerialPorts[id].enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))) {
    if (launch_serial_rigctl(id) == 0) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
      SerialPorts[id].enable = 0;
    }
  } else {
    disable_serial_rigctl(id);
  }
  #if defined (__DVL__)
  } else {
    SerialPorts[id].enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  }
  #endif
  t_print("%s: Serial enable : ID=%d Enabled=%d\n", __FUNCTION__, id, SerialPorts[id].enable);
}

// Set Baud Rate
static void baud_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  int bd = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  int new;

  //
  // If ANDROMEDA is active, keep 9600
  //
  if (SerialPorts[id].andromeda && SerialPorts[id].baud == B9600) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 1);
    return;
  }

  //
  // Do nothing if the baud rate is already effective.
  // If a serial client is already running and the baud rate is changed, we close and re-open it
  //
  switch (bd) {
  case 0:
  default:
    new = B4800;
    break;

  case 1:
    new = B9600;
    break;

  case 2:
    new = B19200;
    break;

  case 3:
    new = B38400;
    break;
  }

  if (new == SerialPorts[id].baud) {
    return;
  }

  SerialPorts[id].baud = new;

  if (SerialPorts[id].enable) {
    t_print("%s: closing/re-opening serial port %s\n", __FUNCTION__, SerialPorts[id].port);
    disable_serial_rigctl(id);

    if (launch_serial_rigctl(id) == 0) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(serial_enable[id]), FALSE);
      SerialPorts[id].enable = 0;
    }
  }

  t_print("%s: Baud rate changed: Port=%s Baud=%d\n", __FUNCTION__, SerialPorts[id].port, SerialPorts[id].baud);
}

void rigctl_menu(GtkWidget *parent) {
  GtkWidget *w;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  #if defined (__LDESK__)
  char _title[32];
    #ifdef TCI
      snprintf(_title, 32, "%s - CAT/TCI", PGNAME);
      gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
    #else
      snprintf(_title, 32, "%s - CAT", PGNAME);
      gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
    #endif
  #else
    #ifdef TCI
      gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - CAT/TCI");
    #else
      gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - CAT");
    #endif
  #endif
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  #ifdef TCI
    gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  #endif
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  int row = 0;
  w = gtk_button_new_with_label("Close");
  gtk_widget_set_name(w, "close_button");
  g_signal_connect (w, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 2, 1);
  #ifdef TCI
    w = gtk_check_button_new_with_label("Enable CAT/TCI Debug Logging");
  #else
    w = gtk_check_button_new_with_label("Enable CAT Debug Logging");
  #endif
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), rigctl_debug);
  gtk_grid_attach(GTK_GRID(grid), w, 4, row, 4, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(rigctl_debug_cb), NULL);
  row++;
  w = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(w, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 7, 1);
  row++;
  w = gtk_label_new("TCP");
  gtk_widget_set_name(w, "boldlabel");
  gtk_widget_set_halign(w, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 1, 1);
  w = gtk_spin_button_new_with_range(1025, 65535, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), (double)rigctl_tcp_port);
  gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
  g_signal_connect(w, "value_changed", G_CALLBACK(rigctl_value_changed_cb), NULL);
  w = gtk_check_button_new_with_label("Enable");
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), rigctl_tcp_enable);
  gtk_widget_show(w);
  gtk_grid_attach(GTK_GRID(grid), w, 4, row, 1, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(rigctl_tcp_enable_cb), NULL);
  w = gtk_check_button_new_with_label("Andromeda");
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), rigctl_tcp_andromeda);
  gtk_grid_attach(GTK_GRID(grid), w, 5, row, 1, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(tcp_andromeda_cb), NULL);
  w = gtk_check_button_new_with_label("AutoRprt");
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), rigctl_tcp_autoreporting);
  gtk_grid_attach(GTK_GRID(grid), w, 6, row, 1, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(tcp_autoreporting_cb), NULL);

  /* Put the Serial Port stuff here, one port per line */
  for (int i = 0; i < MAX_SERIAL; i++) {
    char str[64];
    row++;
    //
    // If this serial port is used internally in a G2 simply state port name
    //
    snprintf (str, 64, "Serial");
    w = gtk_label_new(str);
    gtk_widget_set_name(w, "boldlabel");
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), w, 0, row, 1, 1);

    //
    // If this serial port is used internally in a G2 simply state port name
    //
    if (!SerialPorts[i].g2) {
      w = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(w), SerialPorts[i].port);
      gtk_grid_attach(GTK_GRID(grid), w, 1, row, 2, 1);
      g_signal_connect(w, "changed", G_CALLBACK(serial_port_cb), GINT_TO_POINTER(i));
      serial_baud[i] = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_baud[i]), NULL, "4800 Bd");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_baud[i]), NULL, "9600 Bd");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_baud[i]), NULL, "19200 Bd");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_baud[i]), NULL, "38400 Bd");

      switch (SerialPorts[i].baud) {
      case B9600:
        gtk_combo_box_set_active(GTK_COMBO_BOX(serial_baud[i]), 1);
        break;

      case B19200:
        gtk_combo_box_set_active(GTK_COMBO_BOX(serial_baud[i]), 2);
        break;

      case B38400:
        gtk_combo_box_set_active(GTK_COMBO_BOX(serial_baud[i]), 3);
        break;

      default:
        SerialPorts[i].baud = B4800;
        gtk_combo_box_set_active(GTK_COMBO_BOX(serial_baud[i]), 0);
        break;
      }

      my_combo_attach(GTK_GRID(grid), serial_baud[i], 3, row, 1, 1);
      g_signal_connect(serial_baud[i], "changed", G_CALLBACK(baud_cb), GINT_TO_POINTER(i));

      serial_enable[i] = gtk_check_button_new_with_label("Enable");
      gtk_widget_set_name(serial_enable[i], "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (serial_enable[i]), SerialPorts[i].enable);
      gtk_grid_attach(GTK_GRID(grid), serial_enable[i], 4, row, 1, 1);
      g_signal_connect(serial_enable[i], "toggled", G_CALLBACK(serial_enable_cb), GINT_TO_POINTER(i));
      
      w = gtk_check_button_new_with_label("Andromeda");
      gtk_widget_set_name(w, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), SerialPorts[i].andromeda);
      gtk_grid_attach(GTK_GRID(grid), w, 5, row, 1, 1);
      g_signal_connect(w, "toggled", G_CALLBACK(andromeda_cb), GINT_TO_POINTER(i));
      
      w = gtk_check_button_new_with_label("AutoRprt");
      gtk_widget_set_name(w, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), SerialPorts[i].autoreporting);
      gtk_grid_attach(GTK_GRID(grid), w, 6, row, 1, 1);
      g_signal_connect(w, "toggled", G_CALLBACK(serial_autoreporting_cb), GINT_TO_POINTER(i));
    } else {
      //
      // If the Serial port is used for the G2 panel, just report port name.
      // If it is not enabled, this means the initial launch_serial() failed.
      //
      snprintf (str, 64, "%s %s for G2-internal communication", SerialPorts[i].port,
                 SerialPorts[i].enable ? "used" : "failed");
      w = gtk_label_new(str);
      gtk_widget_set_name(w, "boldlabel");
      gtk_widget_set_halign(w, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), w, 1, row, 5, 1);
    }
  }
#if defined (__DVL__)
//-----------------------------------------------------------------------------------------------------------------
    char str[64];
    row++;
    w = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_size_request(w, -1, 3);
    gtk_grid_attach(GTK_GRID(grid), w, 0, row, 7, 1);
    row++;
    snprintf (str, 64, "Serial");
    w = gtk_label_new(str);
    gtk_widget_set_name(w, "boldlabel");
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), w, 0, row, 1, 1);
    w = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w), SerialPorts[MAX_SERIAL].port);
    gtk_grid_attach(GTK_GRID(grid), w, 1, row, 2, 1);
    g_signal_connect(w, "changed", G_CALLBACK(serial_port_cb), GINT_TO_POINTER(MAX_SERIAL));

      serial_baud[MAX_SERIAL] = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_baud[MAX_SERIAL]), NULL, "4800 Bd");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_baud[MAX_SERIAL]), NULL, "9600 Bd");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_baud[MAX_SERIAL]), NULL, "19200 Bd");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(serial_baud[MAX_SERIAL]), NULL, "38400 Bd");

      switch (SerialPorts[MAX_SERIAL].baud) {
      case B9600:
        gtk_combo_box_set_active(GTK_COMBO_BOX(serial_baud[MAX_SERIAL]), 1);
        break;

      case B19200:
        gtk_combo_box_set_active(GTK_COMBO_BOX(serial_baud[MAX_SERIAL]), 2);
        break;

      case B38400:
        gtk_combo_box_set_active(GTK_COMBO_BOX(serial_baud[MAX_SERIAL]), 3);
        break;

      default:
        SerialPorts[MAX_SERIAL].baud = B4800;
        gtk_combo_box_set_active(GTK_COMBO_BOX(serial_baud[MAX_SERIAL]), 0);
        break;
      }

      my_combo_attach(GTK_GRID(grid), serial_baud[MAX_SERIAL], 3, row, 1, 1);
      g_signal_connect(serial_baud[MAX_SERIAL], "changed", G_CALLBACK(baud_cb), GINT_TO_POINTER(MAX_SERIAL));

      serial_enable[MAX_SERIAL] = gtk_check_button_new_with_label("Enable RTS & DTR during TUNE");
      gtk_widget_set_name(serial_enable[MAX_SERIAL], "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (serial_enable[MAX_SERIAL]), SerialPorts[MAX_SERIAL].enable);
      gtk_grid_attach(GTK_GRID(grid), serial_enable[MAX_SERIAL], 4, row, 1, 1);
      g_signal_connect(serial_enable[MAX_SERIAL], "toggled", G_CALLBACK(serial_enable_cb), GINT_TO_POINTER(MAX_SERIAL));

//-----------------------------------------------------------------------------------------------------------------
#endif
  // row++;
  // w = gtk_check_button_new_with_label("Enable RigCtl Debug Logging");
  // gtk_widget_set_name(w, "boldlabel");
  // gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), rigctl_debug);
  // gtk_grid_attach(GTK_GRID(grid), w, 0, row, 4, 1);
  // g_signal_connect(w, "toggled", G_CALLBACK(rigctl_debug_cb), NULL);
#ifdef TCI
  row++;
  w = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_size_request(w, -1, 3);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 7, 1);
  row++;
  w = gtk_label_new("TCI");
  gtk_widget_set_name(w, "boldlabel");
  gtk_widget_set_halign(w, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), w, 0, row, 1, 1);
  w = gtk_spin_button_new_with_range(1025, 65535, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), (double)tci_port);
  gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
  g_signal_connect(w, "value_changed", G_CALLBACK(tci_port_changed_cb), NULL);
  w = gtk_check_button_new_with_label("Enable");
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), tci_enable);
  gtk_widget_show(w);
  gtk_grid_attach(GTK_GRID(grid), w, 4, row, 1, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(tci_enable_cb), NULL);
  w = gtk_check_button_new_with_label("Report TX Frequency Only");
  gtk_widget_set_name(w, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), tci_txonly);
  gtk_widget_show(w);
  gtk_grid_attach(GTK_GRID(grid), w, 5, row, 3, 1);
  g_signal_connect(w, "toggled", G_CALLBACK(tci_txonly_changed_cb), NULL);
#endif
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}