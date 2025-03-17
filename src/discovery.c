/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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
#include <gdk/gdk.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "discovered.h"
#include "old_discovery.h"
#include "new_discovery.h"
#ifdef SOAPYSDR
  #include "soapy_discovery.h"
#endif
#include "main.h"
#include "radio.h"
#ifdef USBOZY
  #include "ozyio.h"
#endif
#ifdef STEMLAB_DISCOVERY
  #include "stemlab_discovery.h"
#endif
#include "ext.h"
#include "gpio.h"
#ifdef GPIO
  #include "actions.h"
  #include "configure.h"
#endif
#include "protocols.h"
#include "property.h"
#include "message.h"
#if defined (__LDESK__)
  #include "new_menu.h"
#endif

static GtkWidget *discovery_dialog;
static DISCOVERED *d;

static GtkWidget *apps_combobox[MAX_DEVICES];

GtkWidget *tcpaddr;
#define IPADDR_LEN 20
static char ipaddr_buf[IPADDR_LEN] = "";
char *ipaddr_radio = &ipaddr_buf[0];

int discover_only_stemlab = 0;

int delayed_discovery(gpointer data);

static gboolean close_cb() {
  // There is nothing to clean up
  return TRUE;
}

static gboolean start_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  radio = (DISCOVERED *)data;
#ifdef STEMLAB_DISCOVERY

  // We need to start the STEMlab app before destroying the dialog, since
  // we otherwise lose the information about which app has been selected.
  if (radio->protocol == STEMLAB_PROTOCOL) {
    const int device_id = radio - discovered;

    if (radio->software_version & BARE_REDPITAYA) {
      // Start via the simple web interface
      (void) alpine_start_app(gtk_combo_box_get_active_id(GTK_COMBO_BOX(apps_combobox[device_id])));
    } else {
      // Start via the STEMlab "bazaar" interface
      (void) stemlab_start_app(gtk_combo_box_get_active_id(GTK_COMBO_BOX(apps_combobox[device_id])));
    }

    //
    // To make this bullet-proof, we do another "discover" now
    // and proceeding this way is the only way to choose between UDP and TCP connection
    // Since we have just started the app, we temporarily deactivate STEMlab detection
    //
    stemlab_cleanup();
    discover_only_stemlab = 1;
    gtk_widget_destroy(discovery_dialog);
    status_text("Wait for STEMlab app\n");
    g_timeout_add(2000, delayed_discovery, NULL);
    return TRUE;
  }

#endif
  //
  // Starting the radio via the GTK queue ensures quick update
  // of the status label
  //
  status_text("Starting Radio ...\n");
  g_timeout_add(10, ext_start_radio, NULL);
  gtk_widget_destroy(discovery_dialog);
  return TRUE;
}

static gboolean protocols_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  configure_protocols(discovery_dialog);
  return TRUE;
}

#ifdef GPIO
#ifdef GPIO_CONFIGURE_LINES
static gboolean gpio_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  configure_gpio(discovery_dialog);
  return TRUE;
}

#endif
#endif

static void gpio_changed_cb(GtkWidget *widget, gpointer data) {
  controller = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
#ifndef GPIO

  if (controller != G2_V2) {
    controller = NO_CONTROLLER;
    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), controller);
  }

#endif
  gpio_set_defaults(controller);
  gpioSaveState();
}

static gboolean discover_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  gtk_widget_destroy(discovery_dialog);
  g_timeout_add(100, delayed_discovery, NULL);
  return TRUE;
}

static gboolean exit_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  gtk_widget_destroy(discovery_dialog);
  _exit(0);
  return TRUE;
}

static gboolean radio_ip_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  struct sockaddr_in sa;
  int len;
  const char *cp;
  cp = gtk_entry_get_text(GTK_ENTRY(tcpaddr));
  len = strnlen(cp, IPADDR_LEN);

  if (len == 0) {
    // if the text entry field is empty, delete ip.addr
    unlink("ip.addr");
    return TRUE;
  }

  if (inet_pton(AF_INET, cp, &(sa.sin_addr)) != 1) {
    // if text is non-empty but also not a valid IP addr,
    // do nothing
    return TRUE;
  }

  strncpy(ipaddr_radio, cp, IPADDR_LEN);
  ipaddr_radio[IPADDR_LEN - 1] = 0;
  FILE *fp = fopen("ip.addr", "w");

  if (fp) {
    fprintf(fp, "%s\n", ipaddr_radio);
    fclose(fp);
  }

  return FALSE;
}

void discovery() {
  //
  // On the discovery screen, make the combo-boxes "touchscreen-friendly"
  //
  optimize_for_touchscreen = 1;
  protocolsRestoreState();
  selected_device = 0;
  devices = 0;
  // Try to locate IP addr
  FILE *fp = fopen("ip.addr", "r");

  if (fp) {
    (void) fgets(ipaddr_radio, IPADDR_LEN, fp);
    fclose(fp);
    ipaddr_radio[IPADDR_LEN - 1] = 0;
    // remove possible trailing newline char in ipaddr_radio
    int len = strnlen(ipaddr_radio, IPADDR_LEN);

    while (--len >= 0) {
      if (ipaddr_radio[len] != '\n') { break; }

      ipaddr_radio[len] = 0;
    }
  }

#ifdef USBOZY

  if (enable_usbozy && !discover_only_stemlab) {
    //
    // first: look on USB for an Ozy
    //
    status_text("Looking for USB based OZY devices");

    if (ozy_discover() != 0) {
      discovered[devices].protocol = ORIGINAL_PROTOCOL;
      discovered[devices].device = DEVICE_OZY;
      discovered[devices].software_version = 10;              // we can't know yet so this isn't a real response
      g_strlcpy(discovered[devices].name, "Ozy on USB", sizeof(discovered[devices].name));
      discovered[devices].frequency_min = 0.0;
      discovered[devices].frequency_max = 61440000.0;

      for (int i = 0; i < 6; i++) {
        discovered[devices].info.network.mac_address[i] = 0;
      }

      discovered[devices].status = STATE_AVAILABLE;
      discovered[devices].info.network.address_length = 0;
      discovered[devices].info.network.interface_length = 0;
      g_strlcpy(discovered[devices].info.network.interface_name, "USB",
              sizeof(discovered[devices].info.network.interface_name));
      discovered[devices].use_tcp = 0;
      discovered[devices].use_routing = 0;
      discovered[devices].supported_receivers = 2;
      t_print("discovery: found USB OZY device min=%0.3f MHz max=%0.3f MHz\n",
              discovered[devices].frequency_min * 1E-6,
              discovered[devices].frequency_max * 1E-6);
      devices++;
    }
  }

#endif
#ifdef SATURN
#include "saturnmain.h"

  if (enable_saturn_xdma && !discover_only_stemlab) {
    status_text("Looking for /dev/xdma* based saturn devices");
    saturn_discovery();
  }

#endif
#ifdef STEMLAB_DISCOVERY

  if (enable_stemlab && !discover_only_stemlab) {
    status_text("Looking for STEMlab WEB apps");
    stemlab_discovery();
  }

#endif

  if (enable_protocol_1 || discover_only_stemlab) {
    if (discover_only_stemlab) {
      status_text("Stemlab ... Looking for SDR apps");
    } else {
      status_text("Protocol 1 ... Discovering Devices");
    }

    old_discovery();
  }

  if (enable_protocol_2 && !discover_only_stemlab) {
    status_text("Protocol 2 ... Discovering Devices");
    new_discovery();
  }

#ifdef SOAPYSDR

  if (enable_soapy_protocol && !discover_only_stemlab) {
    status_text("SoapySDR ... Discovering Devices");
    soapy_discovery();
  }

#endif
  status_text("Discovery completed.");
  // subsequent discoveries check all protocols enabled.
  discover_only_stemlab = 0;
  t_print("discovery: found %d devices\n", devices);
  gdk_window_set_cursor(gtk_widget_get_window(top_window), gdk_cursor_new(GDK_ARROW));
  discovery_dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(discovery_dialog), GTK_WINDOW(top_window));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(discovery_dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
#if defined (__LDESK__)
  char _title[32];
  snprintf(_title, 32, "%s - Discovery", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
#else
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Discovery");
#endif
  g_signal_connect(discovery_dialog, "delete_event", G_CALLBACK(close_cb), NULL);
  g_signal_connect(discovery_dialog, "destroy", G_CALLBACK(close_cb), NULL);
  GtkWidget *content;
  content = gtk_dialog_get_content_area(GTK_DIALOG(discovery_dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  int row = 0;

  if (devices == 0) {
    GtkWidget *label = gtk_label_new("No local devices found!");
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 3, 1);
    row++;
  } else {
    char version[16];
    char text[512];
    char macStr[18];

    for (row = 0; row < devices; row++) {
      d = &discovered[row];
      t_print("Device Protocol=%d name=%s\n", d->protocol, d->name);
      snprintf(version, sizeof(version), "v%d.%d",
               d->software_version / 10,
               d->software_version % 10);
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               d->info.network.mac_address[0],
               d->info.network.mac_address[1],
               d->info.network.mac_address[2],
               d->info.network.mac_address[3],
               d->info.network.mac_address[4],
               d->info.network.mac_address[5]);

      switch (d->protocol) {
      case ORIGINAL_PROTOCOL:
      case NEW_PROTOCOL:
        if (d->device == DEVICE_OZY) {
          snprintf(text, sizeof(text), "%s (%s via USB)", d->name,
                   d->protocol == ORIGINAL_PROTOCOL ? "Protocol 1" : "Protocol 2");
        } else if (d->device == NEW_DEVICE_SATURN && strcmp(d->info.network.interface_name, "XDMA") == 0) {
          snprintf(text, sizeof(text), "%s (%s v%d) fpga:%x (%s) on /dev/xdma*", d->name,
                   d->protocol == ORIGINAL_PROTOCOL ? "Protocol 1" : "Protocol 2", d->software_version,
                   d->fpga_version, macStr);
        } else {
          snprintf(text, sizeof(text), "%s (%s %s) %s (%s) on %s: ",
                   d->name,
                   d->protocol == ORIGINAL_PROTOCOL ? "Protocol 1" : "Protocol 2",
                   version,
                   inet_ntoa(d->info.network.address.sin_addr),
                   macStr,
                   d->info.network.interface_name);
        }

        break;

      case SOAPYSDR_PROTOCOL:
#ifdef SOAPYSDR
        snprintf(text, sizeof(text), "%s (Protocol SOAPY_SDR %s) on %s", d->name, d->info.soapy.version, d->info.soapy.address);
#endif
        break;

      case STEMLAB_PROTOCOL:
        snprintf(text, sizeof(text), "Choose SDR App from %s: ",
                 inet_ntoa(d->info.network.address.sin_addr));
      }

      GtkWidget *label = gtk_label_new(text);
      gtk_widget_set_name(label, "boldlabel");
      gtk_widget_set_halign (label, GTK_ALIGN_START);
      gtk_widget_show(label);
      gtk_grid_attach(GTK_GRID(grid), label, 0, row, 3, 1);
      GtkWidget *start_button = gtk_button_new();
      gtk_widget_set_name(start_button, "big_txt");
      gtk_widget_show(start_button);
      gtk_grid_attach(GTK_GRID(grid), start_button, 3, row, 1, 1);
      g_signal_connect(start_button, "button-press-event", G_CALLBACK(start_cb), (gpointer)d);

      // if not available then cannot start it
      switch (d->status) {
      case STATE_AVAILABLE:
        gtk_button_set_label(GTK_BUTTON(start_button), "Start");
        break;

      case STATE_SENDING:
        gtk_button_set_label(GTK_BUTTON(start_button), "In Use");
        gtk_widget_set_sensitive(start_button, FALSE);
        break;

      case STATE_INCOMPATIBLE:
        gtk_button_set_label(GTK_BUTTON(start_button), "Incompatible");
        gtk_widget_set_sensitive(start_button, FALSE);
        break;
      }

      if (d->device != SOAPYSDR_USB_DEVICE) {
        int can_connect = 0;

        //
        // We can connect if
        //  a) either the computer or the radio have a self-assigned IP 169.254.xxx.yyy address
        //  b) we have a "routed" (TCP or UDP) connection to the radio
        //  c) radio and network address are in the same subnet
        //
        if (!strncmp(inet_ntoa(d->info.network.address.sin_addr), "169.254.", 8)) { can_connect = 1; }

        if (!strncmp(inet_ntoa(d->info.network.interface_address.sin_addr), "169.254.", 8)) { can_connect = 1; }

        if (d->use_routing) { can_connect = 1; }

        if ((d->info.network.interface_address.sin_addr.s_addr & d->info.network.interface_netmask.sin_addr.s_addr) ==
            (d->info.network.address.sin_addr.s_addr & d->info.network.interface_netmask.sin_addr.s_addr)) { can_connect = 1; }

        if (!can_connect) {
          gtk_button_set_label(GTK_BUTTON(start_button), "Subnet!");
          gtk_widget_set_sensitive(start_button, FALSE);
        }
      }

      if (d->protocol == STEMLAB_PROTOCOL) {
        if (d->software_version == 0) {
          gtk_button_set_label(GTK_BUTTON(start_button), "No SDR app found!");
          gtk_widget_set_sensitive(start_button, FALSE);
        } else {
          apps_combobox[row] = gtk_combo_box_text_new();

          // We want the default selection priority for the STEMlab app to be
          // RP-Trx > HAMlab-Trx > Pavel-Trx > Pavel-Rx, so we add in decreasing order and
          // always set the newly added entry to be active.
          if ((d->software_version & STEMLAB_PAVEL_RX) != 0) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(apps_combobox[row]),
                                      "sdr_receiver_hpsdr", "Pavel-Rx");
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(apps_combobox[row]),
                                        "sdr_receiver_hpsdr");
          }

          if ((d->software_version & STEMLAB_PAVEL_TRX) != 0) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(apps_combobox[row]),
                                      "sdr_transceiver_hpsdr", "Pavel-Trx");
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(apps_combobox[row]),
                                        "sdr_transceiver_hpsdr");
          }

          if ((d->software_version & HAMLAB_RP_TRX) != 0) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(apps_combobox[row]),
                                      "hamlab_sdr_transceiver_hpsdr", "HAMlab-Trx");
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(apps_combobox[row]),
                                        "hamlab_sdr_transceiver_hpsdr");
          }

          if ((d->software_version & STEMLAB_RP_TRX) != 0) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(apps_combobox[row]),
                                      "stemlab_sdr_transceiver_hpsdr", "STEMlab-Trx");
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(apps_combobox[row]),
                                        "stemlab_sdr_transceiver_hpsdr");
          }

          my_combo_attach(GTK_GRID(grid), apps_combobox[row], 4, row, 1, 1);
          gtk_widget_show(apps_combobox[row]);
        }
      }
    }
  }

  controller = NO_CONTROLLER;
  gpioRestoreState();
  gpio_set_defaults(controller);
  GtkWidget *gpio = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gpio), NULL, "No Controller");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gpio), NULL, "Controller1");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gpio), NULL, "Controller2 V1");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gpio), NULL, "Controller2 V2");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gpio), NULL, "G2 Front Panel");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gpio), NULL, "G2 Mk2 Panel");
  my_combo_attach(GTK_GRID(grid), gpio, 0, row, 1, 1);
  gtk_combo_box_set_active(GTK_COMBO_BOX(gpio), controller);
  g_signal_connect(gpio, "changed", G_CALLBACK(gpio_changed_cb), NULL);
  GtkWidget *discover_b = gtk_button_new_with_label("Discover");
  g_signal_connect (discover_b, "button-press-event", G_CALLBACK(discover_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), discover_b, 1, row, 1, 1);
  GtkWidget *protocols_b = gtk_button_new_with_label("Protocols");
  g_signal_connect (protocols_b, "button-press-event", G_CALLBACK(protocols_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), protocols_b, 2, row, 1, 1);
  row++;
#ifdef GPIO
#if 0
  GtkWidget *gpio_b = gtk_button_new_with_label("Configure GPIO");
  g_signal_connect (gpio_b, "button-press-event", G_CALLBACK(gpio_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), gpio_b, 0, row, 1, 1);
#endif
#endif
  GtkWidget *tcp_b = gtk_label_new("Radio IP Addr:");
  gtk_widget_set_name(tcp_b, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), tcp_b, 1, row, 1, 1);
  tcpaddr = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(tcpaddr), 20);
  gtk_grid_attach(GTK_GRID(grid), tcpaddr, 2, row, 1, 1);
  gtk_entry_set_text(GTK_ENTRY(tcpaddr), ipaddr_radio);
  g_signal_connect (tcpaddr, "changed", G_CALLBACK(radio_ip_cb), NULL);
  GtkWidget *exit_b = gtk_button_new_with_label("Exit");
  gtk_widget_set_name(exit_b, "close_button");
  g_signal_connect (exit_b, "button-press-event", G_CALLBACK(exit_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), exit_b, 3, row, 1, 1);
  gtk_container_add (GTK_CONTAINER (content), grid);
  gtk_widget_show_all(discovery_dialog);
  t_print("showing device dialog\n");
  //
  // Autostart and RedPitaya radios:
  //
  // Autostart means that if there only one device detected, start the
  // radio without the need to click the "Start" button.
  //
  // If this is the first round of a RedPitaya (STEMlab) discovery,
  // there may be more than one SDR app on the RedPitya available
  // from which one can choose one.
  // With autostart, there is no choice in this case, the SDR app
  // with highest priority is automatically chosen (and started),
  // and then the discovery process is re-initiated for RedPitya
  // devices only.
  //
  t_print("%s: devices=%d autostart=%d\n", __FUNCTION__, devices, autostart);

  if (devices == 1 && autostart) {
    d = &discovered[0];

    if (d->status == STATE_AVAILABLE) {
      if (start_cb(NULL, NULL, (gpointer)d)) { return; }
    }
  }
}

//
// Execute discovery() through g_timeout_add()
//
int delayed_discovery(gpointer data) {
  discovery();
  return G_SOURCE_REMOVE;
}
