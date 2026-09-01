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

#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

#include "audio.h"
#include "buffer_monitor.h"
#include "main.h"
#include "new_protocol.h"
#include "radio.h"

#define BUFFER_MONITOR_WIDTH   390
#define BUFFER_MONITOR_ROW_H    48
#define BUFFER_MONITOR_MARGIN   12
#define BUFFER_MONITOR_REFRESH 250
#define BUFFER_MONITOR_MAX_ROWS 12

typedef struct {
  char name[32];
  char value[64];
  double fraction;
  double current;
  double minimum;
  double maximum;
  int have_history;
  int valid;
} BUFFER_MONITOR_ROW;

static GtkWidget *buffer_monitor_window = NULL;
static GtkWidget *buffer_monitor_area = NULL;
static guint buffer_monitor_timer_id = 0;
static BUFFER_MONITOR_ROW rows[BUFFER_MONITOR_MAX_ROWS];
static int row_count = 0;
static double rx_buffered_latency_ms = 0.0;
static int have_rx_buffered_latency = 0;

static void row_update(int index,
                       const char *name,
                       const char *value,
                       double current,
                       double fraction,
                       int valid) {
  if (index < 0 || index >= BUFFER_MONITOR_MAX_ROWS) {
    return;
  }
  BUFFER_MONITOR_ROW *row = &rows[index];
  if (g_strcmp0(row->name, name) != 0) {
    memset(row, 0, sizeof(*row));
  }
  g_strlcpy(row->name, name, sizeof(row->name));
  g_strlcpy(row->value, value, sizeof(row->value));
  row->valid = valid;
  row->fraction = fraction;
  if (row->fraction < 0.0) {
    row->fraction = 0.0;
  } else if (row->fraction > 1.0) {
    row->fraction = 1.0;
  }
  if (valid) {
    row->current = current;
    if (!row->have_history) {
      row->minimum = current;
      row->maximum = current;
      row->have_history = 1;
    } else {
      if (current < row->minimum) {
        row->minimum = current;
      }
      if (current > row->maximum) {
        row->maximum = current;
      }
    }
  }
}

static void buffer_monitor_collect(void) {
  int n = 0;
  rx_buffered_latency_ms = 0.0;
  have_rx_buffered_latency = 0;
  if (protocol == NEW_PROTOCOL) {
    for (int ddc = 0; ddc < MAX_DDC && n < BUFFER_MONITOR_MAX_ROWS; ddc++) {
      P2_BUFFER_DIAG diag;
      if (!new_protocol_get_buffer_diag(ddc, &diag) || !diag.active) {
        continue;
      }
      if (diag.jitter_enabled && n < BUFFER_MONITOR_MAX_ROWS) {
        char name[32];
        char value[64];
        g_snprintf(name, sizeof(name), "P2 Jitter DDC%d", ddc);
        if (diag.jitter_ms > 0.0) {
          g_snprintf(value, sizeof(value), "%.1f ms / %.0f ms",
                     diag.jitter_ms, diag.jitter_target_ms);
          double scale = diag.jitter_target_ms > 0.0
                         ? diag.jitter_target_ms * 1.5 : 1.0;
          row_update(n++, name, value, diag.jitter_ms,
                     diag.jitter_ms / scale, 1);
          if (ddc == 0) {
            rx_buffered_latency_ms += diag.jitter_ms;
            have_rx_buffered_latency = 1;
          }
        } else {
          g_snprintf(value, sizeof(value), "%u / %u packets",
                     diag.jitter_queued, diag.jitter_capacity);
          row_update(n++, name, value, (double)diag.jitter_queued,
                     (double)diag.jitter_queued / (double)diag.jitter_capacity, 1);
        }
      }
      if (n < BUFFER_MONITOR_MAX_ROWS) {
        char name[32];
        char value[64];
        g_snprintf(name, sizeof(name), "RX IQ DDC%d", ddc);
        g_snprintf(value, sizeof(value), "%u / %u   peak %u",
                   diag.rxiq_queued, diag.rxiq_capacity, diag.rxiq_peak);
        row_update(n++, name, value, (double)diag.rxiq_peak,
                   (double)diag.rxiq_peak / (double)diag.rxiq_capacity, 1);
      }
    }
  }
  for (int rx = 0; rx < receivers && n < BUFFER_MONITOR_MAX_ROWS; rx++) {
    if (receiver[rx] == NULL) {
      continue;
    }
    AUDIO_BUFFER_DIAG diag;
    if (audio_get_rx_buffer_diag(receiver[rx], &diag) && diag.available) {
      char name[32];
      char value[64];
      double ms = (double)diag.queued * 1000.0 / 48000.0;
      double target_ms = (double)diag.target * 1000.0 / 48000.0;
      double scale_samples = diag.high > 0 ? (double)diag.high : (double)diag.capacity;
#ifdef COREAUDIO
      g_snprintf(name, sizeof(name), "RX%d CoreAudio", rx + 1);
#elif defined(PULSEAUDIO)
      g_snprintf(name, sizeof(name), "RX%d PulseAudio", rx + 1);
#else
      g_snprintf(name, sizeof(name), "RX%d ALSA", rx + 1);
#endif
      if (diag.target > 0) {
        g_snprintf(value, sizeof(value), "%.1f ms / %.0f ms", ms, target_ms);
      } else {
        g_snprintf(value, sizeof(value), "%.1f ms", ms);
      }
      row_update(n++, name, value, ms,
                 (double)diag.queued / scale_samples, 1);
#ifdef COREAUDIO
      if (n < BUFFER_MONITOR_MAX_ROWS) {
        char corr_name[32];
        char corr_value[64];
        g_snprintf(corr_name, sizeof(corr_name), "RX%d Corrections", rx + 1);
        g_snprintf(corr_value, sizeof(corr_value), "LOW %u   HIGH %u",
                   diag.low_corrections, diag.high_corrections);
        row_update(n++, corr_name, corr_value,
                   (double)(diag.low_corrections + diag.high_corrections),
                   0.0, 0);
      }
#endif
      if (rx == 0) {
        rx_buffered_latency_ms += ms;
        have_rx_buffered_latency = 1;
      }
    }
  }
  if (n < BUFFER_MONITOR_MAX_ROWS) {
    AUDIO_BUFFER_DIAG diag;
    if (audio_get_mic_buffer_diag(&diag) && diag.available) {
      char value[64];
      double ms = (double)diag.queued * 1000.0 / 48000.0;
      g_snprintf(value, sizeof(value), "%.1f ms", ms);
#ifdef COREAUDIO
      const char *mic_name = "Mic CoreAudio";
#elif defined(PULSEAUDIO)
      const char *mic_name = "Mic PulseAudio";
#else
      const char *mic_name = "Mic ALSA";
#endif
      row_update(n++, mic_name, value, ms,
                 (double)diag.queued / (double)diag.capacity, 1);
    }
  }
  if (active_receiver != NULL && n < BUFFER_MONITOR_MAX_ROWS) {
    AUDIO_BUFFER_DIAG diag;
    if (audio_get_cw_buffer_diag(active_receiver, &diag) && diag.available) {
      char value[64];
      double ms = (double)diag.queued * 1000.0 / 48000.0;
      double target_ms = (double)diag.target * 1000.0 / 48000.0;
      g_snprintf(value, sizeof(value), "%.1f ms / %.1f ms", ms, target_ms);
      double scale = diag.target > 0 ? (double)diag.target * 2.0 : (double)diag.capacity;
      row_update(n++, "CW Sidetone", value, ms,
                 (double)diag.queued / scale, 1);
    }
  }
  if (radio_is_transmitting()) {
    have_rx_buffered_latency = 0;
  }
  row_count = n;
}

static gboolean buffer_monitor_draw_cb(GtkWidget *widget, cairo_t *cr, gpointer data) {
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  GtkStyleContext *style = gtk_widget_get_style_context(widget);
  gtk_render_background(style, cr, 0, 0, allocation.width, allocation.height);
  GdkRGBA fg;
  gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &fg);
  cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, fg.alpha);
  cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 16.0);
  cairo_move_to(cr, BUFFER_MONITOR_MARGIN, 20);
  cairo_show_text(cr, "Buffer Monitor");
  cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 15.0);
  for (int i = 0; i < row_count; i++) {
    BUFFER_MONITOR_ROW *row = &rows[i];
    int y = 31 + (i * BUFFER_MONITOR_ROW_H);
    int bar_y = y + 20;
    int bar_x = BUFFER_MONITOR_MARGIN;
    int bar_w = allocation.width - (2 * BUFFER_MONITOR_MARGIN);
    int bar_h = 11;
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, fg.alpha);
    cairo_move_to(cr, bar_x, y + 12);
    cairo_show_text(cr, row->name);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, row->value, &ext);
    cairo_move_to(cr, allocation.width - BUFFER_MONITOR_MARGIN - ext.width, y + 12);
    cairo_show_text(cr, row->value);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.18);
    cairo_rectangle(cr, bar_x, bar_y, bar_w, bar_h);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.72);
    cairo_rectangle(cr, bar_x, bar_y, bar_w * row->fraction, bar_h);
    cairo_fill(cr);
    if (row->have_history) {
      char history[64];
      g_snprintf(history, sizeof(history), "min %.1f   max %.1f",
                 row->minimum, row->maximum);
      cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.65);
      cairo_set_font_size(cr, 15.0);
      cairo_move_to(cr, bar_x, bar_y + bar_h + 13);
      cairo_show_text(cr, history);
      cairo_set_font_size(cr, 13.0);
    }
  }
  if (row_count == 0) {
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.7);
    cairo_move_to(cr, BUFFER_MONITOR_MARGIN, 50);
    cairo_show_text(cr, "No active monitored buffers");
  }
  if (have_rx_buffered_latency) {
    char value[64];
    int y = 42 + (row_count > 0 ? row_count : 1) * BUFFER_MONITOR_ROW_H;
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, fg.alpha);
    cairo_set_font_size(cr, 15.0);
    cairo_move_to(cr, BUFFER_MONITOR_MARGIN, y + 12);
    cairo_show_text(cr, "LATENCY");
    y += 22;
    g_snprintf(value, sizeof(value), "%.1f ms", rx_buffered_latency_ms);
    cairo_move_to(cr, BUFFER_MONITOR_MARGIN, y + 12);
    cairo_show_text(cr, "RX buffering");
    cairo_text_extents_t ext;
    cairo_text_extents(cr, value, &ext);
    cairo_move_to(cr, allocation.width - BUFFER_MONITOR_MARGIN - ext.width, y + 12);
    cairo_show_text(cr, value);
    y += 22;
    cairo_move_to(cr, BUFFER_MONITOR_MARGIN, y + 12);
    cairo_show_text(cr, "DSP processing");
    cairo_text_extents(cr, "additional", &ext);
    cairo_move_to(cr, allocation.width - BUFFER_MONITOR_MARGIN - ext.width, y + 12);
    cairo_show_text(cr, "additional");
    y += 22;
    g_snprintf(value, sizeof(value), "> %.1f ms", rx_buffered_latency_ms);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_move_to(cr, BUFFER_MONITOR_MARGIN, y + 12);
    cairo_show_text(cr, "Total RX latency");
    cairo_text_extents(cr, value, &ext);
    cairo_move_to(cr, allocation.width - BUFFER_MONITOR_MARGIN - ext.width, y + 12);
    cairo_show_text(cr, value);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
  }
  return FALSE;
}

static void buffer_monitor_position(void) {
  if (buffer_monitor_window == NULL || GTK_IS_POPOVER(buffer_monitor_window)) {
    return;
  }
  if (top_window == NULL || !gtk_widget_get_realized(top_window)) {
    return;
  }
  int main_x, main_y, main_w, main_h;
  int monitor_w, monitor_h;
  gtk_window_get_position(GTK_WINDOW(top_window), &main_x, &main_y);
  gtk_window_get_size(GTK_WINDOW(top_window), &main_w, &main_h);
  gtk_window_get_size(GTK_WINDOW(buffer_monitor_window), &monitor_w, &monitor_h);
  if (monitor_w <= 1) {
    monitor_w = BUFFER_MONITOR_WIDTH;
  }
  gtk_window_move(GTK_WINDOW(buffer_monitor_window),
                  main_x - monitor_w,
                  main_y + ((main_h - monitor_h) / 2));
}

static gboolean buffer_monitor_update_cb(gpointer data) {
  if (buffer_monitor_window == NULL) {
    buffer_monitor_timer_id = 0;
    return G_SOURCE_REMOVE;
  }
  buffer_monitor_collect();
  int height = 42 + (row_count > 0 ? row_count : 1) * BUFFER_MONITOR_ROW_H;
  if (have_rx_buffered_latency) {
    height += 94;
  }
  gtk_widget_set_size_request(buffer_monitor_area, BUFFER_MONITOR_WIDTH, height);
  gtk_widget_queue_draw(buffer_monitor_area);
  buffer_monitor_position();
  return G_SOURCE_CONTINUE;
}

static void buffer_monitor_destroy_cb(GtkWidget *widget, gpointer data) {
  if (buffer_monitor_timer_id != 0) {
    g_source_remove(buffer_monitor_timer_id);
    buffer_monitor_timer_id = 0;
  }
  buffer_monitor_window = NULL;
  buffer_monitor_area = NULL;
  memset(rows, 0, sizeof(rows));
  row_count = 0;
}

static void buffer_monitor_show_popover(GtkWidget *anchor, gpointer data) {
  GtkWidget *popover = GTK_WIDGET(data);
  if (popover == NULL) {
    return;
  }
  GtkAllocation a;
  gtk_widget_get_allocation(anchor, &a);
  GdkRectangle rect = { -1, a.height / 2, 1, 1 };
#ifdef __linux__
  /*
   * On Wayland this is only a placement preference; the compositor remains
   * authoritative. Allow the popover to extend beyond its relative widget
   * where the backend permits it.
   */
  if (!full_screen) {
    gtk_popover_set_constrain_to(GTK_POPOVER(popover), GTK_POPOVER_CONSTRAINT_NONE);
  }
#endif
  gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
  gtk_widget_show_all(popover);
  gtk_window_present(GTK_WINDOW(top_window));
}

static void buffer_monitor_create(void) {
  memset(rows, 0, sizeof(rows));
  row_count = 0;
  buffer_monitor_area = gtk_drawing_area_new();
  gtk_widget_set_size_request(buffer_monitor_area, BUFFER_MONITOR_WIDTH, 220);
  g_signal_connect(buffer_monitor_area, "draw",
                   G_CALLBACK(buffer_monitor_draw_cb), NULL);
  /*
   * Backend split:
   *
   * - Wayland: use a GtkPopover because arbitrary top-level positioning is
   *   intentionally restricted by the compositor.
   * - macOS/Quartz and X11: use a real non-focusable dialog. This guarantees
   *   that the monitor stays outside the left edge of the main window; Quartz
   *   otherwise may flip a GtkPopover inward despite GTK_POS_LEFT.
   */
  if (use_wayland) {
    GtkWidget *anchor = NULL;
    if (GTK_IS_BIN(top_window)) {
      anchor = gtk_bin_get_child(GTK_BIN(top_window));
    }
    if (anchor == NULL) {
      anchor = topgrid;
    }
    if (anchor != NULL) {
      GtkWidget *popover = gtk_popover_new(anchor);
      gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_LEFT);
      gtk_popover_set_modal(GTK_POPOVER(popover), FALSE);
      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
      gtk_container_add(GTK_CONTAINER(popover), box);
      gtk_box_pack_start(GTK_BOX(box), buffer_monitor_area, TRUE, TRUE, 0);
      buffer_monitor_window = popover;
      g_signal_connect(buffer_monitor_window, "destroy",
                       G_CALLBACK(buffer_monitor_destroy_cb), NULL);
      if (gtk_widget_get_mapped(anchor)) {
        buffer_monitor_show_popover(anchor, buffer_monitor_window);
      } else {
        g_signal_connect(anchor, "map",
                         G_CALLBACK(buffer_monitor_show_popover),
                         buffer_monitor_window);
      }
    }
  }
  if (buffer_monitor_window == NULL) {
    GtkWidget *dlg = gtk_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(top_window));
    gtk_window_set_type_hint(GTK_WINDOW(dlg), GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(dlg), FALSE);
    gtk_widget_set_can_focus(dlg, FALSE);
    GtkWidget *hb = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(dlg), hb);
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), FALSE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(hb), "Buffer Monitor");
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_add(GTK_CONTAINER(content), buffer_monitor_area);
    buffer_monitor_window = dlg;
    g_signal_connect(buffer_monitor_window, "destroy",
                     G_CALLBACK(buffer_monitor_destroy_cb), NULL);
    gtk_widget_show_all(buffer_monitor_window);
    buffer_monitor_position();
    gtk_window_present(GTK_WINDOW(top_window));
  }
  buffer_monitor_collect();
  buffer_monitor_timer_id =
          g_timeout_add(BUFFER_MONITOR_REFRESH, buffer_monitor_update_cb, NULL);
  gtk_widget_queue_draw(buffer_monitor_area);
}

void buffer_monitor_close(void) {
  if (buffer_monitor_window != NULL) {
    gtk_widget_destroy(buffer_monitor_window);
  }
}

void buffer_monitor_toggle(void) {
  if (buffer_monitor_window != NULL) {
    buffer_monitor_close();
  } else {
    buffer_monitor_create();
  }
}
