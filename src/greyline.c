/* Copyright (C)
*
* 2024 - 2026 Heiko Amft, DL1BZ (Project deskHPSDR)
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

#include "greyline.h"
#include "wmap_fhd_jpg.h"
#include <gtk/gtk.h>
#include <glib.h>
#include <ctype.h>
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEG2RAD(d) ((d) * M_PI / 180.0)
#define RAD2DEG(r) ((r) * 180.0 / M_PI)

static GtkWindow *find_active_parent_window(void) {
  GList *wins = gtk_window_list_toplevels();
  GtkWindow *first_realized = NULL;

  for (GList *l = wins; l; l = l->next) {
    GtkWindow *w = GTK_WINDOW(l->data);

    if (!GTK_IS_WINDOW(w)) { continue; }

    if (gtk_window_is_active(w)) {
      g_list_free(wins);
      return w;
    }

    if (!first_realized && gtk_widget_get_realized(GTK_WIDGET(w))) {
      first_realized = w;
    }
  }

  g_list_free(wins);
  return first_realized;
}

static GtkWidget *g_greyline_singleton_window = NULL;

typedef struct {
  GtkWidget *window;
  GtkWidget *area;

  int w;
  int h;

  char locator[16];

  GdkPixbuf *map_pixbuf;
  cairo_surface_t *map_surface;

  guint timer_id;

  double aspect_w_over_h;
} GreylineWin;

/* --- Embedded JPEG -> GdkPixbuf --- */
static GdkPixbuf *load_embedded_jpg(void) {
  /* day_jpg is expected to be provided by day_jpg.h */
  GInputStream *stream = g_memory_input_stream_new_from_data(day_jpg, (gssize)day_jpg_len, NULL);
  GError *err = NULL;
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &err);
  g_object_unref(stream);

  if (!pixbuf) {
    if (err) {
      g_warning("Greyline: failed to load embedded JPG: %s", err->message);
      g_error_free(err);
    } else {
      g_warning("Greyline: failed to load embedded JPG (unknown error)");
    }
  }

  return pixbuf;
}

static gboolean get_embedded_map_dims(int *mw, int *mh) {
  if (!mw || !mh) { return FALSE; }

  GdkPixbuf *pb = load_embedded_jpg();

  if (!pb) { return FALSE; }

  *mw = gdk_pixbuf_get_width(pb);
  *mh = gdk_pixbuf_get_height(pb);
  g_object_unref(pb);
  return (*mw > 0 && *mh > 0);
}

/* --- Maidenhead locator (4/6 chars typical) -> lat/lon (approx center) --- */
static gboolean locator_to_latlon(const char *loc, double *lat, double *lon) {
  if (!loc || !lat || !lon) { return FALSE; }

  size_t n = strlen(loc);

  if (n < 4) { return FALSE; }

  char A = (char)toupper((unsigned char)loc[0]);
  char B = (char)toupper((unsigned char)loc[1]);

  if (A < 'A' || A > 'R' || B < 'A' || B > 'R') { return FALSE; }

  int field_lon = A - 'A';
  int field_lat = B - 'A';

  if (loc[2] < '0' || loc[2] > '9' || loc[3] < '0' || loc[3] > '9') { return FALSE; }

  int square_lon = loc[2] - '0';
  int square_lat = loc[3] - '0';
  /* Base (lower-left corner) */
  double lo = -180.0 + field_lon * 20.0 + square_lon * 2.0;
  double la =  -90.0 + field_lat * 10.0 + square_lat * 1.0;
  /* Subsquares (optional, letters) */
  double lon_size = 2.0;
  double lat_size = 1.0;

  if (n >= 6) {
    char C = (char)tolower((unsigned char)loc[4]);
    char D = (char)tolower((unsigned char)loc[5]);

    if (C < 'a' || C > 'x' || D < 'a' || D > 'x') { return FALSE; }

    int subs_lon = C - 'a';
    int subs_lat = D - 'a';
    lon_size = 2.0 / 24.0;
    lat_size = 1.0 / 24.0;
    lo += subs_lon * lon_size;
    la += subs_lat * lat_size;
  }

  /* Center of the grid cell */
  *lon = lo + lon_size / 2.0;
  *lat = la + lat_size / 2.0;
  return TRUE;
}

/* --- Solar position: subsolar point (lat, lon) for current UTC --- */
static void subsolar_point_utc(double *sub_lat, double *sub_lon) {
  /* This is a practical approximation suitable for greyline visualization.
     For higher precision, swap in a proper NOAA/SPA implementation. */
  time_t t = time(NULL);
  struct tm gmt;
#if defined(_WIN32)
  gmtime_s(&gmt, &t);
#else
  gmtime_r(&t, &gmt);
#endif
  int year = gmt.tm_year + 1900;
  int month = gmt.tm_mon + 1;
  int day = gmt.tm_mday;
  double hour = gmt.tm_hour + gmt.tm_min / 60.0 + gmt.tm_sec / 3600.0;
  /* Julian Day (UTC) */
  int a = (14 - month) / 12;
  int y = year + 4800 - a;
  int m = month + 12 * a - 3;
  long JDN = day + (153 * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045;
  double JD = (double)JDN + (hour - 12.0) / 24.0;
  double T = (JD - 2451545.0) / 36525.0;
  /* Sun mean longitude & anomaly */
  double L0 = fmod(280.46646 + 36000.76983 * T + 0.0003032 * T * T, 360.0);
  double M  = fmod(357.52911 + 35999.05029 * T - 0.0001537 * T * T, 360.0);
  /* Equation of center */
  double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * sin(DEG2RAD(M))
             + (0.019993 - 0.000101 * T) * sin(DEG2RAD(2 * M))
             + 0.000289 * sin(DEG2RAD(3 * M));
  double true_long = L0 + C;
  /* Obliquity */
  double eps0 = 23.439291 - 0.0130042 * T;
  /* Declination */
  double decl = RAD2DEG(asin(sin(DEG2RAD(eps0)) * sin(DEG2RAD(true_long))));
  /* Equation of time (minutes) approximation */
  double y2 = tan(DEG2RAD(eps0) / 2.0);
  y2 *= y2;
  double Etime = 4.0 * RAD2DEG(
                   y2 * sin(2 * DEG2RAD(L0)) -
                   2.0 * 0.016708634 * sin(DEG2RAD(M)) +
                   4.0 * 0.016708634 * y2 * sin(DEG2RAD(M)) * cos(2 * DEG2RAD(L0)) -
                   0.5 * y2 * y2 * sin(4 * DEG2RAD(L0)) -
                   1.25 * 0.016708634 * 0.016708634 * sin(2 * DEG2RAD(M))
                 );
  /* Subsolar latitude is declination */
  *sub_lat = decl;
  /* Subsolar longitude from apparent solar time:
     lon = 180 - (UTC_minutes + EOT) * 0.25  (approx)
     Normalize to [-180,180]. */
  double utc_minutes = gmt.tm_hour * 60.0 + gmt.tm_min + gmt.tm_sec / 60.0;
  double lon = 180.0 - (utc_minutes + Etime) * 0.25;

  while (lon > 180.0) { lon -= 360.0; }

  while (lon < -180.0) { lon += 360.0; }

  *sub_lon = lon;
}

/* --- D-Layer estimation helpers (solar altitude based) --- */
static double solar_altitude_deg(double lat_deg, double lon_deg) {
  double sub_lat, sub_lon;
  subsolar_point_utc(&sub_lat, &sub_lon);
  double lat = DEG2RAD(lat_deg);
  double lon = DEG2RAD(lon_deg);
  double dec = DEG2RAD(sub_lat);
  double sublon = DEG2RAD(sub_lon);
  double dlon = lon - sublon;
  double cosZ = sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(dlon);

  if (cosZ > 1.0) { cosZ = 1.0; }

  if (cosZ < -1.0) { cosZ = -1.0; }

  /* altitude = asin(cosZ) */
  return RAD2DEG(asin(cosZ));
}

static void draw_text_shadow(cairo_t *cr, double x, double y, const char *s) {
  cairo_save(cr);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.85);
  cairo_move_to(cr, x + 1, y + 1);
  cairo_show_text(cr, s);
  cairo_restore(cr);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, s);
}

static void draw_band_state(cairo_t *cr, double x, double y, const char *label,
                            double r, double g, double b) {
  /* color dot */
  cairo_save(cr);
  cairo_set_source_rgba(cr, r, g, b, 0.95);
  cairo_arc(cr, x + 6, y - 5, 5.0, 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.65);
  cairo_set_line_width(cr, 1.0);
  cairo_arc(cr, x + 6, y - 5, 5.0, 0, 2 * M_PI);
  cairo_stroke(cr);
  cairo_restore(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.90);
  draw_text_shadow(cr, x + 16, y, label);
}

static void draw_dlayer_panel(cairo_t *cr, int w, int h, const char *locator) {
  double lat, lon;

  if (!locator_to_latlon(locator, &lat, &lon)) { return; }

  double alt = solar_altitude_deg(lat, lon);
  /* Simple operational traffic-light logic */
  /* 160/80m: highly D-layer sensitive */
  gboolean lo_red    = (alt > 0.0);
  gboolean lo_yellow = (alt <= 0.0 && alt >= -12.0);
  /* 40m: moderate D-layer impact */
  gboolean mid_red    = (alt > 12.0);
  gboolean mid_yellow = (alt <= 12.0 && alt > 0.0);
  /* 30m: low D-layer sensitivity */
  gboolean hi_yellow  = (alt > 20.0);
  /* Panel box (bottom-left) */
  const double pad = 10.0;
  const double box_w = 210.0;
  const double box_h = 108.0;
  const double x0 = pad;
  const double y0 = (double)h - pad - box_h;
  cairo_save(cr);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.45);
  cairo_rectangle(cr, x0, y0, box_w, box_h);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.15);
  cairo_set_line_width(cr, 1.0);
  cairo_rectangle(cr, x0 + 0.5, y0 + 0.5, box_w - 1.0, box_h - 1.0);
  cairo_stroke(cr);
  cairo_select_font_face(cr, "Roboto-Bold", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 12.0);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.92);
  draw_text_shadow(cr, x0 + 10, y0 + 18, "D-Layer Low band DX estimate");
  /* Band lines */
  double y = y0 + 38;

  if (lo_red) {
    draw_band_state(cr, x0 + 10, y, "160m/80m", 0.90, 0.18, 0.18);
  } else if (lo_yellow) {
    draw_band_state(cr, x0 + 10, y, "160m/80m", 0.90, 0.75, 0.18);
  } else {
    draw_band_state(cr, x0 + 10, y, "160m/80m", 0.18, 0.85, 0.25);
  }

  y += 20;

  if (mid_red) {
    draw_band_state(cr, x0 + 10, y, "40m", 0.90, 0.18, 0.18);
  } else if (mid_yellow) {
    draw_band_state(cr, x0 + 10, y, "40m", 0.90, 0.75, 0.18);
  } else {
    draw_band_state(cr, x0 + 10, y, "40m", 0.18, 0.85, 0.25);
  }

  y += 20;

  if (hi_yellow) {
    draw_band_state(cr, x0 + 10, y, "30m", 0.90, 0.75, 0.18);
  } else {
    draw_band_state(cr, x0 + 10, y, "30m", 0.18, 0.85, 0.25);
  }

  /* Sun elevation line */
  char buf[64];
  g_snprintf(buf, sizeof(buf), "Sun elevation: %.1f°", alt);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.90);
  draw_text_shadow(cr, x0 + 10, y0 + box_h - 10, buf);
  cairo_restore(cr);
}

/* --- Terminator shading --- */
static double terminator_lat_for_lon(double sub_lat, double sub_lon, double lon) {
  double phi = DEG2RAD(sub_lat);
  double lam = DEG2RAD(lon - sub_lon);
  double tanphi = tan(phi);

  if (fabs(tanphi) < 1e-6) {
    return 0.0;
  }

  return RAD2DEG(atan(-cos(lam) / tanphi));
}

static void draw_greyline_overlay(cairo_t *cr, int w, int h) {
  double sub_lat, sub_lon;
  subsolar_point_utc(&sub_lat, &sub_lon);
  int steps = w;

  if (steps < 400) { steps = 400; }

  double step = (double)w / (double)steps;
  /* Determine which side is night by checking the antisolar point */
  double anti_lat = -sub_lat;
  double anti_lon = sub_lon + 180.0;

  while (anti_lon > 180.0) { anti_lon -= 360.0; }

  while (anti_lon < -180.0) { anti_lon += 360.0; }

  double t_lat_at_anti = terminator_lat_for_lon(sub_lat, sub_lon, anti_lon);
  /* If antisolar latitude is south of terminator at that longitude, night polygon closes to bottom */
  gboolean night_closes_to_bottom = (anti_lat < t_lat_at_anti);
  /* Night shading polygon (no CLEAR; avoids white/transparent artifacts) */
  cairo_save(cr);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.45);
  cairo_new_path(cr);

  for (int i = 0; i <= steps; i++) {
    double x = i * step;
    double lon = -180.0 + (x / (double)w) * 360.0;
    double lat = terminator_lat_for_lon(sub_lat, sub_lon, lon);
    double y = (90.0 - lat) / 180.0 * (double)h;

    if (i == 0) { cairo_move_to(cr, x, y); }
    else { cairo_line_to(cr, x, y); }
  }

  if (night_closes_to_bottom) {
    cairo_line_to(cr, w, h);
    cairo_line_to(cr, 0, h);
  } else {
    cairo_line_to(cr, w, 0);
    cairo_line_to(cr, 0, 0);
  }

  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_restore(cr);
  /* Terminator line */
  cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.65);
  cairo_set_line_width(cr, 1.2);
  cairo_new_path(cr);

  for (int i = 0; i <= steps; i++) {
    double x = i * step;
    double lon = -180.0 + (x / (double)w) * 360.0;
    double lat = terminator_lat_for_lon(sub_lat, sub_lon, lon);
    double y = (90.0 - lat) / 180.0 * (double)h;

    if (i == 0) { cairo_move_to(cr, x, y); }
    else { cairo_line_to(cr, x, y); }
  }

  cairo_stroke(cr);
}

/* --- Draw station marker --- */
static void draw_locator_marker(cairo_t *cr, int w, int h, const char *locator) {
  double lat, lon;

  if (!locator_to_latlon(locator, &lat, &lon)) { return; }

  double x = (lon + 180.0) / 360.0 * (double)w;
  double y = (90.0 - lat) / 180.0 * (double)h;
  cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, 0.9);
  cairo_arc(cr, x, y, 4.0, 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
  cairo_set_line_width(cr, 1.0);
  cairo_arc(cr, x, y, 6.5, 0, 2 * M_PI);
  cairo_stroke(cr);

  /* Locator label near marker (readable on any background) */
  if (locator && locator[0]) {
    cairo_text_extents_t ext;
    cairo_save(cr);
    cairo_select_font_face(cr, "Roboto-Bold", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 13.0);
    cairo_text_extents(cr, locator, &ext);
    /* Default: right of marker; if too close to right edge, place left */
    double tx = x + 10.0;
    double ty = y - 10.0;

    if (tx + ext.width + 6.0 > (double)w) {
      tx = x - 10.0 - ext.width;
    }

    if (ty - ext.height < 0.0) {
      ty = y + 16.0;
    }

    /* Shadow */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.85);
    cairo_move_to(cr, tx + 1.0, ty + 1.0);
    cairo_show_text(cr, locator);
    /* Foreground */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, locator);
    cairo_restore(cr);
  }
}

/* --- GTK draw callback --- */
static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
  GreylineWin *gw = (GreylineWin *)user_data;
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);
  gw->w = a.width;
  gw->h = a.height;

  if (!gw->map_surface) {
    if (!gw->map_pixbuf) { gw->map_pixbuf = load_embedded_jpg(); }

    if (gw->map_pixbuf) {
      gw->map_surface = gdk_cairo_surface_create_from_pixbuf(gw->map_pixbuf, 1, NULL);
    }
  }

  if (gw->map_surface) {
    cairo_save(cr);
    int mw = gdk_pixbuf_get_width(gw->map_pixbuf);
    int mh = gdk_pixbuf_get_height(gw->map_pixbuf);
    cairo_scale(cr,
                (double)gw->w / (double)mw,
                (double)gw->h / (double)mh);
    cairo_set_source_surface(cr, gw->map_surface, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
  } else {
    cairo_set_source_rgb(cr, 0.08, 0.08, 0.10);
    cairo_rectangle(cr, 0, 0, gw->w, gw->h);
    cairo_fill(cr);
  }

  draw_greyline_overlay(cr, gw->w, gw->h);
  draw_locator_marker(cr, gw->w, gw->h, gw->locator);
  draw_dlayer_panel(cr, gw->w, gw->h, gw->locator);
  return FALSE;
}

/* --- periodic refresh --- */
static gboolean on_tick(gpointer user_data) {
  GreylineWin *gw = (GreylineWin *)user_data;

  if (!gw || !GTK_IS_WIDGET(gw->area)) { return G_SOURCE_REMOVE; }

  gtk_widget_queue_draw(gw->area);
  return G_SOURCE_CONTINUE;
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
  GreylineWin *gw = (GreylineWin *)user_data;

  if (g_greyline_singleton_window == widget) {
    g_greyline_singleton_window = NULL;
  }

  if (gw->timer_id) {
    g_source_remove(gw->timer_id);
    gw->timer_id = 0;
  }

  if (gw->map_surface) {
    cairo_surface_destroy(gw->map_surface);
    gw->map_surface = NULL;
  }

  if (gw->map_pixbuf) {
    g_object_unref(gw->map_pixbuf);
    gw->map_pixbuf = NULL;
  }

  g_free(gw);
}

static void open_impl(GtkWindow *parent, int window_width, int window_height, const char *locator) {
  /* Single-instance: if already open, raise the existing window and return. */
  if (g_greyline_singleton_window && GTK_IS_WINDOW(g_greyline_singleton_window)) {
    gtk_window_present(GTK_WINDOW(g_greyline_singleton_window));
    return;
  }

  GreylineWin *gw = g_malloc0(sizeof(GreylineWin));
  double aspect_w_over_h = 2.0; /* width/height */

  if (locator && locator[0]) {
    strncpy(gw->locator, locator, sizeof(gw->locator) - 1);
  } else {
    strcpy(gw->locator, "UNKNOWN");
  }

  /* Auto height from embedded map aspect ratio */
  if (window_height <= 0) {
    int mw = 0, mh = 0;

    if (get_embedded_map_dims(&mw, &mh) && mw > 0 && mh > 0) {
      aspect_w_over_h = (double)mw / (double)mh;
      window_height = (int)lround(((double)window_width * (double)mh) / (double)mw);
    } else {
      aspect_w_over_h = 2.0;
      window_height = window_width / 2;
    }
  }

  gw->aspect_w_over_h = aspect_w_over_h;
  gw->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_greyline_singleton_window = gw->window;
  g_object_add_weak_pointer(G_OBJECT(gw->window), (gpointer *)&g_greyline_singleton_window);
  {
    char *title = g_strdup_printf("Greyline - %s", gw->locator);
    gtk_window_set_title(GTK_WINDOW(gw->window), title);
    g_free(title);
  }
  gtk_window_set_default_size(GTK_WINDOW(gw->window), window_width, window_height);
  /* Enforce window aspect ratio = map aspect ratio (no grey side bars). */
  {
    GdkGeometry geo;
    memset(&geo, 0, sizeof(geo));
    geo.min_aspect = gw->aspect_w_over_h;
    geo.max_aspect = gw->aspect_w_over_h;
    gtk_window_set_geometry_hints(GTK_WINDOW(gw->window), NULL, &geo, GDK_HINT_ASPECT);
  }

  /* Parent integration */
  if (parent) {
    gtk_window_set_transient_for(GTK_WINDOW(gw->window), parent);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(gw->window), TRUE);
    gtk_window_set_position(GTK_WINDOW(gw->window), GTK_WIN_POS_CENTER_ON_PARENT);
    /* Do NOT force modal by default; caller can set it externally if desired. */
  } else {
    gtk_window_set_position(GTK_WINDOW(gw->window), GTK_WIN_POS_CENTER);
  }

  gw->area = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(gw->window), gw->area);
  g_signal_connect(gw->area, "draw", G_CALLBACK(on_draw), gw);
  g_signal_connect(gw->window, "destroy", G_CALLBACK(on_window_destroy), gw);
  /* Refresh every 60 seconds. */
  gw->timer_id = g_timeout_add_seconds(60, on_tick, gw);
  gtk_widget_show_all(gw->window);
}

typedef struct {
  GtkWindow *parent_ref;   /* optional, ref'd until executed */
  int window_width;
  int window_height;
  char locator[16];
} GreylineOpenArgs;

static gboolean open_on_main_cb(gpointer user_data) {
  GreylineOpenArgs *a = (GreylineOpenArgs *)user_data;
  open_impl(a->parent_ref, a->window_width, a->window_height, a->locator);

  if (a->parent_ref) {
    g_object_unref(a->parent_ref);
    a->parent_ref = NULL;
  }

  g_free(a);
  return G_SOURCE_REMOVE;
}

static void open_async(GtkWindow *parent, int window_width, int window_height, const char *locator) {
  GreylineOpenArgs *a = g_malloc0(sizeof(*a));
  a->parent_ref = parent ? GTK_WINDOW(g_object_ref(parent)) : NULL;
  a->window_width = window_width;
  a->window_height = window_height;

  if (locator && locator[0]) {
    strncpy(a->locator, locator, sizeof(a->locator) - 1);
  } else {
    strncpy(a->locator, "UNKNOWN", sizeof(a->locator) - 1);
  }

  /* Marshal to GTK main thread (default main context). */
  g_main_context_invoke(NULL, open_on_main_cb, a);
}

/* --- Public API --- */

/* New API: only width is required; height is derived from embedded map aspect ratio. */
void open_greyline_win(int window_width, const char *locator) {
  GtkWindow *parent = find_active_parent_window();
  open_async(parent, window_width, 0, locator);
}

void open_greyline_win_for_parent(GtkWindow *parent, int window_width, const char *locator) {
  open_async(parent, window_width, 0, locator);
}

/* Backward-compatible helpers: explicit width/height. */
void open_greyline_win_wh(int window_width, int window_height, const char *locator) {
  GtkWindow *parent = find_active_parent_window();
  open_async(parent, window_width, window_height, locator);
}

void open_greyline_win_for_parent_wh(GtkWindow *parent,
                                     int window_width,
                                     int window_height,
                                     const char *locator) {
  open_async(parent, window_width, window_height, locator);
}
