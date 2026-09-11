/* Copyright (C)
 * 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>

#ifndef WDSP1
  #include <wdsp.h>
#endif

#include "cfc_graph.h"
#include "radio.h"
#include "vfo.h"
#include "ext.h"

#define CFC_POINTS 12
#define CFC_DRAW_POINTS 1024
#define CFC_FMIN 10.0
#define CFC_FMAX 16000.0
#define CFC_YMIN -20.0
#define CFC_YMAX 20.0
#define CFC_PAD_LEFT 46.0
#define CFC_PAD_RIGHT 14.0
#define CFC_PAD_TOP 12.0
#define CFC_PAD_BOTTOM 30.0
#define CFC_POINT_RADIUS 5.0
#define CFC_MARKER_OFFSET 3.0

typedef enum {
  CFC_POINT_NONE = 0,
  CFC_POINT_COMP,
  CFC_POINT_POST
} CFC_POINT_KIND;

typedef struct {
  GtkWidget *container;
  GtkWidget *area;
  GtkWidget *freq_spin[13];
  GtkWidget *comp_spin[13];
  GtkWidget *post_spin[13];
  TRANSMITTER *tx;
  int selected;
  CFC_POINT_KIND selected_kind;
  gboolean dragging;
  double drag_freq;
  double drag_value;
} CFC_GRAPH;

static CFC_GRAPH *active_graph = NULL;

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
  const double plotw = width - CFC_PAD_LEFT - CFC_PAD_RIGHT;
  const double lo = log10(CFC_FMIN);
  const double hi = log10(CFC_FMAX);
  f = clampd(f, CFC_FMIN, CFC_FMAX);
  return CFC_PAD_LEFT + (log10(f) - lo) * plotw / (hi - lo);
}

static double x_to_freq(double x, double width) {
  const double plotw = width - CFC_PAD_LEFT - CFC_PAD_RIGHT;
  const double lo = log10(CFC_FMIN);
  const double hi = log10(CFC_FMAX);
  double t = (x - CFC_PAD_LEFT) / plotw;
  t = clampd(t, 0.0, 1.0);
  return pow(10.0, lo + t * (hi - lo));
}

static double value_to_y(double v, double height) {
  const double ploth = height - CFC_PAD_TOP - CFC_PAD_BOTTOM;
  v = clampd(v, CFC_YMIN, CFC_YMAX);
  return CFC_PAD_TOP + (CFC_YMAX - v) * ploth / (CFC_YMAX - CFC_YMIN);
}

static double y_to_value(double y, double height) {
  const double ploth = height - CFC_PAD_TOP - CFC_PAD_BOTTOM;
  double t = (y - CFC_PAD_TOP) / ploth;
  t = clampd(t, 0.0, 1.0);
  return CFC_YMAX - t * (CFC_YMAX - CFC_YMIN);
}

static double marker_offset(CFC_POINT_KIND kind) {
  return kind == CFC_POINT_COMP ? -CFC_MARKER_OFFSET : CFC_MARKER_OFFSET;
}

static void save_curve_to_mode(CFC_GRAPH *g) {
  int mode = vfo[vfo_get_tx_vfo()].mode;
  mode_settings[mode].cfc_comp_curve_degree = g->tx->cfc_comp_curve_degree;
  mode_settings[mode].cfc_comp_curve_r = g->tx->cfc_comp_curve_r;
  mode_settings[mode].cfc_comp_curve_umethod = g->tx->cfc_comp_curve_umethod;
  mode_settings[mode].cfc_post_curve_degree = g->tx->cfc_post_curve_degree;
  mode_settings[mode].cfc_post_curve_r = g->tx->cfc_post_curve_r;
  mode_settings[mode].cfc_post_curve_umethod = g->tx->cfc_post_curve_umethod;
  for (int i = 0; i < CFC_POINTS; i++) {
    mode_settings[mode].cfc_comp_weight[i] = g->tx->cfc_comp_weight[i];
    mode_settings[mode].cfc_post_weight[i] = g->tx->cfc_post_weight[i];
  }
  copy_mode_settings(mode);
}

static void apply_comp_curve(CFC_GRAPH *g) {
#ifndef WDSP1
  SetTXACFCOMPCompCurve(g->tx->id, g->tx->cfc_comp_curve_degree,
                        g->tx->cfc_comp_curve_r, g->tx->cfc_comp_curve_umethod);
  SetTXACFCOMPCompWeights(g->tx->id, CFC_POINTS, g->tx->cfc_comp_weight);
#endif
  gtk_widget_queue_draw(g->area);
}

static void apply_post_curve(CFC_GRAPH *g) {
#ifndef WDSP1
  SetTXACFCOMPPeqCurve(g->tx->id, g->tx->cfc_post_curve_degree,
                       g->tx->cfc_post_curve_r, g->tx->cfc_post_curve_umethod);
  SetTXACFCOMPPeqWeights(g->tx->id, CFC_POINTS, g->tx->cfc_post_weight);
#endif
  gtk_widget_queue_draw(g->area);
}

static int degree_from_combo(GtkComboBox *combo) {
  static const int degrees[] = {0, 1, 3, 5, 7};
  int n = gtk_combo_box_get_active(combo);
  if (n < 0 || n >= (int)(sizeof(degrees) / sizeof(degrees[0]))) { return -1; }
  return degrees[n];
}

static void comp_degree_changed_cb(GtkComboBox *combo, gpointer data) {
  CFC_GRAPH *g = data;
  int degree = degree_from_combo(combo);
  if (degree < 0) { return; }
  g->tx->cfc_comp_curve_degree = degree;
  apply_comp_curve(g);
  save_curve_to_mode(g);
}

static void post_degree_changed_cb(GtkComboBox *combo, gpointer data) {
  CFC_GRAPH *g = data;
  int degree = degree_from_combo(combo);
  if (degree < 0) { return; }
  g->tx->cfc_post_curve_degree = degree;
  apply_post_curve(g);
  save_curve_to_mode(g);
}

static void comp_weights_toggled_cb(GtkToggleButton *button, gpointer data) {
  CFC_GRAPH *g = data;
  g->tx->cfc_comp_curve_r = gtk_toggle_button_get_active(button) ? 1 : 0;
  apply_comp_curve(g);
  save_curve_to_mode(g);
}

static void post_weights_toggled_cb(GtkToggleButton *button, gpointer data) {
  CFC_GRAPH *g = data;
  g->tx->cfc_post_curve_r = gtk_toggle_button_get_active(button) ? 1 : 0;
  apply_post_curve(g);
  save_curve_to_mode(g);
}

static void draw_text(cairo_t *cr, double x, double y, const char *text, double size) {
  cairo_set_font_size(cr, size);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, text);
}

static void draw_legacy_curve(CFC_GRAPH *g, cairo_t *cr, double width, double height,
                              CFC_POINT_KIND kind) {
  for (int i = 1; i <= CFC_POINTS; i++) {
    double f = (g->dragging && g->selected == i) ? g->drag_freq : g->tx->cfc_freq[i];
    double v;
    if (g->dragging && g->selected == i && g->selected_kind == kind) {
      v = g->drag_value;
    } else {
      v = kind == CFC_POINT_COMP ? g->tx->cfc_lvl[i] : g->tx->cfc_post[i];
    }
    double x = freq_to_x(f, width);
    double y = value_to_y(v, height);
    if (i == 1) { cairo_move_to(cr, x, y); }
    else { cairo_line_to(cr, x, y); }
  }
  cairo_stroke(cr);
}

#ifndef WDSP1
static void draw_wdsp_curve(CFC_GRAPH *g, cairo_t *cr, double width, double height,
                            CFC_POINT_KIND kind) {
  double X[CFC_DRAW_POINTS];
  double Y[CFC_DRAW_POINTS];
  if (kind == CFC_POINT_COMP) { GetTXACFCOMPCompDraw(g->tx->id, X, Y); }
  else { GetTXACFCOMPPeqDraw(g->tx->id, X, Y); }
  gboolean started = FALSE;
  for (int i = 0; i < CFC_DRAW_POINTS; i++) {
    /* CFCOMP returns its spline X coordinates directly in Hz. */
    double f = X[i];
    if (!isfinite(f) || !isfinite(Y[i]) || f < CFC_FMIN || f > CFC_FMAX) { continue; }
    double x = freq_to_x(f, width);
    double y = value_to_y(Y[i], height);
    if (!started) {
      cairo_move_to(cr, x, y);
      started = TRUE;
    } else {
      cairo_line_to(cr, x, y);
    }
  }
  if (started) { cairo_stroke(cr); }
}
#endif

static gboolean draw_cb(GtkWidget *widget, cairo_t *cr, gpointer data) {
  CFC_GRAPH *g = data;
  GtkAllocation a;
  GdkRGBA fg;
  gtk_widget_get_allocation(widget, &a);
  GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
  gtk_style_context_get_color(ctx, gtk_style_context_get_state(ctx), &fg);
  const double width = a.width;
  const double height = a.height;
  const double x0 = CFC_PAD_LEFT;
  const double x1 = width - CFC_PAD_RIGHT;
  const double y0 = CFC_PAD_TOP;
  const double y1 = height - CFC_PAD_BOTTOM;
  cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.22);
  cairo_set_line_width(cr, 1.0);
  const int levels[] = {-20, -10, 0, 10, 20};
  for (unsigned int i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
    double y = value_to_y(levels[i], height);
    cairo_move_to(cr, x0, y);
    cairo_line_to(cr, x1, y);
    cairo_stroke(cr);
    char s[16];
    snprintf(s, sizeof(s), "%+d", levels[i]);
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
  /* Compression: solid curve. */
  cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.95);
  cairo_set_line_width(cr, 2.0);
#ifndef WDSP1
  if (g->tx->cfc_comp_curve_degree >= 1 && !g->dragging) {
    draw_wdsp_curve(g, cr, width, height, CFC_POINT_COMP);
  } else
#endif
  {
    draw_legacy_curve(g, cr, width, height, CFC_POINT_COMP);
  }
  /* Post gain: dashed curve. */
  const double dashes[] = {7.0, 5.0};
  cairo_set_dash(cr, dashes, 2, 0.0);
  cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.70);
#ifndef WDSP1
  if (g->tx->cfc_post_curve_degree >= 1 && !g->dragging) {
    draw_wdsp_curve(g, cr, width, height, CFC_POINT_POST);
  } else
#endif
  {
    draw_legacy_curve(g, cr, width, height, CFC_POINT_POST);
  }
  cairo_set_dash(cr, NULL, 0, 0.0);
  for (int i = 1; i <= CFC_POINTS; i++) {
    double f = (g->dragging && g->selected == i) ? g->drag_freq : g->tx->cfc_freq[i];
    double comp = (g->dragging && g->selected == i && g->selected_kind == CFC_POINT_COMP)
                  ? g->drag_value : g->tx->cfc_lvl[i];
    double post = (g->dragging && g->selected == i && g->selected_kind == CFC_POINT_POST)
                  ? g->drag_value : g->tx->cfc_post[i];
    double xc = freq_to_x(f, width) + marker_offset(CFC_POINT_COMP);
    double xp = freq_to_x(f, width) + marker_offset(CFC_POINT_POST);
    double yc = value_to_y(comp, height);
    double yp = value_to_y(post, height);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue,
                          (i == g->selected && g->selected_kind == CFC_POINT_COMP) ? 1.0 : 0.9);
    cairo_arc(cr, xc, yc, CFC_POINT_RADIUS, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue,
                          (i == g->selected && g->selected_kind == CFC_POINT_POST) ? 1.0 : 0.65);
    cairo_rectangle(cr, xp - CFC_POINT_RADIUS, yp - CFC_POINT_RADIUS,
                    2.0 * CFC_POINT_RADIUS, 2.0 * CFC_POINT_RADIUS);
    cairo_fill(cr);
  }
  return FALSE;
}

static gboolean point_at(CFC_GRAPH *g, double x, double y, double width, double height,
                         int *point, CFC_POINT_KIND *kind) {
  int best = 0;
  CFC_POINT_KIND best_kind = CFC_POINT_NONE;
  double best_d2 = 11.0 * 11.0;
  for (int i = 1; i <= CFC_POINTS; i++) {
    double f = (g->dragging && g->selected == i) ? g->drag_freq : g->tx->cfc_freq[i];
    for (CFC_POINT_KIND k = CFC_POINT_COMP; k <= CFC_POINT_POST; k++) {
      double v;
      if (g->dragging && g->selected == i && g->selected_kind == k) {
        v = g->drag_value;
      } else {
        v = k == CFC_POINT_COMP ? g->tx->cfc_lvl[i] : g->tx->cfc_post[i];
      }
      double px = freq_to_x(f, width) + marker_offset(k);
      double py = value_to_y(v, height);
      double dx = x - px;
      double dy = y - py;
      double d2 = dx * dx + dy * dy;
      if (d2 < best_d2) {
        best = i;
        best_kind = k;
        best_d2 = d2;
      }
    }
  }
  if (!best) { return FALSE; }
  *point = best;
  *kind = best_kind;
  return TRUE;
}

static gboolean query_tooltip_cb(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode,
                                 GtkTooltip *tooltip, gpointer data) {
  CFC_GRAPH *g = data;
  if (keyboard_mode) { return FALSE; }
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);
  int p;
  CFC_POINT_KIND kind;
  if (!point_at(g, x, y, a.width, a.height, &p, &kind)) { return FALSE; }
  double f = (g->dragging && g->selected == p) ? g->drag_freq : g->tx->cfc_freq[p];
  double v = (g->dragging && g->selected == p && g->selected_kind == kind)
             ? g->drag_value
             : (kind == CFC_POINT_COMP ? g->tx->cfc_lvl[p] : g->tx->cfc_post[p]);
  const double *weights = kind == CFC_POINT_COMP ? g->tx->cfc_comp_weight : g->tx->cfc_post_weight;
  int rational = kind == CFC_POINT_COMP ? g->tx->cfc_comp_curve_r : g->tx->cfc_post_curve_r;
  char text[96];
  if (rational) {
    snprintf(text, sizeof(text), "%s\nFrequency: %.0f Hz\nLevel: %+.1f dB\nWeight: %.1f",
             kind == CFC_POINT_COMP ? "Pre Compression" : "Post Gain", f, v, weights[p - 1]);
  } else {
    snprintf(text, sizeof(text), "%s\nFrequency: %.0f Hz\nLevel: %+.1f dB",
             kind == CFC_POINT_COMP ? "Pre Compression" : "Post Gain", f, v);
  }
  gtk_tooltip_set_text(tooltip, text);
  return TRUE;
}

static gboolean button_press_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  CFC_GRAPH *g = data;
  if (event->button != 1) { return FALSE; }
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);
  int p;
  CFC_POINT_KIND kind;
  if (!point_at(g, event->x, event->y, a.width, a.height, &p, &kind)) { return FALSE; }
  g->selected = p;
  g->selected_kind = kind;
  g->dragging = TRUE;
  g->drag_freq = g->tx->cfc_freq[p];
  g->drag_value = kind == CFC_POINT_COMP ? g->tx->cfc_lvl[p] : g->tx->cfc_post[p];
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean motion_cb(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
  CFC_GRAPH *g = data;
  if (!g->dragging || g->selected < 1 || g->selected_kind == CFC_POINT_NONE) { return FALSE; }
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);
  double f = x_to_freq(event->x - marker_offset(g->selected_kind), a.width);
  double v = y_to_value(event->y, a.height);
  double flo = (g->selected > 1) ? g->tx->cfc_freq[g->selected - 1] + 10.0 : CFC_FMIN;
  double fhi = (g->selected < CFC_POINTS) ? g->tx->cfc_freq[g->selected + 1] - 10.0 : CFC_FMAX;
  f = clampd(f, flo, fhi);
  f = round(f / 10.0) * 10.0;
  v = round(v);
  if (g->selected_kind == CFC_POINT_COMP) { v = clampd(v, 0.0, 20.0); }
  else { v = clampd(v, -20.0, 20.0); }
  g->drag_freq = f;
  g->drag_value = v;
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean button_release_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  CFC_GRAPH *g = data;
  if (event->button != 1 || !g->dragging || g->selected < 1) { return FALSE; }
  int p = g->selected;
  CFC_POINT_KIND kind = g->selected_kind;
  g->dragging = FALSE;
  g->tx->cfc_freq[p] = g->drag_freq;
  if (kind == CFC_POINT_COMP) { g->tx->cfc_lvl[p] = g->drag_value; }
  else { g->tx->cfc_post[p] = g->drag_value; }
  int mode = vfo[vfo_get_tx_vfo()].mode;
  mode_settings[mode].cfc_freq[p] = g->drag_freq;
  if (kind == CFC_POINT_COMP) { mode_settings[mode].cfc_lvl[p] = g->drag_value; }
  else { mode_settings[mode].cfc_post[p] = g->drag_value; }
  copy_mode_settings(mode);
  set_spin_value_silent(g->freq_spin[p], g->drag_freq);
  if (kind == CFC_POINT_COMP) { set_spin_value_silent(g->comp_spin[p], g->drag_value); }
  else { set_spin_value_silent(g->post_spin[p], g->drag_value); }
  tx_set_compressor(g->tx);
  g_idle_add(ext_vfo_update, NULL);
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean scroll_cb(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
  CFC_GRAPH *g = data;
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);
  int p;
  CFC_POINT_KIND kind;
  if (!point_at(g, event->x, event->y, a.width, a.height, &p, &kind)) { return FALSE; }
  int rational = kind == CFC_POINT_COMP ? g->tx->cfc_comp_curve_r : g->tx->cfc_post_curve_r;
  if (!rational) { return FALSE; }
  double delta = 0.0;
  if (event->direction == GDK_SCROLL_UP) { delta = 0.1; }
  else if (event->direction == GDK_SCROLL_DOWN) { delta = -0.1; }
  else if (event->direction == GDK_SCROLL_SMOOTH) {
    double dx, dy;
    if (gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy)) { delta = -0.1 * dy; }
  }
  if (delta == 0.0) { return FALSE; }
  double *weights = kind == CFC_POINT_COMP ? g->tx->cfc_comp_weight : g->tx->cfc_post_weight;
  weights[p - 1] = clampd(weights[p - 1] + delta, 0.1, 99.9);
  if (kind == CFC_POINT_COMP) { apply_comp_curve(g); }
  else { apply_post_curve(g); }
  save_curve_to_mode(g);
  return TRUE;
}

static void graph_destroy_cb(GtkWidget *widget, gpointer data) {
  CFC_GRAPH *g = data;
  if (active_graph == g) { active_graph = NULL; }
  g_free(g);
}

static GtkWidget *curve_combo(int degree) {
  GtkWidget *combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Legacy linear");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Linear (1)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Cubic (3)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Degree 5");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Degree 7");
  int active = 0;
  if (degree == 1) { active = 1; }
  else if (degree == 3) { active = 2; }
  else if (degree == 5) { active = 3; }
  else if (degree == 7) { active = 4; }
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active);
#ifdef WDSP1
  gtk_widget_set_sensitive(combo, FALSE);
#endif
  return combo;
}

GtkWidget *cfc_graph_create(TRANSMITTER *tx) {
  CFC_GRAPH *g = g_new0(CFC_GRAPH, 1);
  g->tx = tx;
  GtkWidget *frame = gtk_frame_new("CFC Curves");
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_container_set_border_width(GTK_CONTAINER(box), 3);
  gtk_container_add(GTK_CONTAINER(frame), box);
  g->area = gtk_drawing_area_new();
  // gtk_widget_set_size_request(g->area, 820, 170);
  gtk_widget_set_size_request(g->area, 620, 170);
  gtk_widget_set_hexpand(g->area, TRUE);
  gtk_widget_add_events(g->area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                        GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
  gtk_widget_set_has_tooltip(g->area, TRUE);
  gtk_box_pack_start(GTK_BOX(box), g->area, TRUE, TRUE, 0);
  GtkWidget *controls = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(controls), 8);
  gtk_grid_set_row_spacing(GTK_GRID(controls), 2);
  gtk_box_pack_start(GTK_BOX(box), controls, FALSE, FALSE, 0);
  GtkWidget *label = gtk_label_new("Pre curve:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(controls), label, 0, 0, 1, 1);
  GtkWidget *comp_combo = curve_combo(tx->cfc_comp_curve_degree);
  gtk_grid_attach(GTK_GRID(controls), comp_combo, 1, 0, 1, 1);
  GtkWidget *comp_weights = gtk_check_button_new_with_label("NURBS weights");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(comp_weights), tx->cfc_comp_curve_r != 0);
  gtk_grid_attach(GTK_GRID(controls), comp_weights, 2, 0, 1, 1);
  label = gtk_label_new("Post curve:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(controls), label, 0, 1, 1, 1);
  GtkWidget *post_combo = curve_combo(tx->cfc_post_curve_degree);
  gtk_grid_attach(GTK_GRID(controls), post_combo, 1, 1, 1, 1);
  GtkWidget *post_weights = gtk_check_button_new_with_label("NURBS weights");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(post_weights), tx->cfc_post_curve_r != 0);
  gtk_grid_attach(GTK_GRID(controls), post_weights, 2, 1, 1, 1);
#ifdef WDSP1
  gtk_widget_set_sensitive(comp_weights, FALSE);
  gtk_widget_set_sensitive(post_weights, FALSE);
#endif
  gtk_widget_set_tooltip_text(comp_weights,
                              "Enable rational NURBS weights for Pre Compression. Hover a circular point and use the mouse wheel to adjust its weight.");
  gtk_widget_set_tooltip_text(post_weights,
                              "Enable rational NURBS weights for Post Gain. Hover a square point and use the mouse wheel to adjust its weight.");
  GtkWidget *hint =
          gtk_label_new("Solid/circles: Pre Compression   Dashed/squares: Post Gain   Drag points; DSP updates on release.");
  gtk_widget_set_halign(hint, GTK_ALIGN_END);
  gtk_widget_set_hexpand(hint, TRUE);
  gtk_grid_attach(GTK_GRID(controls), hint, 3, 0, 1, 2);
  g_signal_connect(g->area, "draw", G_CALLBACK(draw_cb), g);
  g_signal_connect(g->area, "query-tooltip", G_CALLBACK(query_tooltip_cb), g);
  g_signal_connect(g->area, "button-press-event", G_CALLBACK(button_press_cb), g);
  g_signal_connect(g->area, "motion-notify-event", G_CALLBACK(motion_cb), g);
  g_signal_connect(g->area, "button-release-event", G_CALLBACK(button_release_cb), g);
  g_signal_connect(g->area, "scroll-event", G_CALLBACK(scroll_cb), g);
  g_signal_connect(comp_combo, "changed", G_CALLBACK(comp_degree_changed_cb), g);
  g_signal_connect(post_combo, "changed", G_CALLBACK(post_degree_changed_cb), g);
  g_signal_connect(comp_weights, "toggled", G_CALLBACK(comp_weights_toggled_cb), g);
  g_signal_connect(post_weights, "toggled", G_CALLBACK(post_weights_toggled_cb), g);
  g_signal_connect(frame, "destroy", G_CALLBACK(graph_destroy_cb), g);
  g->container = frame;
  active_graph = g;
  return frame;
}

void cfc_graph_bind_control(int index, GtkWidget *freq_spin, GtkWidget *comp_spin, GtkWidget *post_spin) {
  if (!active_graph || index < 1 || index > CFC_POINTS) { return; }
  if (freq_spin) { active_graph->freq_spin[index] = freq_spin; }
  if (comp_spin) { active_graph->comp_spin[index] = comp_spin; }
  if (post_spin) { active_graph->post_spin[index] = post_spin; }
}

void cfc_graph_refresh(void) {
  if (active_graph && active_graph->area) { gtk_widget_queue_draw(active_graph->area); }
}
