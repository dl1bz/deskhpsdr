/* Copyright (C)
 * 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef WDSP1
  #include <wdsp.h>
#endif

#include "rx_eq_graph.h"
#include "radio.h"
#include "vfo.h"
#include "ext.h"

#define RX_EQ_POINTS 12
#define RX_EQ_DRAW_POINTS 1024
#define RX_EQ_FMIN 10.0
#define RX_EQ_FMAX 16000.0
#define RX_EQ_GMIN -20.0
#define RX_EQ_GMAX 20.0
#define RX_EQ_PAD_LEFT 46.0
#define RX_EQ_PAD_RIGHT 14.0
#define RX_EQ_PAD_TOP 12.0
#define RX_EQ_PAD_BOTTOM 30.0
#define RX_EQ_POINT_RADIUS 5.0

typedef struct {
  GtkWidget *container;
  GtkWidget *area;
  GtkWidget *freq_spin[13];
  GtkWidget *gain_spin[13];
  RECEIVER *rx;
  int selected;
  gboolean dragging;
  double drag_freq;
  double drag_gain;
} RX_EQ_GRAPH;

static RX_EQ_GRAPH *active_graph[2] = {NULL, NULL};

static void set_spin_value_silent(GtkWidget *spin, double value) {
  if (!spin) { return; }
  guint signal_id = g_signal_lookup("value-changed", GTK_TYPE_SPIN_BUTTON);
  g_signal_handlers_block_matched(spin, G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
  g_signal_handlers_unblock_matched(spin, G_SIGNAL_MATCH_ID, signal_id, 0, NULL, NULL, NULL);
}

static double clampd(double v, double lo, double hi) {
  if (v < lo) { return lo; }
  if (v > hi) { return hi; }
  return v;
}

static double freq_to_x(double f, double width) {
  const double plotw = width - RX_EQ_PAD_LEFT - RX_EQ_PAD_RIGHT;
  const double lo = log10(RX_EQ_FMIN);
  const double hi = log10(RX_EQ_FMAX);
  f = clampd(f, RX_EQ_FMIN, RX_EQ_FMAX);
  return RX_EQ_PAD_LEFT + (log10(f) - lo) * plotw / (hi - lo);
}

static double x_to_freq(double x, double width) {
  const double plotw = width - RX_EQ_PAD_LEFT - RX_EQ_PAD_RIGHT;
  const double lo = log10(RX_EQ_FMIN);
  const double hi = log10(RX_EQ_FMAX);
  double t = (x - RX_EQ_PAD_LEFT) / plotw;
  t = clampd(t, 0.0, 1.0);
  return pow(10.0, lo + t * (hi - lo));
}

static double gain_to_y(double g, double height) {
  const double ploth = height - RX_EQ_PAD_TOP - RX_EQ_PAD_BOTTOM;
  g = clampd(g, RX_EQ_GMIN, RX_EQ_GMAX);
  return RX_EQ_PAD_TOP + (RX_EQ_GMAX - g) * ploth / (RX_EQ_GMAX - RX_EQ_GMIN);
}

static double y_to_gain(double y, double height) {
  const double ploth = height - RX_EQ_PAD_TOP - RX_EQ_PAD_BOTTOM;
  double t = (y - RX_EQ_PAD_TOP) / ploth;
  t = clampd(t, 0.0, 1.0);
  return RX_EQ_GMAX - t * (RX_EQ_GMAX - RX_EQ_GMIN);
}

static void save_curve_to_mode(RX_EQ_GRAPH *g) {
  if (g->rx->id != 0) { return; }
  int mode = vfo[g->rx->id].mode;
  mode_settings[mode].rx_eq_curve_degree = g->rx->eq_curve_degree;
  mode_settings[mode].rx_eq_curve_r = g->rx->eq_curve_r;
  mode_settings[mode].rx_eq_curve_umethod = g->rx->eq_curve_umethod;
  for (int i = 0; i < RX_EQ_POINTS; i++) {
    mode_settings[mode].rx_eq_weight[i] = g->rx->eq_weight[i];
  }
  copy_mode_settings(mode);
}

static void apply_curve(RX_EQ_GRAPH *g) {
#ifndef WDSP1
  SetRXAEQCurve(g->rx->id, g->rx->eq_curve_degree, g->rx->eq_curve_r, g->rx->eq_curve_umethod);
  SetRXAEQWeights(g->rx->id, RX_EQ_POINTS, g->rx->eq_weight);
#endif
  gtk_widget_queue_draw(g->area);
}

static void degree_changed_cb(GtkComboBox *combo, gpointer data) {
  RX_EQ_GRAPH *g = data;
  static const int degrees[] = {0, 1, 3, 5, 7};
  int n = gtk_combo_box_get_active(combo);
  if (n < 0 || n >= (int)(sizeof(degrees) / sizeof(degrees[0]))) { return; }
  g->rx->eq_curve_degree = degrees[n];
  apply_curve(g);
  save_curve_to_mode(g);
}

static void weights_toggled_cb(GtkToggleButton *button, gpointer data) {
  RX_EQ_GRAPH *g = data;
  g->rx->eq_curve_r = gtk_toggle_button_get_active(button) ? 1 : 0;
  apply_curve(g);
  save_curve_to_mode(g);
}

static void draw_text(cairo_t *cr, double x, double y, const char *text, double size) {
  cairo_set_font_size(cr, size);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, text);
}

static gboolean draw_cb(GtkWidget *widget, cairo_t *cr, gpointer data) {
  RX_EQ_GRAPH *g = data;
  GtkAllocation a;
  GdkRGBA fg;
  gtk_widget_get_allocation(widget, &a);
  GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
  gtk_style_context_get_color(ctx, gtk_style_context_get_state(ctx), &fg);
  const double width = a.width;
  const double height = a.height;
  const double x0 = RX_EQ_PAD_LEFT;
  const double x1 = width - RX_EQ_PAD_RIGHT;
  const double y0 = RX_EQ_PAD_TOP;
  const double y1 = height - RX_EQ_PAD_BOTTOM;
  cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.22);
  cairo_set_line_width(cr, 1.0);
  const int gains[] = {-20, -10, 0, 10, 20};
  for (unsigned int i = 0; i < sizeof(gains) / sizeof(gains[0]); i++) {
    double y = gain_to_y(gains[i], height);
    cairo_move_to(cr, x0, y);
    cairo_line_to(cr, x1, y);
    cairo_stroke(cr);
    char s[16];
    snprintf(s, sizeof(s), "%+d", gains[i]);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.75);
    draw_text(cr, 4.0, y + 4.0, s, 10.0);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.22);
  }
  const int freqs[] = {10, 30, 100, 300, 1000, 3000, 10000, 16000};
  const char *flabels[] = {"10", "30", "100", "300", "1k", "3k", "10k", "16k"};
  for (unsigned int i = 0; i < sizeof(freqs) / sizeof(freqs[0]); i++) {
    double x = freq_to_x(freqs[i], width);
    cairo_move_to(cr, x, y0);
    cairo_line_to(cr, x, y1);
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.75);
    draw_text(cr, x - 9.0, height - 8.0, flabels[i], 10.0);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.22);
  }
  cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.9);
  cairo_set_line_width(cr, 2.0);
#ifndef WDSP1
  if (g->rx->eq_curve_degree >= 1) {
    double X[RX_EQ_DRAW_POINTS];
    double Y[RX_EQ_DRAW_POINTS];
    GetRXAEQDraw(g->rx->id, X, Y);
    gboolean started = FALSE;
    for (int i = 0; i < RX_EQ_DRAW_POINTS; i++) {
      /* WDSP 2.10 currently returns X as a fraction of Nyquist. */
      double f = X[i] * 24000.0;
      if (!isfinite(f) || !isfinite(Y[i]) || f < RX_EQ_FMIN || f > RX_EQ_FMAX) { continue; }
      double x = freq_to_x(f, width);
      double y = gain_to_y(Y[i], height);
      if (!started) {
        cairo_move_to(cr, x, y);
        started = TRUE;
      } else {
        cairo_line_to(cr, x, y);
      }
    }
    if (started) { cairo_stroke(cr); }
  } else
#endif
  {
    for (int i = 1; i <= RX_EQ_POINTS; i++) {
      double f = (g->dragging && g->selected == i) ? g->drag_freq : g->rx->eq_freq[i];
      double gain = (g->dragging && g->selected == i) ? g->drag_gain : g->rx->eq_gain[i];
      double x = freq_to_x(f, width);
      double y = gain_to_y(gain, height);
      if (i == 1) { cairo_move_to(cr, x, y); }
      else { cairo_line_to(cr, x, y); }
    }
    cairo_stroke(cr);
  }
  for (int i = 1; i <= RX_EQ_POINTS; i++) {
    double f = (g->dragging && g->selected == i) ? g->drag_freq : g->rx->eq_freq[i];
    double gain = (g->dragging && g->selected == i) ? g->drag_gain : g->rx->eq_gain[i];
    double x = freq_to_x(f, width);
    double y = gain_to_y(gain, height);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, i == g->selected ? 1.0 : 0.8);
    cairo_arc(cr, x, y, RX_EQ_POINT_RADIUS, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
  }
  return FALSE;
}

static int point_at(RX_EQ_GRAPH *g, double x, double y, double width, double height) {
  int best = 0;
  double best_d2 = 11.0 * 11.0;
  for (int i = 1; i <= RX_EQ_POINTS; i++) {
    double f = (g->dragging && g->selected == i) ? g->drag_freq : g->rx->eq_freq[i];
    double gain = (g->dragging && g->selected == i) ? g->drag_gain : g->rx->eq_gain[i];
    double px = freq_to_x(f, width);
    double py = gain_to_y(gain, height);
    double dx = x - px;
    double dy = y - py;
    double d2 = dx * dx + dy * dy;
    if (d2 <= best_d2) {
      best = i;
      best_d2 = d2;
    }
  }
  return best;
}

static gboolean query_tooltip_cb(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode,
                                 GtkTooltip *tooltip, gpointer data) {
  RX_EQ_GRAPH *g = data;
  if (keyboard_mode) { return FALSE; }
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);
  int p = point_at(g, x, y, a.width, a.height);
  if (!p) { return FALSE; }
  double f = (g->dragging && g->selected == p) ? g->drag_freq : g->rx->eq_freq[p];
  double gain = (g->dragging && g->selected == p) ? g->drag_gain : g->rx->eq_gain[p];
  char text[64];
  snprintf(text, sizeof(text), "Frequency: %.0f Hz\nGain: %+.1f dB", f, gain);
  gtk_tooltip_set_text(tooltip, text);
  return TRUE;
}

static gboolean button_press_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  RX_EQ_GRAPH *g = data;
  if (event->button != 1) { return FALSE; }
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);
  int p = point_at(g, event->x, event->y, a.width, a.height);
  if (!p) { return FALSE; }
  g->selected = p;
  g->dragging = TRUE;
  g->drag_freq = g->rx->eq_freq[p];
  g->drag_gain = g->rx->eq_gain[p];
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean motion_cb(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
  RX_EQ_GRAPH *g = data;
  if (!g->dragging || g->selected < 1) { return FALSE; }
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);
  double f = x_to_freq(event->x, a.width);
  double gain = y_to_gain(event->y, a.height);
  /* Preserve point order so each saved NURBS weight stays attached to its point. */
  double flo = (g->selected > 1) ? g->rx->eq_freq[g->selected - 1] + 10.0 : RX_EQ_FMIN;
  double fhi = (g->selected < RX_EQ_POINTS) ? g->rx->eq_freq[g->selected + 1] - 10.0 : RX_EQ_FMAX;
  f = clampd(f, flo, fhi);
  f = round(f / 10.0) * 10.0;
  gain = round(gain);
  g->drag_freq = f;
  g->drag_gain = gain;
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean button_release_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  RX_EQ_GRAPH *g = data;
  if (event->button != 1 || !g->dragging || g->selected < 1) { return FALSE; }
  int p = g->selected;
  g->dragging = FALSE;
  /* Commit frequency and gain together.  Synchronize the numeric controls
   * silently and perform exactly one WDSP update after button release. */
  g->rx->eq_freq[p] = g->drag_freq;
  g->rx->eq_gain[p] = g->drag_gain;
  if (g->rx->id == 0) {
    int mode = vfo[g->rx->id].mode;
    mode_settings[mode].rx_eq_freq[p] = g->drag_freq;
    mode_settings[mode].rx_eq_gain[p] = g->drag_gain;
    copy_mode_settings(mode);
  }
  set_spin_value_silent(g->freq_spin[p], g->drag_freq);
  set_spin_value_silent(g->gain_spin[p], g->drag_gain);
  rx_set_equalizer(g->rx);
  g_idle_add(ext_vfo_update, NULL);
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean scroll_cb(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
  RX_EQ_GRAPH *g = data;
  if (!g->rx->eq_curve_r) { return FALSE; }
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);
  int p = point_at(g, event->x, event->y, a.width, a.height);
  if (!p) { return FALSE; }
  double delta = 0.0;
  if (event->direction == GDK_SCROLL_UP) { delta = 0.1; }
  else if (event->direction == GDK_SCROLL_DOWN) { delta = -0.1; }
  else if (event->direction == GDK_SCROLL_SMOOTH) {
    double dx, dy;
    if (gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy)) { delta = -0.1 * dy; }
  }
  if (delta == 0.0) { return FALSE; }
  g->rx->eq_weight[p - 1] = clampd(g->rx->eq_weight[p - 1] + delta, 0.1, 99.9);
  apply_curve(g);
  save_curve_to_mode(g);
  return TRUE;
}

static void graph_destroy_cb(GtkWidget *widget, gpointer data) {
  RX_EQ_GRAPH *g = data;
  if (g->rx->id >= 0 && g->rx->id < 2 && active_graph[g->rx->id] == g) { active_graph[g->rx->id] = NULL; }
  g_free(g);
}

GtkWidget *rx_eq_graph_create(RECEIVER *rx) {
  RX_EQ_GRAPH *g = g_new0(RX_EQ_GRAPH, 1);
  g->rx = rx;
  g->selected = 0;
  GtkWidget *frame = gtk_frame_new("RX EQ Curve");
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_container_set_border_width(GTK_CONTAINER(box), 5);
  gtk_container_add(GTK_CONTAINER(frame), box);
  g->area = gtk_drawing_area_new();
  gtk_widget_set_size_request(g->area, 620, 250);
  gtk_widget_set_hexpand(g->area, TRUE);
  gtk_widget_add_events(g->area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                        GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
  gtk_widget_set_has_tooltip(g->area, TRUE);
  gtk_box_pack_start(GTK_BOX(box), g->area, TRUE, TRUE, 0);
  GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(box), controls, FALSE, FALSE, 0);
  GtkWidget *label = gtk_label_new("Curve:");
  gtk_box_pack_start(GTK_BOX(controls), label, FALSE, FALSE, 0);
  GtkWidget *combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Legacy linear");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Linear (1)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Cubic (3)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Degree 5");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Degree 7");
  int active = 0;
  if (rx->eq_curve_degree == 1) { active = 1; }
  else if (rx->eq_curve_degree == 3) { active = 2; }
  else if (rx->eq_curve_degree == 5) { active = 3; }
  else if (rx->eq_curve_degree == 7) { active = 4; }
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active);
  gtk_box_pack_start(GTK_BOX(controls), combo, FALSE, FALSE, 0);
  GtkWidget *weights = gtk_check_button_new_with_label("NURBS weights");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(weights), rx->eq_curve_r != 0);
  gtk_widget_set_tooltip_text(weights,
                              "Enable rational NURBS weights. Hover a control point and use the mouse wheel to adjust its weight.");
  gtk_box_pack_start(GTK_BOX(controls), weights, FALSE, FALSE, 0);
  GtkWidget *hint = gtk_label_new("Drag points; DSP updates on release. Curve excludes the frequency-independent gain.");
  gtk_widget_set_halign(hint, GTK_ALIGN_END);
  gtk_widget_set_hexpand(hint, TRUE);
  gtk_box_pack_start(GTK_BOX(controls), hint, TRUE, TRUE, 0);
  g_signal_connect(g->area, "draw", G_CALLBACK(draw_cb), g);
  g_signal_connect(g->area, "query-tooltip", G_CALLBACK(query_tooltip_cb), g);
  g_signal_connect(g->area, "button-press-event", G_CALLBACK(button_press_cb), g);
  g_signal_connect(g->area, "motion-notify-event", G_CALLBACK(motion_cb), g);
  g_signal_connect(g->area, "button-release-event", G_CALLBACK(button_release_cb), g);
  g_signal_connect(g->area, "scroll-event", G_CALLBACK(scroll_cb), g);
  g_signal_connect(combo, "changed", G_CALLBACK(degree_changed_cb), g);
  g_signal_connect(weights, "toggled", G_CALLBACK(weights_toggled_cb), g);
  g_signal_connect(frame, "destroy", G_CALLBACK(graph_destroy_cb), g);
  g->container = frame;
  if (rx->id >= 0 && rx->id < 2) { active_graph[rx->id] = g; }
  return frame;
}

void rx_eq_graph_bind_control(int rx_id, int index, GtkWidget *freq_spin, GtkWidget *gain_spin) {
  if (rx_id < 0 || rx_id >= 2 || !active_graph[rx_id] || index < 1 || index > RX_EQ_POINTS) { return; }
  active_graph[rx_id]->freq_spin[index] = freq_spin;
  active_graph[rx_id]->gain_spin[index] = gain_spin;
}

void rx_eq_graph_refresh(int rx_id) {
  if (rx_id < 0 || rx_id >= 2) { return; }
  if (active_graph[rx_id] && active_graph[rx_id]->area) { gtk_widget_queue_draw(active_graph[rx_id]->area); }
}
