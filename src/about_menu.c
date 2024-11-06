/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
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
#include "mystring.h"

static GtkWidget *dialog = NULL;
static GtkWidget *label;

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
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, row, 1, 1);
  row++;
  snprintf(text, 1024, "Hamradio SDR-Software for HPSDR protocol 1 & 2\n"
           "Based on source code of piHPSDR developed by G0ORX/N6LYT and DL1YCF\n"
           "This version is developed and maintained by DL1BZ\n\n"
           "    Credits:\n"
           "    John Melton G0ORX/N6LYT: first and initial version of piHPSDR\n"
           "    Steve Wilson, KA6S: RIGCTL (CAT over TCP)\n"
           "    Laurence Barker, G8NJJ: USB OZY Support\n"
           //"    Johan Maas, PA3GSB: RadioBerry support\n"
           "    Ken Hopper, N9VV: Testing and Documentation\n"
           "    Christoph van Wüllen, DL1YCF: CW, PureSignal, Diversity, MIDI, CFC, DEXP  \n"
           "    Heiko Amft, DL1BZ: Improvements of GUI, TX Audio tools and macOS\n\n"
           "Build system: %s %s @ %s\n"
           "Build date: %s (commit %s)\n"
           "Build version: %s\n"
           "WDSP version: %d.%02d\n\n",
           unameData.sysname, unameData.release, unameData.machine, build_date, build_commit, build_version, GetWDSPVersion() / 100, GetWDSPVersion() % 100);

  switch (radio->protocol) {
  case ORIGINAL_PROTOCOL:
  case NEW_PROTOCOL:
    if (device == DEVICE_OZY) {
      snprintf(line, 512, "Device:  OZY (via USB)  Protocol %s v%d.%d", radio->protocol == ORIGINAL_PROTOCOL ? "1" : "2",
               radio->software_version / 10, radio->software_version % 10);
      STRLCAT(text, line, 1024);
    } else {
      char interface_addr[64];
      char addr[64];
      STRLCPY(addr, inet_ntoa(radio->info.network.address.sin_addr), 64);
      STRLCPY(interface_addr, inet_ntoa(radio->info.network.interface_address.sin_addr), 64);

      if (have_saturn_xdma) {
        snprintf(line, 512, "Device: Saturn (via XDMA), Protocol %s, v%d.%d\n",
                 radio->protocol == ORIGINAL_PROTOCOL ? "1" : "2",
                 radio->software_version / 10, radio->software_version % 10);
      } else {
        snprintf(line, 512, "Device: %s, Protocol %s, v%d.%d\n"
                 "    Mac Address: %02X:%02X:%02X:%02X:%02X:%02X\n"
                 "    IP Address: %s on %s (%s)",
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

      STRLCAT(text, line, 1024);
    }

    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    snprintf(line, 512, "Device: %s (via SoapySDR)\n"
             "    %s (%s)",
             radio->name, radio->info.soapy.hardware_key, radio->info.soapy.driver_key);
    STRLCAT(text, line, 1024);
    break;
#endif
  }

  label = gtk_label_new(text);
  gtk_widget_set_name(label, "small_button");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 1, row, 5, 1);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
