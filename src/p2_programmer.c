/* Copyright (C)
* 2024-2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
* SPDX-License-Identifier: GPL-3.0-or-later
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

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __linux__
  #include <linux/if.h>
#endif

#include "p2_programmer.h"

#define P2_PROGRAM_PORT       1024
#define P2_SET_IP_LEN         115
#define P2_ERASE_LEN          115
#define P2_PROGRAM_LEN        265
#define P2_PROGRAM_DATA       256
#define P2_RX_TIMEOUT_SEC     2
#define P2_ERASE_TIMEOUT_SEC  30
#define P2_PROGRAM_RETRIES    5

struct p2_target {
  unsigned char mac[6];
  struct sockaddr_in address;
  struct sockaddr_in local_address;
  char interface_name[64];
};

typedef struct {
  GtkWidget *parent;
  GtkWidget *dialog;
  GtkWidget *ip_entry;
  GtkWidget *status_label;
  GtkWidget *progress;
  GtkWidget *set_ip_button;
  GtkWidget *dhcp_button;
  GtkWidget *program_button;
  GtkWidget *close_button;
  struct p2_target target;
  int busy;
} P2Programmer;

typedef enum {
  P2_UI_STATUS,
  P2_UI_PROGRESS,
  P2_UI_SUCCESS,
  P2_UI_ERROR
} P2UiEventType;

typedef struct {
  P2Programmer *ui;
  P2UiEventType type;
  double fraction;
  char text[256];
} P2UiEvent;

typedef struct {
  P2Programmer *ui;
  char *filename;
} P2ProgramJob;

static void p2_set_controls_sensitive(P2Programmer *ui, gboolean sensitive) {
  gtk_widget_set_sensitive(ui->ip_entry, sensitive);
  gtk_widget_set_sensitive(ui->set_ip_button, sensitive);
  gtk_widget_set_sensitive(ui->dhcp_button, sensitive);
  gtk_widget_set_sensitive(ui->program_button, sensitive);
  if (ui->close_button != NULL) {
    gtk_widget_set_sensitive(ui->close_button, sensitive);
  }
}

static void p2_error_dialog(GtkWindow *parent, const char *message) {
  GtkWidget *dialog = gtk_message_dialog_new(parent,
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_MESSAGE_ERROR,
    GTK_BUTTONS_OK,
    "%s", message);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

static void p2_success_and_exit(P2Programmer *ui, const char *message) {
  char text[512];
  snprintf(text, sizeof(text), "%s\n\nPower-cycle the SDR device before continuing.\n"
                               "deskHPSDR will now exit.", message);
  GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(ui->dialog),
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_MESSAGE_INFO,
    GTK_BUTTONS_OK,
    "%s", text);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  if (ui->parent != NULL) {
    gtk_widget_destroy(ui->parent);
  }
  exit(EXIT_SUCCESS);
}

static uint32_t p2_read_be32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) |
         ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) |
         (uint32_t)p[3];
}

static void p2_write_be32(unsigned char *p, uint32_t value) {
  p[0] = (unsigned char)(value >> 24);
  p[1] = (unsigned char)(value >> 16);
  p[2] = (unsigned char)(value >> 8);
  p[3] = (unsigned char)value;
}

static int p2_bind_socket(const struct p2_target *target, int timeout_sec,
                          int broadcast, char *error, size_t error_len) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    snprintf(error, error_len, "socket: %s", strerror(errno));
    return -1;
  }
  int optval = 1;
  (void) setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#ifdef SO_REUSEPORT
  (void) setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
#endif
  if (broadcast) {
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval)) < 0) {
      snprintf(error, error_len, "SO_BROADCAST: %s", strerror(errno));
      close(sock);
      return -1;
    }
  }
#ifdef __linux__
  if (target->interface_name[0] != '\0') {
    (void) setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE,
                      target->interface_name, strlen(target->interface_name));
  }
#endif
  struct sockaddr_in bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr = target->local_address.sin_addr;
  bind_addr.sin_port = htons(0);
  if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
    snprintf(error, error_len, "bind: %s", strerror(errno));
    close(sock);
    return -1;
  }
  if (timeout_sec > 0) {
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
      snprintf(error, error_len, "SO_RCVTIMEO: %s", strerror(errno));
      close(sock);
      return -1;
    }
  }
  return sock;
}

static int p2_set_ip(const struct p2_target *target, struct in_addr new_ip,
                     char *error, size_t error_len) {
  int sock = p2_bind_socket(target, 0, 1, error, error_len);
  if (sock < 0) {
    return -1;
  }
  unsigned char packet[P2_SET_IP_LEN];
  memset(packet, 0, sizeof(packet));
  packet[4] = 0x03;
  memcpy(&packet[5], target->mac, 6);
  memcpy(&packet[11], &new_ip.s_addr, 4);
  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(P2_PROGRAM_PORT);
  dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  ssize_t n = sendto(sock, packet, sizeof(packet), 0,
                     (struct sockaddr *)&dst, sizeof(dst));
  if (n != (ssize_t)sizeof(packet)) {
    snprintf(error, error_len, "Set-IP send failed: %s",
             n < 0 ? strerror(errno) : "short UDP datagram");
    close(sock);
    return -1;
  }
  close(sock);
  return 0;
}

static int p2_recv_reply(int sock, const struct p2_target *target,
                         unsigned char expected_type, uint32_t expected_seq,
                         char *error, size_t error_len) {
  unsigned char buf[2048];
  for (;;) {
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);
    ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&src, &srclen);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;
      }
      snprintf(error, error_len, "Program receive failed: %s", strerror(errno));
      return -1;
    }
    if (n < 5) {
      continue;
    }
    if (src.sin_addr.s_addr != target->address.sin_addr.s_addr) {
      continue;
    }
    if (buf[4] == expected_type && p2_read_be32(buf) == expected_seq) {
      return 1;
    }
  }
}

static void p2_post_event(P2Programmer *ui, P2UiEventType type,
                          double fraction, const char *text);

static gboolean p2_ui_event_cb(gpointer data) {
  P2UiEvent *event = (P2UiEvent *)data;
  P2Programmer *ui = event->ui;
  switch (event->type) {
  case P2_UI_STATUS:
    gtk_label_set_text(GTK_LABEL(ui->status_label), event->text);
    break;
  case P2_UI_PROGRESS:
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), event->fraction);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ui->progress), event->text);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ui->progress), TRUE);
    break;
  case P2_UI_SUCCESS:
    ui->busy = 0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), 1.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ui->progress), "100%");
    p2_success_and_exit(ui, event->text);
    break;
  case P2_UI_ERROR:
    ui->busy = 0;
    p2_set_controls_sensitive(ui, TRUE);
    gtk_label_set_text(GTK_LABEL(ui->status_label), "Programming failed.");
    p2_error_dialog(GTK_WINDOW(ui->dialog), event->text);
    break;
  }
  g_free(event);
  return G_SOURCE_REMOVE;
}

static void p2_post_event(P2Programmer *ui, P2UiEventType type,
                          double fraction, const char *text) {
  if (ui == NULL) {
    return;
  }
  P2UiEvent *event = g_new0(P2UiEvent, 1);
  event->ui = ui;
  event->type = type;
  event->fraction = fraction;
  g_strlcpy(event->text, text != NULL ? text : "", sizeof(event->text));
  g_idle_add(p2_ui_event_cb, event);
}

static int p2_program_rbf(P2Programmer *ui, const char *filename,
                          char *error, size_t error_len) {
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    snprintf(error, error_len, "%s: %s", filename, strerror(errno));
    return -1;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    snprintf(error, error_len, "Cannot seek RBF file: %s", strerror(errno));
    fclose(fp);
    return -1;
  }
  long file_size = ftell(fp);
  if (file_size <= 0) {
    snprintf(error, error_len, "RBF file is empty or its size cannot be determined.");
    fclose(fp);
    return -1;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    snprintf(error, error_len, "Cannot rewind RBF file: %s", strerror(errno));
    fclose(fp);
    return -1;
  }
  uint64_t blocks64 = ((uint64_t)file_size + P2_PROGRAM_DATA - 1) / P2_PROGRAM_DATA;
  if (blocks64 > UINT32_MAX) {
    snprintf(error, error_len, "RBF file is too large.");
    fclose(fp);
    return -1;
  }
  uint32_t blocks = (uint32_t)blocks64;
  int sock = p2_bind_socket(&ui->target, P2_ERASE_TIMEOUT_SEC, 0, error, error_len);
  if (sock < 0) {
    fclose(fp);
    return -1;
  }
  p2_post_event(ui, P2_UI_STATUS, 0.0, "Erasing FPGA flash...");
  unsigned char erase_packet[P2_ERASE_LEN];
  memset(erase_packet, 0, sizeof(erase_packet));
  erase_packet[4] = 0x04;
  if (sendto(sock, erase_packet, sizeof(erase_packet), 0,
             (const struct sockaddr *)&ui->target.address,
             sizeof(ui->target.address)) < 0) {
    snprintf(error, error_len, "Erase send failed: %s", strerror(errno));
    close(sock);
    fclose(fp);
    return -1;
  }
  for (int reply = 0; reply < 2; reply++) {
    int rc = p2_recv_reply(sock, &ui->target, 0x03, 0, error, error_len);
    if (rc == 0) {
      snprintf(error, error_len, "FPGA flash erase timed out.");
      close(sock);
      fclose(fp);
      return -1;
    }
    if (rc < 0) {
      close(sock);
      fclose(fp);
      return -1;
    }
  }
  p2_post_event(ui, P2_UI_STATUS, 0.0, "Erase complete. Programming...");
  struct timeval tv;
  tv.tv_sec = P2_RX_TIMEOUT_SEC;
  tv.tv_usec = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    snprintf(error, error_len, "SO_RCVTIMEO: %s", strerror(errno));
    close(sock);
    fclose(fp);
    return -1;
  }
  for (uint32_t seq = 0; seq < blocks; seq++) {
    unsigned char packet[P2_PROGRAM_LEN];
    memset(packet, 0, sizeof(packet));
    p2_write_be32(&packet[0], seq);
    packet[4] = 0x05;
    p2_write_be32(&packet[5], blocks);
    memset(&packet[9], 0xff, P2_PROGRAM_DATA);
    size_t nread = fread(&packet[9], 1, P2_PROGRAM_DATA, fp);
    if (nread == 0 && ferror(fp)) {
      snprintf(error, error_len, "RBF read failed: %s", strerror(errno));
      close(sock);
      fclose(fp);
      return -1;
    }
    int acknowledged = 0;
    for (int attempt = 0; attempt < P2_PROGRAM_RETRIES; attempt++) {
      if (sendto(sock, packet, sizeof(packet), 0,
                 (const struct sockaddr *)&ui->target.address,
                 sizeof(ui->target.address)) < 0) {
        snprintf(error, error_len, "Program send failed: %s", strerror(errno));
        close(sock);
        fclose(fp);
        return -1;
      }
      int rc = p2_recv_reply(sock, &ui->target, 0x04, seq, error, error_len);
      if (rc < 0) {
        close(sock);
        fclose(fp);
        return -1;
      }
      if (rc > 0) {
        acknowledged = 1;
        break;
      }
    }
    if (!acknowledged) {
      snprintf(error, error_len, "No acknowledgement for RBF block %u.", seq);
      close(sock);
      fclose(fp);
      return -1;
    }
    if (seq == 0 || seq + 1 == blocks || ((seq + 1) % 100U) == 0) {
      unsigned int percent =
              (unsigned int)(((uint64_t)(seq + 1) * 100U) / blocks);
      char progress[96];
      snprintf(progress, sizeof(progress), "%u%% (%u/%u)",
               percent, seq + 1, blocks);
      p2_post_event(ui, P2_UI_PROGRESS,
                    (double)(seq + 1) / (double)blocks, progress);
    }
  }
  close(sock);
  fclose(fp);
  return 0;
}

static gpointer p2_program_thread(gpointer data) {
  P2ProgramJob *job = (P2ProgramJob *)data;
  char error[256];
  error[0] = '\0';
  if (p2_program_rbf(job->ui, job->filename, error, sizeof(error)) == 0) {
    p2_post_event(job->ui, P2_UI_SUCCESS, 1.0, "FPGA programming completed successfully.");
  } else {
    p2_post_event(job->ui, P2_UI_ERROR, 0.0,
                  error[0] != '\0' ? error : "FPGA programming failed.");
  }
  g_free(job->filename);
  g_free(job);
  return NULL;
}

static void p2_set_ip_clicked(GtkButton *button, gpointer data) {
  (void)button;
  P2Programmer *ui = (P2Programmer *)data;
  const char *text = gtk_entry_get_text(GTK_ENTRY(ui->ip_entry));
  struct in_addr new_ip;
  if (inet_pton(AF_INET, text, &new_ip) != 1) {
    p2_error_dialog(GTK_WINDOW(ui->dialog), "Enter a valid IPv4 address.");
    return;
  }
  uint32_t host_ip = ntohl(new_ip.s_addr);
  uint8_t first_octet = (uint8_t)((host_ip >> 24) & 0xffU);
  uint8_t last_octet = (uint8_t)(host_ip & 0xffU);
  if (last_octet == 0U || last_octet == 255U) {
    p2_error_dialog(GTK_WINDOW(ui->dialog),
                    "Static IPv4 address must not end in .0 or .255.");
    return;
  }
  if (first_octet == 0U || first_octet == 127U || first_octet >= 224U) {
    p2_error_dialog(GTK_WINDOW(ui->dialog),
                    "IPv4 address is not valid as a static host address.");
    return;
  }
  char error[256];
  if (p2_set_ip(&ui->target, new_ip, error, sizeof(error)) != 0) {
    p2_error_dialog(GTK_WINDOW(ui->dialog), error);
    return;
  }
  p2_success_and_exit(ui, "Static IP address command sent successfully.");
}

static void p2_dhcp_clicked(GtkButton *button, gpointer data) {
  (void)button;
  P2Programmer *ui = (P2Programmer *)data;
  struct in_addr dhcp;
  dhcp.s_addr = htonl(INADDR_ANY);
  char error[256];
  if (p2_set_ip(&ui->target, dhcp, error, sizeof(error)) != 0) {
    p2_error_dialog(GTK_WINDOW(ui->dialog), error);
    return;
  }
  p2_success_and_exit(ui, "DHCP command sent successfully.");
}

static void p2_warning_agree_toggled(GtkToggleButton *toggle, gpointer data) {
  GtkWidget *ok_button = GTK_WIDGET(data);
  gtk_widget_set_sensitive(ok_button, gtk_toggle_button_get_active(toggle));
}

static void p2_program_clicked(GtkButton *button, gpointer data) {
  (void)button;
  P2Programmer *ui = (P2Programmer *)data;
  GtkFileChooserNative *chooser = gtk_file_chooser_native_new("Select Protocol 2 RBF file",
    GTK_WINDOW(ui->dialog),
    GTK_FILE_CHOOSER_ACTION_OPEN,
    "_Program",
    "_Cancel");
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "FPGA RBF files (*.rbf)");
  gtk_file_filter_add_pattern(filter, "*.rbf");
  gtk_file_filter_add_pattern(filter, "*.RBF");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
  if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser)) != GTK_RESPONSE_ACCEPT) {
    g_object_unref(chooser);
    return;
  }
  char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
  g_object_unref(chooser);
  if (filename == NULL) {
    return;
  }
  GtkWidget *warning = gtk_message_dialog_new(GTK_WINDOW(ui->dialog),
    GTK_DIALOG_MODAL,
    GTK_MESSAGE_WARNING,
    GTK_BUTTONS_NONE,
    "FPGA programming can render the SDR unusable if the process fails "
    "or an incorrect gateware file is used.\n\n"
    "Before proceeding, ask the manufacturer of your SDR device whether you have "
    "the correct .rbf file for your specific hardware.\n\n"
    "Important: Brick SDR devices have additional dependencies involving the onboard "
    "STM32 CPU. Its firmware must match the FPGA gateware and vice versa. "
    "The needed STM32 firmware cannot be programmed over the network; an ST-LINK programmer "
    "is required.\n\n"
    "Recovery may require an external FPGA programmer and a different "
    "type of gateware file. The .rbf file used for network programming "
    "cannot be used for recovery.\n\n"
    "FPGA flashing from virtual machines or other virtualized environments is NOT SUPPORTED!\n\n"
    "Proceed entirely at your own risk.");
  gtk_window_set_title(GTK_WINDOW(warning), "Warning: FPGA Programming");
  gtk_dialog_add_buttons(GTK_DIALOG(warning),
                         "_Cancel", GTK_RESPONSE_CANCEL,
                         "_OK", GTK_RESPONSE_OK,
                         NULL);
  GtkWidget *ok_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(warning), GTK_RESPONSE_OK);
  gtk_widget_set_sensitive(ok_button, FALSE);
  GtkWidget *agree = gtk_check_button_new_with_label("I understand and accept the risk.");
  gtk_widget_set_margin_top(agree, 8);
  gtk_widget_set_halign(agree, GTK_ALIGN_START);
  GtkWidget *message_area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(warning));
  gtk_box_pack_start(GTK_BOX(message_area), agree, FALSE, FALSE, 0);
  g_signal_connect(agree, "toggled", G_CALLBACK(p2_warning_agree_toggled), ok_button);
  gtk_widget_show(agree);
  gtk_dialog_set_default_response(GTK_DIALOG(warning), GTK_RESPONSE_CANCEL);
  gint response = gtk_dialog_run(GTK_DIALOG(warning));
  gtk_widget_destroy(warning);
  if (response != GTK_RESPONSE_OK) {
    g_free(filename);
    return;
  }
  ui->busy = 1;
  p2_set_controls_sensitive(ui, FALSE);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), 0.0);
  gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ui->progress), TRUE);
  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ui->progress), "0%");
  gtk_label_set_text(GTK_LABEL(ui->status_label), "Preparing FPGA programming...");
  P2ProgramJob *job = g_new0(P2ProgramJob, 1);
  job->ui = ui;
  job->filename = filename;
  GThread *thread = g_thread_new("p2-programmer", p2_program_thread, job);
  g_thread_unref(thread);
}

static gboolean p2_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
  (void)widget;
  (void)event;
  P2Programmer *ui = (P2Programmer *)data;
  return ui->busy ? TRUE : FALSE;
}

static void p2_dialog_destroyed(GtkWidget *widget, gpointer data) {
  (void)widget;
  P2Programmer *ui = (P2Programmer *)data;
  if (!ui->busy) {
    g_free(ui);
  }
}

static void p2_dialog_response(GtkDialog *dialog, gint response_id, gpointer data) {
  (void)response_id;
  P2Programmer *ui = (P2Programmer *)data;
  if (ui->busy) {
    return;
  }
  gtk_widget_destroy(GTK_WIDGET(dialog));
}

void p2_programmer_open(GtkWidget *parent, const DISCOVERED *radio) {
  if (parent == NULL || radio == NULL || radio->protocol != NEW_PROTOCOL) {
    return;
  }
  P2Programmer *ui = g_new0(P2Programmer, 1);
  ui->parent = parent;
  memcpy(ui->target.mac, radio->info.network.mac_address, 6);
  ui->target.address = radio->info.network.address;
  ui->target.address.sin_port = htons(P2_PROGRAM_PORT);
  ui->target.local_address = radio->info.network.interface_address;
  g_strlcpy(ui->target.interface_name, radio->info.network.interface_name,
            sizeof(ui->target.interface_name));
  ui->dialog = gtk_dialog_new_with_buttons("Protocol 2 Device Setup & Programming",
    GTK_WINDOW(parent),
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    "Close", GTK_RESPONSE_CLOSE,
    NULL);
  gtk_window_set_resizable(GTK_WINDOW(ui->dialog), FALSE);
  ui->close_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(ui->dialog), GTK_RESPONSE_CLOSE);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(ui->dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
  char mac[18];
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
           ui->target.mac[0], ui->target.mac[1], ui->target.mac[2],
           ui->target.mac[3], ui->target.mac[4], ui->target.mac[5]);
  char ip[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &ui->target.address.sin_addr, ip, sizeof(ip)) == NULL) {
    g_strlcpy(ip, "?", sizeof(ip));
  }
  GtkWidget *device_label = gtk_label_new("Device:");
  gtk_widget_set_halign(device_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), device_label, 0, 0, 1, 1);
  char device[128];
  snprintf(device, sizeof(device), "%s  %s", radio->name, mac);
  GtkWidget *device_value = gtk_label_new(device);
  gtk_widget_set_halign(device_value, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), device_value, 1, 0, 2, 1);
  GtkWidget *ip_label = gtk_label_new("IP Address:");
  gtk_widget_set_halign(ip_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), ip_label, 0, 1, 1, 1);
  ui->ip_entry = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(ui->ip_entry), ip);
  gtk_entry_set_width_chars(GTK_ENTRY(ui->ip_entry), 18);
  gtk_grid_attach(GTK_GRID(grid), ui->ip_entry, 1, 1, 1, 1);
  ui->set_ip_button = gtk_button_new_with_label("Set IP");
  gtk_widget_set_tooltip_text(ui->set_ip_button,
                              "Has only an effect, if the SDR device support this setting !\n"
                              "You will receive this information from the manufacturer.");
  gtk_grid_attach(GTK_GRID(grid), ui->set_ip_button, 2, 1, 1, 1);
  g_signal_connect(ui->set_ip_button, "clicked", G_CALLBACK(p2_set_ip_clicked), ui);
  GtkWidget *dhcp_label = gtk_label_new("DHCP:");
  gtk_widget_set_halign(dhcp_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), dhcp_label, 0, 2, 1, 1);
  ui->dhcp_button = gtk_button_new_with_label("(Re-)Enable DHCP");
  gtk_widget_set_tooltip_text(ui->dhcp_button,
                              "Has only an effect, if the SDR device support this setting !\n"
                              "You will receive this information from the manufacturer.");
  gtk_widget_set_halign(ui->dhcp_button, GTK_ALIGN_FILL);
  gtk_grid_attach(GTK_GRID(grid), ui->dhcp_button, 1, 2, 2, 1);
  g_signal_connect(ui->dhcp_button, "clicked", G_CALLBACK(p2_dhcp_clicked), ui);
  GtkWidget *program_label = gtk_label_new("FPGA:");
  gtk_widget_set_halign(program_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), program_label, 0, 3, 1, 1);
  ui->program_button = gtk_button_new_with_label("Flash FPGA code (*.rbf)");
  gtk_widget_set_tooltip_text(ui->program_button,
                              "Flashing FPGA gateware over network into your SDR device.\n"
                              "(if your SDR device support this, ask the manufacturer)");
  gtk_widget_set_halign(ui->program_button, GTK_ALIGN_FILL);
  gtk_grid_attach(GTK_GRID(grid), ui->program_button, 1, 3, 2, 1);
  g_signal_connect(ui->program_button, "clicked", G_CALLBACK(p2_program_clicked), ui);
  ui->status_label = gtk_label_new("Ready.");
  gtk_widget_set_halign(ui->status_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), ui->status_label, 0, 4, 3, 1);
  ui->progress = gtk_progress_bar_new();
  gtk_widget_set_hexpand(ui->progress, TRUE);
  gtk_grid_attach(GTK_GRID(grid), ui->progress, 0, 5, 3, 1);
  gtk_container_add(GTK_CONTAINER(content), grid);
  g_signal_connect(ui->dialog, "delete-event", G_CALLBACK(p2_delete_event), ui);
  g_signal_connect(ui->dialog, "destroy", G_CALLBACK(p2_dialog_destroyed), ui);
  g_signal_connect(ui->dialog, "response", G_CALLBACK(p2_dialog_response), ui);
  gtk_widget_show_all(ui->dialog);
}
