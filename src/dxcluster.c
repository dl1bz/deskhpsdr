/* Copyright (C)
* 2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
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

// dxcluster.c
// DX-Cluster-Client-Fenster (GTK3 + libtelnet)

#include <gtk/gtk.h>
#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>

#include <libtelnet.h>
#include "dxcluster.h"
#include "radio.h"

typedef struct {
  GtkWidget      *window;
  GtkWidget      *text_view;
  GtkTextBuffer  *text_buffer;
  GtkWidget      *entry;
  telnet_t       *telnet;
  int             sockfd;
  GIOChannel     *gio;
  GtkTextTag     *tag_dx;
  GtkTextTag     *tag_self;
  char           *callsign;
  guint           io_watch_id;   /* GSource-ID von g_io_add_watch */
} DxClusterCtx;

/* Singleton-Kontext */
static DxClusterCtx *g_dxcluster_ctx = NULL;

static const telnet_telopt_t telopts[] = {
  { TELNET_TELOPT_ECHO,      TELNET_WONT, TELNET_DO   },
  { TELNET_TELOPT_TTYPE,     TELNET_WILL, TELNET_DONT },
  { TELNET_TELOPT_COMPRESS2, TELNET_WONT, TELNET_DONT },
  { TELNET_TELOPT_MSSP,      TELNET_WONT, TELNET_DONT },
  { -1, 0, 0 }
};

/* -------------------------------------------------------------------------- */

static void
dxcluster_append_text(DxClusterCtx *ctx, const char *data, size_t len) {
  GtkTextIter start_before, end_after;
  GtkTextMark *mark;
  gtk_text_buffer_get_end_iter(ctx->text_buffer, &start_before);
  gint start_offset = gtk_text_iter_get_offset(&start_before);
  gtk_text_buffer_insert(ctx->text_buffer, &start_before, data, (gint)len);
  gtk_text_buffer_get_end_iter(ctx->text_buffer, &end_after);
  gint end_offset = gtk_text_iter_get_offset(&end_after);
  GtkTextIter range_start, range_end;
  gtk_text_buffer_get_iter_at_offset(ctx->text_buffer, &range_start, start_offset);
  gtk_text_buffer_get_iter_at_offset(ctx->text_buffer, &range_end,   end_offset);
  char *segment = gtk_text_buffer_get_text(
                    ctx->text_buffer, &range_start, &range_end, FALSE);

  if (segment && *segment) {
    const char *p;
    const char *needle_dx   = "DX de ";
    const char *needle_self = ctx->callsign ? ctx->callsign : "";
    /* „DX de“ einfärben */
    p = segment;

    while ((p = strstr(p, needle_dx)) != NULL) {
      gint rel_start = (gint)(p - segment);
      gint rel_end   = rel_start + (gint)strlen(needle_dx);
      GtkTextIter tag_start, tag_end;
      gtk_text_buffer_get_iter_at_offset(ctx->text_buffer,
                                         &tag_start,
                                         start_offset + rel_start);
      gtk_text_buffer_get_iter_at_offset(ctx->text_buffer,
                                         &tag_end,
                                         start_offset + rel_end);
      gtk_text_buffer_apply_tag(ctx->text_buffer, ctx->tag_dx,
                                &tag_start, &tag_end);
      p += strlen(needle_dx);
    }

    /* eigenes Rufzeichen hervorheben */
    if (needle_self[0] != '\0') {
      p = segment;

      while ((p = strstr(p, needle_self)) != NULL) {
        gint rel_start = (gint)(p - segment);
        gint rel_end   = rel_start + (gint)strlen(needle_self);
        GtkTextIter tag_start, tag_end;
        gtk_text_buffer_get_iter_at_offset(ctx->text_buffer,
                                           &tag_start,
                                           start_offset + rel_start);
        gtk_text_buffer_get_iter_at_offset(ctx->text_buffer,
                                           &tag_end,
                                           start_offset + rel_end);
        gtk_text_buffer_apply_tag(ctx->text_buffer, ctx->tag_self,
                                  &tag_start, &tag_end);
        p += strlen(needle_self);
      }
    }
  }

  g_free(segment);
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(ctx->text_buffer, &end);
  mark = gtk_text_buffer_create_mark(ctx->text_buffer, NULL, &end, FALSE);
  gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(ctx->text_view), mark);
  gtk_text_buffer_delete_mark(ctx->text_buffer, mark);
}

/* -------------------------------------------------------------------------- */

static void
dxcluster_disconnect(DxClusterCtx *ctx) {
  if (!ctx) {
    return;
  }

  if (ctx->gio) {
    g_io_channel_shutdown(ctx->gio, FALSE, NULL);
    g_io_channel_unref(ctx->gio);
    ctx->gio = NULL;
  }

  if (ctx->sockfd >= 0) {
    close(ctx->sockfd);
    ctx->sockfd = -1;
  }

  if (ctx->telnet) {
    telnet_free(ctx->telnet);
    ctx->telnet = NULL;
  }
}

/* -------------------------------------------------------------------------- */

static void
dxcluster_telnet_event_handler(telnet_t *telnet, telnet_event_t *ev, void *user_data) {
  DxClusterCtx *ctx = (DxClusterCtx *)user_data;
  (void)telnet;

  switch (ev->type) {
  case TELNET_EV_DATA:
    dxcluster_append_text(ctx, ev->data.buffer, ev->data.size);
    break;

  case TELNET_EV_SEND: {
    ssize_t rs = send(ctx->sockfd, ev->data.buffer, ev->data.size, 0);

    if (rs < 0) {
      g_warning("send() failed: %s", g_strerror(errno));
    }

    break;
  }

  case TELNET_EV_WARNING:
    g_warning("libtelnet warning: %s", ev->error.msg);
    break;

  case TELNET_EV_ERROR:
    g_warning("libtelnet error: %s", ev->error.msg);
    break;

  default:
    break;
  }
}

/* -------------------------------------------------------------------------- */

static gboolean
dxcluster_socket_cb(GIOChannel *source, GIOCondition cond, gpointer data) {
  DxClusterCtx *ctx = (DxClusterCtx *)data;
  int fd = g_io_channel_unix_get_fd(source);

  if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
    dxcluster_append_text(ctx, "\n[Verbindung geschlossen]\n",
                          strlen("\n[Verbindung geschlossen]\n"));
    dxcluster_disconnect(ctx);
    ctx->io_watch_id = 0;
    return FALSE;   /* Watch entfernen */
  }

  if ((cond & G_IO_IN) && ctx->telnet) {
    char buf[2048];

    for (;;) {
      ssize_t len = recv(fd, buf, sizeof(buf), 0);

      if (len > 0) {
        if (ctx->telnet) {
          telnet_recv(ctx->telnet, buf, len);
        }
      } else if (len == 0) {
        dxcluster_append_text(ctx,
                              "\n[Server hat die Verbindung beendet]\n",
                              strlen("\n[Server hat die Verbindung beendet]\n"));
        dxcluster_disconnect(ctx);
        ctx->io_watch_id = 0;
        return FALSE;
      } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        }

        g_warning("recv() failed: %s", g_strerror(errno));
        dxcluster_disconnect(ctx);
        ctx->io_watch_id = 0;
        return FALSE;
      }
    }
  }

  return TRUE;
}

/* -------------------------------------------------------------------------- */

static int
dxcluster_connect_tcp(const char *host, const char *port) {
  struct addrinfo hints;
  struct addrinfo *res = NULL, *rp;
  int sock = -1;
  int ret;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (!host || !port) {
    return -1;
  }

  if ((ret = getaddrinfo(host, port, &hints, &res)) != 0) {
    fprintf(stderr, "getaddrinfo(%s:%s): %s\n",
            host, port, gai_strerror(ret));
    return -1;
  }

  for (rp = res; rp != NULL; rp = rp->ai_next) {
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

    if (sock == -1) {
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
    fprintf(stderr, "Verbindung zu %s:%s fehlgeschlagen\n", host, port);
    return -1;
  }

  int flags = fcntl(sock, F_GETFL, 0);

  if (flags != -1) {
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  }

  return sock;
}

/* -------------------------------------------------------------------------- */

static void
dxcluster_on_entry_activate(GtkEntry *entry, gpointer user_data) {
  DxClusterCtx *ctx = (DxClusterCtx *)user_data;
  const gchar *text = gtk_entry_get_text(entry);

  if (ctx->telnet && ctx->sockfd >= 0 && text && *text) {
    gchar *line = g_strdup_printf("%s\r\n", text);
    telnet_send(ctx->telnet, line, strlen(line));
    g_free(line);
  }

  gtk_entry_set_text(entry, "");
}

static void
dxcluster_on_window_destroy(GtkWidget *widget, gpointer user_data) {
  dxcwin_open = 0;
  DxClusterCtx *ctx = (DxClusterCtx *)user_data;
  (void)widget;

  if (ctx->io_watch_id != 0) {
    g_source_remove(ctx->io_watch_id);
    ctx->io_watch_id = 0;
  }

  dxcluster_disconnect(ctx);

  if (ctx->callsign) {
    g_free(ctx->callsign);
  }

  if (g_dxcluster_ctx == ctx) {
    g_dxcluster_ctx = NULL;
  }

  g_free(ctx);
}

gboolean
dxcluster_on_window_configure(GtkWidget *widget,
                              GdkEventConfigure *event,
                              gpointer user_data) {
  dxcwin_x = event->x;
  dxcwin_y = event->y;
  dxcwin_w = event->width;
  dxcwin_h = event->height;
  return FALSE;
}

/* -------------------------------------------------------------------------- */
/*                        öffentliche API-Funktion                            */
/* -------------------------------------------------------------------------- */

void
dxcluster_open_window(const char *host,
                      long int portaddress,
                      const char *callsign,
                      int width,
                      int height,
                      int pos_x,
                      int pos_y) {
  /* Input-Validierung */
  if (!host ||
      !callsign || callsign[0] == '\0' ||
      portaddress < 1 || portaddress > 65535) {
    return;
  }

  char port[16];
  snprintf(port, sizeof(port), "%ld", portaddress);

  /* Wenn Fenster schon existiert: nur nach vorne holen / neu positionieren */
  if (g_dxcluster_ctx && GTK_IS_WINDOW(g_dxcluster_ctx->window)) {
    GtkWindow *w = GTK_WINDOW(g_dxcluster_ctx->window);

    if (width > 0 && height > 0) {
      gtk_window_resize(w, width, height);
    }

    if (pos_x >= 0 && pos_y >= 0) {
      gtk_window_move(w, pos_x, pos_y);
    }

    gtk_window_present(w);
    return;
  }

  DxClusterCtx *ctx = g_new0(DxClusterCtx, 1);
  ctx->sockfd   = -1;
  ctx->callsign = g_strdup(callsign);
  GtkWidget *window   = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  GtkWidget *vbox     = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
  GtkWidget *textview = gtk_text_view_new();
  GtkWidget *entry    = gtk_entry_new();
  ctx->window      = window;
  ctx->text_view   = textview;
  ctx->text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
  ctx->entry       = entry;
#ifdef __linux__
  /* Styleklassen, CSS kommt aus deskHPSDR */
  gtk_style_context_add_class(
    gtk_widget_get_style_context(textview),
    "dxcluster-textview-linux");
  gtk_style_context_add_class(
    gtk_widget_get_style_context(entry),
    "dxcluster-entry-linux");
#else
  /* Styleklassen, CSS kommt aus deskHPSDR */
  gtk_style_context_add_class(
    gtk_widget_get_style_context(textview),
    "dxcluster-textview");
  gtk_style_context_add_class(
    gtk_widget_get_style_context(entry),
    "dxcluster-entry");
#endif
  /* Farbtags */
  ctx->tag_dx = gtk_text_buffer_create_tag(
                  ctx->text_buffer, "dxspot",
                  "foreground", "green",
                  NULL);
  ctx->tag_self = gtk_text_buffer_create_tag(
                    ctx->text_buffer, "selfcall",
                    "foreground", "red",
                    "weight", PANGO_WEIGHT_BOLD,
                    NULL);
  gtk_container_add(GTK_CONTAINER(window), vbox);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(scrolled), textview);
  gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);

  if (width <= 0) { width  = 800; }

  if (height <= 0) { height = 600; }

  gtk_window_set_default_size(GTK_WINDOW(window), width, height);

  if (pos_x >= 0 && pos_y >= 0) {
    gtk_window_move(GTK_WINDOW(window), pos_x, pos_y);
  }

  char title[256];
  snprintf(title, sizeof(title),
           "DX-Cluster %s:%ld (%s)", host, portaddress, callsign);
  gtk_window_set_title(GTK_WINDOW(window), title);
  g_signal_connect(window, "destroy",
                   G_CALLBACK(dxcluster_on_window_destroy), ctx);
  g_signal_connect(entry, "activate",
                   G_CALLBACK(dxcluster_on_entry_activate), ctx);
  g_signal_connect(window, "configure-event",
                   G_CALLBACK(dxcluster_on_window_configure), NULL);
  gtk_widget_show_all(window);
  dxcwin_open = 1;
  /* Singleton-Kontext setzen */
  g_dxcluster_ctx = ctx;
  /* verbinden */
  ctx->sockfd = dxcluster_connect_tcp(host, port);

  if (ctx->sockfd < 0) {
    dxcluster_append_text(ctx,
                          "Konnte keine Verbindung zum DX-Cluster herstellen.\n",
                          strlen("Konnte keine Verbindung zum DX-Cluster herstellen.\n"));
    return;
  }

  /* libtelnet */
  ctx->telnet = telnet_init(telopts, dxcluster_telnet_event_handler, 0, ctx);

  if (!ctx->telnet) {
    fprintf(stderr, "telnet_init fehlgeschlagen\n");
    dxcluster_disconnect(ctx);
    return;
  }

  /* Auto-login */
  {
    char *login = g_strdup_printf("%s\r\n", callsign);
    telnet_send(ctx->telnet, login, strlen(login));
    g_free(login);
  }
  /* GIO-Watch */
  ctx->gio = g_io_channel_unix_new(ctx->sockfd);
  g_io_channel_set_encoding(ctx->gio, NULL, NULL);
  g_io_channel_set_buffered(ctx->gio, FALSE);
  ctx->io_watch_id = g_io_add_watch(
                       ctx->gio,
                       (GIOCondition)(G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL),
                       dxcluster_socket_cb,
                       ctx);
  dxcluster_append_text(ctx,
                        "[Verbunden zum DX-Cluster]\n",
                        strlen("[Verbunden zum DX-Cluster]\n"));
}
