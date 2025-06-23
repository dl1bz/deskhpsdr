/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
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
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <wdsp.h>             // only needed for GetWDSPVersion
#include <sys/utsname.h>

#include "new_menu.h"
#include "about_menu.h"
#include "discovered.h"
#include "radio.h"
#include "version.h"
#include "hpsdr_logo.h"

static GtkWidget *dialog = NULL;
static GtkWidget *label;

static GdkPixbuf *create_pixbuf_from_data() {
  GInputStream *mem_stream;
  GdkPixbuf *pixbuf, *scaled_pixbuf;
  GError *error = NULL;
  mem_stream = g_memory_input_stream_new_from_data(hpsdr_logo, hpsdr_logo_len, NULL);
  pixbuf = gdk_pixbuf_new_from_stream(mem_stream, NULL, &error);

  if (!pixbuf) {
    g_printerr("ERROR loading pic: %s\n", error->message);
    g_error_free(error);
    g_object_unref(mem_stream);
    return NULL;
  }

  // pic scaling
  scaled_pixbuf = gdk_pixbuf_scale_simple(pixbuf, 100, 100, GDK_INTERP_BILINEAR);
  g_object_unref(pixbuf);  // free original-pixbuf
  g_object_unref(mem_stream);
  return scaled_pixbuf;
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

void about_menu(GtkWidget *parent) {
  char text[2048];
  char line[512];
  struct utsname unameData;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  gtk_container_set_border_width(GTK_CONTAINER(dialog), 20); // 20px leer zwischen Fenster und Content
  char title[64];
  uname(&unameData);
  snprintf(title, 64, "%s - About", PGNAME);
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 4);
  int row = 0;
  /*
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, row, 1, 1);
  */
  row++;
  // load the TRX logo now only from the included hpsdr_logo.h
  GtkWidget *hpsdr_logo_widget = gtk_image_new_from_pixbuf(create_pixbuf_from_data());
  gtk_widget_set_halign(hpsdr_logo_widget, GTK_ALIGN_CENTER);  // Horizontal zentrieren
  gtk_widget_set_valign(hpsdr_logo_widget, GTK_ALIGN_START);   // Vertikal oben ausrichten
  gtk_grid_attach(GTK_GRID(grid), hpsdr_logo_widget, 0, row, 1, 1);
  snprintf(text, 2048, "Hamradio SDR-Software for HPSDR protocol 1 & 2 and SOAPY-API\n"
                       "deskHPSDR is developed by Heiko Amft, DL1BZ (dl1bz@bzsax.de)\n"
                       "(contains code portions of piHPSDR by G0ORX/N6LYT and DL1YCF)\n\n"
                       "    Credits:\n"
                       "    Warren C. Pratt, NR0V: WDSP Library\n"
                       "    John Melton, G0ORX/N6LYT: first and initial version of piHPSDR\n"
                       "    Christoph van Wüllen, DL1YCF: Continuation & current version piHPSDR\n"
                       "    Richie, MW0LGE: Developer of main version Thetis\n"
                       "    Reid, MI0BOT: Adaptation of Thetis for the Hermes Lite 2\n"
                       "    Ramakrishnan, VU3RDD: patched WDSP with NR3 and NR4\n\n"
                       "Build OS: %s %s @ %s\n"
                       "Build compiler: %s\n"
                       "Build date: %s (Branch: %s, Commit: %s)\n"
                       "Build version: %s\n"
                       "Build options: %s\n"
                       "WDSP version: %d.%02d\n\n",
           unameData.sysname, unameData.release, unameData.machine, __VERSION__, build_date, build_branch, build_commit,
           build_version,
           build_options, GetWDSPVersion() / 100, GetWDSPVersion() % 100);

  switch (radio->protocol) {
  case ORIGINAL_PROTOCOL:
  case NEW_PROTOCOL:
    if (device == DEVICE_OZY) {
      snprintf(line, 512, "Device:  OZY (via USB)  Protocol %s v%d.%d", radio->protocol == ORIGINAL_PROTOCOL ? "1" : "2",
               radio->software_version / 10, radio->software_version % 10);
      g_strlcat(text, line, 1024);
    } else {
      char interface_addr[64];
      char addr[64];
      g_strlcpy(addr, inet_ntoa(radio->info.network.address.sin_addr), 64);
      g_strlcpy(interface_addr, inet_ntoa(radio->info.network.interface_address.sin_addr), 64);

      if (have_saturn_xdma) {
        snprintf(line, 512, "Device: Saturn (via XDMA), Protocol %s, v%d.%d\n",
                 radio->protocol == ORIGINAL_PROTOCOL ? "1" : "2",
                 radio->software_version / 10, radio->software_version % 10);
      } else {
        snprintf(line, 512, "SDR Device: %s, Protocol %s, Firmware v%d.%d\n"
                            "    MAC address SDR: %02X:%02X:%02X:%02X:%02X:%02X\n"
                            "    IP address SDR: %s [on %s w/ local IP %s]",
                 radio->name, radio->protocol == ORIGINAL_PROTOCOL ? "1" : "2",
                 radio->software_version / 10, radio->software_version % 10,
                 radio->info.network.mac_address[0],
                 radio->info.network.mac_address[1],
                 radio->info.network.mac_address[2],
                 radio->info.network.mac_address[3],
                 radio->info.network.mac_address[4],
                 radio->info.network.mac_address[5],
                 addr,
                 radio->info.network.interface_name,
                 interface_addr);
      }

      g_strlcat(text, line, 1024);
    }

    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    snprintf(line, 512, "Device: %s (via SoapySDR)\n"
                        "    %s (%s)",
             radio->name, radio->info.soapy.hardware_key, radio->info.soapy.driver_key);
    g_strlcat(text, line, 1024);
    break;
#endif
  }

  label = gtk_label_new(text);
  gtk_widget_set_name(label, "smalllabel");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 1, row, 5, 1);
  row++;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  gtk_widget_set_margin_top(close_b, 20); // 20px Platz nach oben
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 2, row, 1, 1);
  // gtk_widget_set_halign(close_b, GTK_ALIGN_CENTER);  // Horizontal zentrieren
  // gtk_widget_set_valign(close_b, GTK_ALIGN_CENTER);   // Vertikal oben ausrichten
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
