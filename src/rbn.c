/*  Copyright (C)
*   2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
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

#include <glib.h>
#include <gtk/gtk.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <libtelnet.h>

#include "message.h"
#include "radio.h"
#include "rbn.h"
#include "rx_panadapter.h"

#define RBN_RECONNECT_SECONDS 30

typedef struct {
  telnet_t       *telnet;
  int             sockfd;
  GIOChannel     *gio;
  guint           io_watch_id;
  guint           reconnect_id;
  GString        *linebuf;
} RbnCtx;

static RbnCtx *g_rbn_ctx = NULL;

static const telnet_telopt_t rbn_telopts[] = {
  { TELNET_TELOPT_ECHO,      TELNET_WONT, TELNET_DO   },
  { TELNET_TELOPT_TTYPE,     TELNET_WILL, TELNET_DONT },
  { TELNET_TELOPT_COMPRESS2, TELNET_WONT, TELNET_DONT },
  { TELNET_TELOPT_MSSP,      TELNET_WONT, TELNET_DONT },
  { -1, 0, 0 }
};

static gboolean rbn_reconnect_cb(gpointer data);

static const char *rbn_login_call(void) {
  if (dxc_login[0] != '\0' && strcmp(dxc_login, "YOUR_CALLSIGN") != 0) {
    return dxc_login;
  }
  if (own_callsign[0] != '\0' && strcmp(own_callsign, "YOUR_CALLSIGN") != 0) {
    return own_callsign;
  }
  return "NOCALL";
}

static gboolean rbn_mode_allowed(const char *mode) {
  if (mode == NULL || mode[0] == '\0') {
    return FALSE;
  }
  if (g_ascii_strcasecmp(mode, "CW") == 0) {
    return rbn_filter_cw ? TRUE : FALSE;
  }
  if (g_ascii_strcasecmp(mode, "RTTY") == 0) {
    return rbn_filter_rtty ? TRUE : FALSE;
  }
  return FALSE;
}

static gboolean rbn_extract_mode(const char *p, char *mode, size_t mode_size) {
  char token[32];
  int token_count = 0;
  if (mode == NULL || mode_size == 0) {
    return FALSE;
  }
  mode[0] = '\0';
  while (p && *p && token_count < 12) {
    while (g_ascii_isspace((guchar) *p)) {
      p++;
    }
    if (!*p) {
      break;
    }
    int i = 0;
    while (*p && !g_ascii_isspace((guchar) *p) && i < (int) sizeof(token) - 1) {
      token[i++] = *p++;
    }
    token[i] = '\0';
    token_count++;
    if (g_ascii_strcasecmp(token, "CW") == 0 ||
        g_ascii_strcasecmp(token, "RTTY") == 0) {
      g_strlcpy(mode, token, mode_size);
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean rbn_line_has_cq_token(const char *line) {
  if (line == NULL || line[0] == '\0') {
    return FALSE;
  }
  const char *p = line;
  while (*p) {
    while (*p && !g_ascii_isalnum((guchar) *p)) {
      p++;
    }
    if (!*p) {
      break;
    }
    char token[32];
    int i = 0;
    while (*p && g_ascii_isalnum((guchar) *p) && i < (int) sizeof(token) - 1) {
      token[i++] = *p++;
    }
    token[i] = '\0';
    if (g_ascii_strcasecmp(token, "CQ") == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

static void rbn_process_line(const char *line) {
  const char *p;
  const char *needle_dx = "DX de ";
  if (!rbn_enabled || line == NULL || line[0] == '\0') {
    return;
  }
  p = strstr(line, needle_dx);
  if (!p) {
    return;
  }
  p += strlen(needle_dx);
  while (*p && *p != ':' && !g_ascii_isspace((guchar) *p)) {
    p++;
  }
  while (*p == ':' || g_ascii_isspace((guchar) *p)) {
    p++;
  }
  if (!*p) {
    return;
  }
  char *endptr = NULL;
  double freq_khz = g_ascii_strtod(p, &endptr);
  if (endptr == p || freq_khz <= 0.0) {
    return;
  }
  p = endptr;
  while (g_ascii_isspace((guchar) *p)) {
    p++;
  }
  if (!*p) {
    return;
  }
  char dxcall[32];
  int i = 0;
  while (*p && !g_ascii_isspace((guchar) *p) && i < (int) sizeof(dxcall) - 1) {
    dxcall[i++] = *p++;
  }
  dxcall[i] = '\0';
  if (i == 0) {
    return;
  }
  char mode[16];
  if (!rbn_extract_mode(p, mode, sizeof(mode))) {
    return;
  }
  if (!rbn_mode_allowed(mode)) {
    return;
  }
  if (rbn_filter_cq && !rbn_line_has_cq_token(line)) {
    return;
  }
  // t_print("RBN: spot %s %.1f kHz %s\n", mode, freq_khz, dxcall);
  pan_add_dx_spot_source(freq_khz, dxcall, PAN_SPOT_SOURCE_RBN);
}

static void rbn_feed_parser(RbnCtx *ctx, const char *data, size_t len) {
  if (!ctx || !ctx->linebuf || !data || len == 0) {
    return;
  }
  for (size_t i = 0; i < len; i++) {
    char c = data[i];
    if (c == '\r' || c == '\n') {
      if (ctx->linebuf->len > 0) {
        rbn_process_line(ctx->linebuf->str);
        g_string_truncate(ctx->linebuf, 0);
      }
      continue;
    }
    g_string_append_c(ctx->linebuf, c);
  }
}

static void rbn_schedule_reconnect(RbnCtx *ctx) {
  if (!ctx || !rbn_enabled || ctx->reconnect_id != 0) {
    return;
  }
  t_print("RBN: reconnect scheduled in %d seconds\n", RBN_RECONNECT_SECONDS);
  ctx->reconnect_id = g_timeout_add_seconds(RBN_RECONNECT_SECONDS, rbn_reconnect_cb, NULL);
}

void rbn_stop(void) {
  RbnCtx *ctx = g_rbn_ctx;
  if (!ctx) {
    return;
  }
  if (ctx->reconnect_id != 0) {
    g_source_remove(ctx->reconnect_id);
    ctx->reconnect_id = 0;
  }
  if (ctx->gio) {
    if (ctx->io_watch_id != 0) {
      g_source_remove(ctx->io_watch_id);
      ctx->io_watch_id = 0;
    }
    g_io_channel_shutdown(ctx->gio, TRUE, NULL);
    g_io_channel_unref(ctx->gio);
    ctx->gio = NULL;
  }
  if (ctx->telnet) {
    telnet_free(ctx->telnet);
    ctx->telnet = NULL;
  }
  if (ctx->sockfd >= 0) {
    close(ctx->sockfd);
    ctx->sockfd = -1;
  }
  if (ctx->linebuf) {
    g_string_free(ctx->linebuf, TRUE);
    ctx->linebuf = NULL;
  }
  g_free(ctx);
  g_rbn_ctx = NULL;
  t_print("RBN: stopped\n");
}

static int rbn_connect_tcp(const char *host, long int port) {
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  struct addrinfo *rp;
  int sock = -1;
  char port_str[16];
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(port_str, sizeof(port_str), "%ld", port);
  int rc = getaddrinfo(host, port_str, &hints, &res);
  if (rc != 0) {
    t_print("RBN: getaddrinfo(%s:%s) failed: %s\n", host, port_str, gai_strerror(rc));
    return -1;
  }
  for (rp = res; rp != NULL; rp = rp->ai_next) {
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock < 0) {
      continue;
    }
    if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
      break;
    }
    close(sock);
    sock = -1;
  }
  freeaddrinfo(res);
  if (sock < 0) {
    t_print("RBN: connect to %s:%s failed: %s\n", host, port_str, g_strerror(errno));
  }
  if (sock >= 0) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0) {
      fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }
  }
  return sock;
}

static void rbn_telnet_event_handler(telnet_t *telnet, telnet_event_t *ev, void *user_data) {
  RbnCtx *ctx = (RbnCtx *) user_data;
  switch (ev->type) {
  case TELNET_EV_DATA:
    rbn_feed_parser(ctx, ev->data.buffer, ev->data.size);
    break;
  case TELNET_EV_SEND:
    if (ctx && ctx->sockfd >= 0) {
      ssize_t wr = send(ctx->sockfd, ev->data.buffer, ev->data.size, 0);
      if (wr < 0) {
        t_print("RBN: send() failed: %s\n", g_strerror(errno));
      }
    }
    break;
  default:
    break;
  }
}

static gboolean rbn_socket_cb(GIOChannel *source, GIOCondition cond, gpointer data) {
  (void) source;
  RbnCtx *ctx = (RbnCtx *) data;
  if (!ctx) {
    return FALSE;
  }
  if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
    t_print("RBN: socket closed or error, reconnecting\n");
    rbn_stop();
    if (rbn_enabled) {
      rbn_start();
    }
    return FALSE;
  }
  if (cond & G_IO_IN) {
    char buf[4096];
    ssize_t n = recv(ctx->sockfd, buf, sizeof(buf), 0);
    if (n > 0) {
      telnet_recv(ctx->telnet, buf, (size_t) n);
      return TRUE;
    }
    if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      t_print("RBN: recv() failed or remote closed connection, reconnecting\n");
      rbn_stop();
      if (rbn_enabled) {
        rbn_start();
      }
      return FALSE;
    }
  }
  return TRUE;
}

void rbn_start(void) {
  if (!rbn_enabled) {
    rbn_stop();
    return;
  }
  if (g_rbn_ctx != NULL) {
    return;
  }
  t_print("RBN: connecting to %s:%ld\n", rbn_address, rbn_port);
  RbnCtx *ctx = g_new0(RbnCtx, 1);
  ctx->sockfd = -1;
  ctx->linebuf = g_string_new(NULL);
  ctx->sockfd = rbn_connect_tcp(rbn_address, rbn_port);
  if (ctx->sockfd < 0) {
    g_rbn_ctx = ctx;
    rbn_schedule_reconnect(ctx);
    return;
  }
  ctx->telnet = telnet_init(rbn_telopts, rbn_telnet_event_handler, 0, ctx);
  if (!ctx->telnet) {
    t_print("RBN: telnet_init failed\n");
    g_rbn_ctx = ctx;
    rbn_schedule_reconnect(ctx);
    return;
  }
  ctx->gio = g_io_channel_unix_new(ctx->sockfd);
  g_io_channel_set_encoding(ctx->gio, NULL, NULL);
  g_io_channel_set_buffered(ctx->gio, FALSE);
  ctx->io_watch_id = g_io_add_watch(ctx->gio,
                                    G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                    rbn_socket_cb,
                                    ctx);
  g_rbn_ctx = ctx;
  const char *login = rbn_login_call();
  t_print("RBN: connected to %s:%ld, login %s\n", rbn_address, rbn_port, login);
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "%s\n", login);
  telnet_send(ctx->telnet, cmd, strlen(cmd));
}

void rbn_update_from_settings(void) {
  if (rbn_enabled) {
    t_print("RBN: settings changed, restarting\n");
    rbn_stop();
    rbn_start();
  } else {
    rbn_stop();
  }
}

static gboolean rbn_reconnect_cb(gpointer data) {
  (void) data;
  if (g_rbn_ctx) {
    g_rbn_ctx->reconnect_id = 0;
  }
  rbn_stop();
  if (rbn_enabled) {
    t_print("RBN: reconnecting\n");
    rbn_start();
  }
  return FALSE;
}
