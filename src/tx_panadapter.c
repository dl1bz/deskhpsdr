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

#include "appearance.h"
#include "agc.h"
#include "band.h"
#include "discovered.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "rx_panadapter.h"
#include "tx_panadapter.h"
#include "vfo.h"
#include "mode.h"
#include "actions.h"
#ifdef GPIO
  #include "gpio.h"
#endif
#include "ext.h"
#include "new_menu.h"
#include "message.h"
#include <wdsp.h>

#define TX_PAN_DECAY_MAX_TX 16
static float *tx_pan_decay_db[TX_PAN_DECAY_MAX_TX];
static int    tx_pan_decay_sz[TX_PAN_DECAY_MAX_TX];
static int    tx_pan_decay_enabled_last[TX_PAN_DECAY_MAX_TX];
static int    tx_pan_decay_duplex_last[TX_PAN_DECAY_MAX_TX];

static void tx_pan_decay_reset(TRANSMITTER *tx) {
  if (!tx) { return; }

  if (tx->id < 0 || tx->id >= TX_PAN_DECAY_MAX_TX) { return; }

  if (tx_pan_decay_db[tx->id]) {
    g_free(tx_pan_decay_db[tx->id]);
    tx_pan_decay_db[tx->id] = NULL;
  }

  tx_pan_decay_sz[tx->id] = 0;
}

/* Create a new surface of the appropriate size to store our scribbles */
static gboolean
tx_panadapter_configure_event_cb (GtkWidget         *widget,
                                  GdkEventConfigure *event,
                                  gpointer           data) {
  TRANSMITTER *tx = (TRANSMITTER *)data;
  int mywidth = gtk_widget_get_allocated_width (tx->panadapter);
  int myheight = gtk_widget_get_allocated_height (tx->panadapter);

  if (tx->panadapter_surface) {
    cairo_surface_destroy (tx->panadapter_surface);
  }

  tx->panadapter_surface = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
                           CAIRO_CONTENT_COLOR,
                           mywidth,
                           myheight);
  cairo_t *cr = cairo_create(tx->panadapter_surface);
  cairo_set_source_rgba(cr, COLOUR_PAN_BACKGND);
  cairo_paint(cr);
  cairo_destroy(cr);
  return TRUE;
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static gboolean
tx_panadapter_draw_cb (GtkWidget *widget,
                       cairo_t   *cr,
                       gpointer   data) {
  TRANSMITTER *tx = (TRANSMITTER *)data;

  if (tx->panadapter_surface) {
    cairo_set_source_surface (cr, tx->panadapter_surface, 0.0, 0.0);
    cairo_paint (cr);
  }

  return FALSE;
}

static inline double tx_norm_db(double v, double min_db, double max_db) {
  if (v < min_db) { v = min_db; }

  if (v > max_db) { v = max_db; }

  return 100.0 * (v - min_db) / (max_db - min_db);
}

// Render der Levelanzeigen in die Offscreen-Surface des Zusatzfensters
static void tx_levels_render(TRANSMITTER *tx) {
  if (!tx || !tx->levels_surface || !tx->levels_area) { return; }

  // Maße vom Widget holen, nicht aus der Surface
  int w = gtk_widget_get_allocated_width(tx->levels_area);
  int h = gtk_widget_get_allocated_height(tx->levels_area);
  char level_label[32];
  double mic_db, eq_db, lvl_db, cfc_db, prc_db, out_db;

  // Rohwerte (dB)
  if (tx->show_af_peak) {
    mic_db = GetTXAMeter(tx->id, TXA_MIC_PK);
    eq_db  = GetTXAMeter(tx->id, TXA_EQ_PK);
    lvl_db = GetTXAMeter(tx->id, TXA_LVLR_PK);
    cfc_db = GetTXAMeter(tx->id, TXA_CFC_PK);
    prc_db = GetTXAMeter(tx->id, TXA_COMP_PK);
    out_db = GetTXAMeter(tx->id, TXA_OUT_PK);
  } else {
    mic_db = GetTXAMeter(tx->id, TXA_MIC_AV);
    eq_db  = GetTXAMeter(tx->id, TXA_EQ_AV);
    lvl_db = GetTXAMeter(tx->id, TXA_LVLR_AV);
    cfc_db = GetTXAMeter(tx->id, TXA_CFC_AV);
    prc_db = GetTXAMeter(tx->id, TXA_COMP_AV);
    out_db = GetTXAMeter(tx->id, TXA_OUT_AV);
  }

  double alc_db = GetTXAMeter(tx->id, TXA_ALC_PK);

  // Unterkante auf -100 dB begrenzen
  if (mic_db < -100.0) { mic_db = -100.0; }

  if (eq_db  < -100.0) { eq_db  = -100.0; }

  if (lvl_db < -100.0) { lvl_db = -100.0; }

  if (cfc_db < -100.0) { cfc_db = -100.0; }

  if (prc_db < -100.0) { prc_db = -100.0; }

  if (out_db < -100.0) { out_db = -100.0; }

  if (alc_db < -100.0) { alc_db = -100.0; }

  double val_db[7];
  val_db[0] = mic_db;
  val_db[1] = eq_db;
  val_db[2] = lvl_db;
  val_db[3] = cfc_db;
  val_db[4] = prc_db;
  val_db[5] = alc_db;
  val_db[6] = out_db;
  // Mapping: Mic/EQ/Lev/CFC/PROC = [-60..+10] dB, OUT = [-40..+10] dB
  double pct_vals[7];
  pct_vals[0] = tx_norm_db(mic_db, -60.0, 10.0);
  pct_vals[1] = tx_norm_db(eq_db,  -60.0, 10.0);
  pct_vals[2] = tx_norm_db(lvl_db, -60.0, 10.0);
  pct_vals[3] = tx_norm_db(cfc_db, -60.0, 10.0);
  pct_vals[4] = tx_norm_db(prc_db, -60.0, 10.0);
  pct_vals[5] = tx_norm_db(alc_db, -60.0, 10.0);
  pct_vals[6] = tx_norm_db(out_db, -40.0, 10.0); // Full-scale = +10 dB
  const char *labels[] = {"Mic", "TX-EQ", "Leveler", "CFC", "PROC", "ALC", "Out"};
  const int N = 7;
  cairo_t *cr = cairo_create(tx->levels_surface);
  cairo_set_source_rgba(cr, COLOUR_PAN_BACKGND);
  cairo_paint(cr);
  int margin = 20;
  int bar_h  = ((h - 2 * margin) / N) * 0.4;  // 40 % der alten Höhe

  if (bar_h < 4) { bar_h = 4; }

  int bar_w  = w - 2 * margin;
  int y = margin;

  for (int i = 0; i < N; i++) {
    // Label
    cairo_set_source_rgba(cr, COLOUR_PAN_TEXT);
    cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#ifdef __APPLE__
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
#else
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE1);
#endif
    cairo_move_to(cr, margin, y - 2);
    snprintf(level_label, sizeof(level_label), "%d - %s", (int)i, labels[i]);
    cairo_show_text(cr, level_label);
    cairo_move_to(cr, w / 2 - 20, y - 2);

    if (i == 5) {
      snprintf(level_label, sizeof(level_label), "%+.1f db", val_db[i]);
    } else {
      snprintf(level_label, sizeof(level_label), "%+.1f dbV", val_db[i]);
    }

    cairo_show_text(cr, level_label);
    cairo_move_to(cr, w - 35, y - 2);
    snprintf(level_label, sizeof(level_label), "+10");
    cairo_set_source_rgba(cr, COLOUR_WHITE);
    cairo_show_text(cr, level_label);
    cairo_set_source_rgba(cr, COLOUR_PAN_TEXT);
    // --- Segmentierte Hintergrund-Skala (4 Bereiche) ---
    double min_db = (i == 6) ? -40.0 : -60.0;
    double max_db = 10.0;
    double px_per_db = bar_w / (max_db - min_db);
    double x_min  = margin;
    double x_neg10 = margin + (-10.0 - min_db) * px_per_db;
    double x_neg5 = margin + (-5.0 - min_db) * px_per_db;
    double x_0    = margin + (0.0  - min_db) * px_per_db;
    double x_max  = margin + bar_w;

    if (x_neg10 > x_min) {
      cairo_set_line_width(cr, PAN_LINE_THIN);
      cairo_set_source_rgba(cr, COLOUR_PAN_LINE_WEAK);
      cairo_rectangle(cr, x_min, y + 4, x_neg10 - x_min, bar_h);
      cairo_stroke(cr);
    }

    if (x_neg5 > x_neg10) {
      cairo_set_source_rgba(cr, COLOUR_OK_WEAK);
      cairo_rectangle(cr, x_neg10, y + 4, x_0 - x_neg10, bar_h);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, COLOUR_PAN_LINE_WEAK);
      cairo_stroke(cr);
    }

    if (x_0 > x_neg5) {
      cairo_set_source_rgba(cr, COLOUR_ORANGE);
      cairo_rectangle(cr, x_neg5, y + 4, x_0 - x_neg5, bar_h);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, COLOUR_PAN_LINE_WEAK);
      cairo_stroke(cr);
    }

    if (x_max > x_0) {
      cairo_set_source_rgba(cr, COLOUR_ALARM_WEAK);
      cairo_rectangle(cr, x_0, y + 4, x_max - x_0, bar_h);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, COLOUR_PAN_LINE_WEAK);
      cairo_stroke(cr);
    }

    // --- Pegelbalken darüber ---
    double fill_w = (pct_vals[i] / 100.0) * bar_w;

    if (fill_w < 0) { fill_w = 0; }

    if (fill_w > bar_w) { fill_w = bar_w; }

    cairo_set_source_rgba(cr, COLOUR_WHITE);
    cairo_rectangle(cr, margin, y + 4, fill_w, bar_h);
    cairo_fill(cr);
    y += bar_h + 20;
  }

  cairo_destroy(cr);
}

// cppcheck-suppress constParameterCallback
static gboolean tx_panadapter_button_press_event_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  switch (event->button) {
  case GDK_BUTTON_SECONDARY:
    g_idle_add(ext_start_tx, NULL);
    break;

  default:
    // do nothing for left mouse button
    break;
  }

  return TRUE;
}

void tx_panadapter_update(TRANSMITTER *tx) {
  if (!tx || !tx->panadapter_surface) {
    return;
  }

  if (tx->panadapter_surface) {
    int mywidth = gtk_widget_get_allocated_width (tx->panadapter);
    int myheight = gtk_widget_get_allocated_height (tx->panadapter);
    int txvfo = vfo_get_tx_vfo();
    int txmode = vfo_get_tx_mode();
    double filter_left, filter_right;
    float *samples = tx->pixel_samples;
    // double hz_per_pixel = (double)tx->iq_output_rate / (double)tx->pixels;
    double hz_per_pixel = 24000.0 / (double)tx->pixels;
    cairo_t *cr;
    cr = cairo_create (tx->panadapter_surface);
    cairo_set_source_rgba(cr, COLOUR_PAN_BACKGND);
    cairo_paint (cr);
    // filter
    filter_left = filter_right = 0.5 * mywidth;
    static double _mic_av;
    static double _eq_av;
    static double _lvlr_av;
    static double _cfc_av;
    static double _proc_av;
    static double _out_av;

    if (txmode != modeCWU && txmode != modeCWL) {
      cairo_set_source_rgba(cr, COLOUR_PAN_FILTER);

      if (txmode == modeFMN) {
        //
        // The bandpass filter used in FM  is applied *before* the FM
        // modulator. Here we use Carson's rule to determine the "true"
        // TX bandwidth
        filter_left = (double)mywidth / 2.0 + ((double)(tx->filter_low - tx->deviation) / hz_per_pixel);
        filter_right = (double)mywidth / 2.0 + ((double)(tx->filter_high + tx->deviation) / hz_per_pixel);
      } else {
        filter_left = (double)mywidth / 2.0 + ((double)tx->filter_low / hz_per_pixel);
        filter_right = (double)mywidth / 2.0 + ((double)tx->filter_high / hz_per_pixel);
      }

      cairo_rectangle(cr, filter_left, 0.0, filter_right - filter_left, (double)myheight);
      cairo_fill(cr);
    }

    // plot the levels   0, -20,  40, ... dBm (bright turquoise line with label)
    // additionally, plot the levels in steps of the chosen panadapter step size
    // (dark turquoise line without label)
    double dbm_per_line = (double)myheight / ((double)tx->panadapter_high - (double)tx->panadapter_low);
    cairo_set_source_rgba(cr, COLOUR_PAN_LINE);
    cairo_set_line_width(cr, PAN_LINE_THICK);
    cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);

    for (int i = tx->panadapter_high; i >= tx->panadapter_low; i--) {
      if ((abs(i) % tx->panadapter_step) == 0) {
        double y = (double)(tx->panadapter_high - i) * dbm_per_line;

        if ((abs(i) % 20) == 0) {
          char v[32];
          cairo_set_source_rgba(cr, COLOUR_PAN_LINE_WEAK);
          cairo_move_to(cr, 0.0, y);
          cairo_line_to(cr, (double)mywidth, y);
          snprintf(v, 32, "%d dBm", i);
          cairo_move_to(cr, 1, y);
          cairo_show_text(cr, v);
          cairo_stroke(cr);
        } else {
          cairo_set_source_rgba(cr, COLOUR_PAN_LINE_WEAK);
          cairo_move_to(cr, 0.0, y);
          cairo_line_to(cr, (double)mywidth, y);
          cairo_stroke(cr);
        }
      }
    }

    // plot frequency markers
    long long half = tx->dialog ? 3000LL : 12000LL; //(long long)(tx->output_rate/2);
    long long frequency;

    if (vfo[txvfo].ctun) {
      frequency = vfo[txvfo].ctun_frequency;
    } else {
      frequency = vfo[txvfo].frequency;
    }

    double vfofreq = (double)mywidth * 0.5;
    long long min_display = frequency - half;
    long long max_display = frequency + half;

    if (tx->dialog == NULL) {
      long long f;
      const long long divisor = 5000;
      //
      // in DUPLEX, space in the TX window is so limited
      // that we cannot print the frequencies
      //
      cairo_set_source_rgba(cr, COLOUR_PAN_LINE);
      cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
      cairo_set_line_width(cr, PAN_LINE_THIN);
      cairo_text_extents_t extents;
      f = ((min_display / divisor) * divisor) + divisor;

      while (f < max_display) {
        double x = (double)(f - min_display) / hz_per_pixel;

        //
        // Skip vertical line if it is in the filter area, since
        // one might want to see a PureSignal Feedback there
        // without any distraction.
        //
        if (x < filter_left || x > filter_right) {
          cairo_move_to(cr, x, 10.0);
          cairo_line_to(cr, x, (double)myheight);
        }

        //
        // For frequency marker lines very close to the left or right
        // edge, do not print a frequency since this probably won't fit
        // on the screen
        //
        if ((f >= min_display + divisor / 2) && (f <= max_display - divisor / 2)) {
          char v[32];

          //
          // For frequencies larger than 10 GHz, we cannot
          // display all digits here
          //
          if (f > 10000000000LL) {
            snprintf(v, 32, "...%03lld.%03lld", (f / 1000000) % 1000, (f % 1000000) / 1000);
          } else {
            snprintf(v, 32, "%0lld.%03lld", f / 1000000, (f % 1000000) / 1000);
          }

          cairo_text_extents(cr, v, &extents);
          cairo_move_to(cr, x - (extents.width / 2.0), 10.0);
          cairo_show_text(cr, v);
        }

        f += divisor;
      }

      cairo_stroke(cr);
    }

    // band edges
    int b = vfo[txvfo].band;
    const BAND *band = band_get_band(b);

    if (band->frequencyMin != 0LL) {
      cairo_set_source_rgba(cr, COLOUR_ALARM);
      cairo_set_line_width(cr, PAN_LINE_EXTRA);

      if ((min_display < band->frequencyMin) && (max_display > band->frequencyMin)) {
        int i = (band->frequencyMin - min_display) / (long long)hz_per_pixel;
        cairo_move_to(cr, (double)i, 0.0);
        cairo_line_to(cr, (double)i, (double)myheight);
        cairo_stroke(cr);
      }

      if ((min_display < band->frequencyMax) && (max_display > band->frequencyMax)) {
        int i = (band->frequencyMax - min_display) / (long long)hz_per_pixel;
        cairo_move_to(cr, (double)i, 0.0);
        cairo_line_to(cr, (double)i, (double)myheight);
        cairo_stroke(cr);
      }
    }

    // cursor
    cairo_set_source_rgba(cr, COLOUR_ALARM);
    cairo_set_line_width(cr, PAN_LINE_THIN);
    //t_print("cursor: x=%f\n",(double)(mywidth/2.0)+(vfo[tx->id].offset/hz_per_pixel));
    cairo_move_to(cr, vfofreq, 0.0);
    cairo_line_to(cr, vfofreq, (double)myheight);
    cairo_stroke(cr);
    // signal
    /* Peak decay line state update (fast attack / slow release) */
    float *decay_db = NULL;

    if (tx->id >= 0 && tx->id < TX_PAN_DECAY_MAX_TX) {
      if (tx_pan_decay_duplex_last[tx->id] != duplex) {
        tx_pan_decay_duplex_last[tx->id] = duplex;
        tx_pan_decay_reset(tx);
      }

      int decay_enabled =
        pan_peak_hold_enabled && pan_peak_hold_decay_db_per_sec > 0.0f;

      if (tx_pan_decay_enabled_last[tx->id] != decay_enabled) {
        tx_pan_decay_enabled_last[tx->id] = decay_enabled;
        tx_pan_decay_reset(tx);
      }

      if (decay_enabled) {
        if (tx_pan_decay_sz[tx->id] != tx->pixels) {
          tx_pan_decay_reset(tx);
          tx_pan_decay_db[tx->id] = g_new0(float, tx->pixels);
          tx_pan_decay_sz[tx->id] = tx->pixels;

          /* initialize with current spectrum to avoid a ramp-in artifact */
          for (int i = 0; i < tx->pixels; i++) {
            tx_pan_decay_db[tx->id][i] = samples[i];
          }
        } else if (tx_pan_decay_db[tx->id] == NULL && tx->pixels > 0) {
          tx_pan_decay_db[tx->id] = g_new0(float, tx->pixels);
          tx_pan_decay_sz[tx->id] = tx->pixels;

          for (int i = 0; i < tx->pixels; i++) {
            tx_pan_decay_db[tx->id][i] = samples[i];
          }
        }

        if (tx_pan_decay_db[tx->id]) {
          float decay_db_per_frame = 0.0f;

          if (tx->fps > 0.0f) {
            decay_db_per_frame = pan_peak_hold_decay_db_per_sec / tx->fps;
          } else {
            /* fps not available yet -> keep peak-hold line visible, no release */
            decay_db_per_frame = 0.0f;
          }

          if (decay_db_per_frame < 0.0f) { decay_db_per_frame = 0.0f; }

          for (int i = 0; i < tx->pixels; i++) {
            float cur = samples[i];
            float prev = tx_pan_decay_db[tx->id][i];

            if (cur >= prev) {
              tx_pan_decay_db[tx->id][i] = cur; /* fast attack */
            } else {
              float v = prev - decay_db_per_frame;
              tx_pan_decay_db[tx->id][i] = (v > cur) ? v : cur; /* slow release */
            }
          }

          decay_db = tx_pan_decay_db[tx->id];
        }
      }
    }

    double s1;
    int draw_width = mywidth;

    if (draw_width > tx->pixels) { draw_width = tx->pixels; }

    int offset = (tx->pixels / 2) - (draw_width / 2);

    if (offset < 0) { offset = 0; }

    if (offset + draw_width > tx->pixels) { offset = tx->pixels - draw_width; }

    if (draw_width > 0) {
      samples[offset] = -200.0f;
      samples[offset + draw_width - 1] = -200.0f;
    }

    s1 = (double)samples[offset];
    s1 = floor((tx->panadapter_high - s1)
               * (double) myheight
               / (tx->panadapter_high - tx->panadapter_low));
    cairo_move_to(cr, 0.0, s1);
    int span = draw_width;
    int den = (mywidth > 1) ? (mywidth - 1) : 1;

    for (int x = 1; x < mywidth; x++) {
      int idx = offset + (int)(((long long)x * (long long)(span - 1)) / (long long)den);
      double s2 = (double)samples[idx];
      s2 = floor((tx->panadapter_high - s2)
                 * (double) myheight
                 / (tx->panadapter_high - tx->panadapter_low));
      cairo_line_to(cr, (double)x, s2);
    }

    if (tx->display_filled) {
      // cairo_set_source_rgba(cr, COLOUR_PAN_FILL2);
      cairo_set_source_rgba(cr,
                            tx_pan_fill_col.r,
                            tx_pan_fill_col.g,
                            tx_pan_fill_col.b,
                            tx_pan_fill_col.a);
      cairo_close_path (cr);
      cairo_fill_preserve (cr);
      cairo_set_line_width(cr, PAN_LINE_THIN);
    } else {
      // cairo_set_source_rgba(cr, COLOUR_PAN_FILL3);
      cairo_set_source_rgba(cr,
                            tx_pan_fill_col.r,
                            tx_pan_fill_col.g,
                            tx_pan_fill_col.b,
                            tx_pan_fill_col.a);
      cairo_set_line_width(cr, PAN_LINE_THICK);
    }

    cairo_stroke(cr);

    /* Draw peak-decay line (no fill) */
    // if (decay_db) {
    if (!duplex && decay_db && draw_width > 0 && offset >= 0 && offset + draw_width <= tx->pixels) {
      double d1;
      d1 = (double)decay_db[offset];
      d1 = floor((tx->panadapter_high - d1)
                 * (double) myheight
                 / (tx->panadapter_high - tx->panadapter_low));
      cairo_move_to(cr, 0.0, d1);
      int span = draw_width;
      int den = (mywidth > 1) ? (mywidth - 1) : 1;

      for (int x = 1; x < mywidth; x++) {
        int idx = offset + (int)(((long long)x * (long long)(span - 1)) / (long long)den);
        double d2 = (double)decay_db[idx];
        d2 = floor((tx->panadapter_high - d2)
                   * (double) myheight
                   / (tx->panadapter_high - tx->panadapter_low));
        cairo_line_to(cr, (double)x, d2);
      }

      cairo_set_source_rgba(cr,
                            peak_line_col.r,
                            peak_line_col.g,
                            peak_line_col.b,
                            peak_line_col.a);
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_stroke(cr);
    }

    //
    // When doing CW, the signal is produced outside WDSP, so
    // it makes no sense to display a PureSignal status. The
    // only exception is if we are running twotone from
    // within the PS menu.
    //
    int cwmode = (txmode == modeCWL || txmode == modeCWU) && !tx->twotone;

    if (tx->puresignal && !cwmode) {
      cairo_set_source_rgba(cr, COLOUR_OK);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
      cairo_move_to(cr, mywidth / 2 + 10, myheight - 10);
      cairo_show_text(cr, "PureSignal");
      int info[16];
      tx_ps_getinfo(tx, info);

      if (info[14] == 0) {
        cairo_set_source_rgba(cr, COLOUR_ALARM);
      } else {
        cairo_set_source_rgba(cr, COLOUR_OK);
      }

      if (tx->dialog) {
        cairo_move_to(cr, (mywidth / 2) + 10, myheight - 30);
      } else {
        cairo_move_to(cr, (mywidth / 2) + 110, myheight - 10);
      }

      cairo_show_text(cr, "Correcting");
    }

    if (tx->dialog) {
      char text[64];
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
      int _xpos = 0;
      int _ypos = 80;

      if (vfo_get_tx_mode() == modeLSB || vfo_get_tx_mode() == modeDIGL) {
        _xpos = 130;
      } else {
        _xpos = 10;
      }

      cairo_set_source_rgba(cr, COLOUR_ORANGE);
      cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      int row = 0;

      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        //
        // Power values not available for SoapySDR
        //
        snprintf(text, 64, "FWD %0.1f W", transmitter->fwd);
        row += 20;
        cairo_move_to(cr, _xpos, row);
        cairo_show_text(cr, text);
        //
        // Since colour is already red, no special
        // action for "high SWR" warning
        //
        snprintf(text, 64, "SWR 1:%1.1f", transmitter->swr);
        row += 20;
        cairo_move_to(cr, _xpos, row);
        cairo_show_text(cr, text);
      }

      if (!cwmode) {
        row += 20;
        snprintf(text, 64, "ALC %2.1f dB", transmitter->alc);
        cairo_move_to(cr, _xpos, row);
        cairo_show_text(cr, text);
      }

      if (duplex && !cwmode && !tx->show_levels) {
        cairo_set_source_rgba(cr, COLOUR_METER);  // revert to white color
        cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);

        if (!tune) {
          // additional display levels of the audio chain
          _mic_av = GetTXAMeter(transmitter->id, TXA_MIC_AV);

          if (_mic_av < -100.0) {
            _mic_av = 0.0;
          }

          _eq_av = GetTXAMeter(transmitter->id, TXA_EQ_AV);

          if (_eq_av < -100.0) {
            _eq_av = 0.0;
          }

          _lvlr_av = GetTXAMeter(transmitter->id, TXA_LVLR_AV);

          if (_lvlr_av < -100.0) {
            _lvlr_av = 0.0;
          }

          _cfc_av = GetTXAMeter(transmitter->id, TXA_CFC_AV);

          if (_cfc_av < -100.0) {
            _cfc_av = 0.0;
          }

          _proc_av = GetTXAMeter(transmitter->id, TXA_COMP_AV);

          if (_proc_av < -100.0) {
            _proc_av = 0.0;
          }

          _out_av = GetTXAMeter(transmitter->id, TXA_OUT_AV);

          if (_mic_av > 0.0) {
            cairo_set_source_rgba(cr, COLOUR_ALARM);
          } else {
            cairo_set_source_rgba(cr, COLOUR_METER);
          }

          cairo_move_to(cr, _xpos, _ypos);
          snprintf(text, 64, "Mic %.0f", _mic_av);
          cairo_show_text(cr, text);
          cairo_set_source_rgba(cr, COLOUR_METER);

          if (vfo_get_tx_mode() == modeDIGL || vfo_get_tx_mode() == modeDIGU) {
            cairo_set_source_rgba(cr, COLOUR_SHADE);
          }

          snprintf(text, 64, "EQ  %.0f", _eq_av);
          _ypos += 20;
          cairo_move_to(cr, _xpos, _ypos);
          cairo_show_text(cr, text);
          snprintf(text, 64, "Lev %.0f", _lvlr_av);
          _ypos += 20;
          cairo_move_to(cr, _xpos, _ypos);
          cairo_show_text(cr, text);
          snprintf(text, 64, "CFC %.0f", _cfc_av);
          _ypos += 20;
          cairo_move_to(cr, _xpos, _ypos);
          cairo_show_text(cr, text);
          snprintf(text, 64, "PROC %.0f", _proc_av);
          _ypos += 20;
          cairo_move_to(cr, _xpos, _ypos);
          cairo_show_text(cr, text);
          cairo_set_source_rgba(cr, COLOUR_METER);

          if (_out_av > 0.0) {
            cairo_set_source_rgba(cr, COLOUR_ALARM);
          } else {
            cairo_set_source_rgba(cr, COLOUR_METER);
          }

          snprintf(text, 64, "Out %.0f", _out_av);
          _ypos += 20;
          cairo_move_to(cr, _xpos, _ypos);
          cairo_show_text(cr, text);
          cairo_set_source_rgba(cr, COLOUR_METER);
        }
      }
    }

    if (tx->panadapter_peaks_on != 0) {
      int num_peaks = tx->panadapter_num_peaks;
      /*
      gboolean peaks_in_passband = TRUE;

      if (tx->panadapter_peaks_in_passband_filled != 1) {
        peaks_in_passband = FALSE;
      }

      gboolean hide_noise = TRUE;

      if (tx->panadapter_hide_noise_filled != 1) {
        hide_noise = FALSE;
      }
      */
      gboolean peaks_in_passband = SET(tx->panadapter_peaks_in_passband_filled);
      gboolean hide_noise = SET(tx->panadapter_hide_noise_filled);
      double noise_percentile = (double)tx->panadapter_ignore_noise_percentile;
      int ignore_range_divider = tx->panadapter_ignore_range_divider;
      int ignore_range = (mywidth + ignore_range_divider - 1) / ignore_range_divider; // Round up
      double peaks[num_peaks];
      int peak_positions[num_peaks];

      for (int a = 0; a < num_peaks; a++) {
        peaks[a] = -200;
        peak_positions[a] = 0;
      }

      /*
      // Dynamically allocate a copy of samples for sorting
      double *sorted_samples = malloc(mywidth * sizeof(double));

      if (sorted_samples == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        return; // Handle memory allocation failure
      }

      for (int i = 0; i < mywidth; i++) {
        sorted_samples[i] = (double)samples[i + offset];
      }
      */
      // Calculate the noise level if needed
      double noise_level = 0.0;

      if (hide_noise) {
        // Dynamically allocate a copy of samples for sorting
        double *sorted_samples = malloc(mywidth * sizeof(double));

        if (sorted_samples != NULL) {
          for (int i = 0; i < mywidth; i++) {
            sorted_samples[i] = (double)samples[i + offset];
          }

          qsort(sorted_samples, mywidth, sizeof(double), compare_doubles);
          int index = (int)((noise_percentile / 100.0) * mywidth);
          // noise_level = sorted_samples[index];
          noise_level = sorted_samples[index] + 3.0;
          free(sorted_samples); // Free memory after use
        }
      }

      // free(sorted_samples); // Free memory after use
      // Detect peaks
      double filter_left_bound = peaks_in_passband ? filter_left : 0;
      double filter_right_bound = peaks_in_passband ? filter_right : mywidth;

      for (int i = 1; i < mywidth - 1; i++) {
        if (i >= filter_left_bound && i <= filter_right_bound) {
          double s = (double)samples[i + offset];

          // Check if the point is a peak
          if ((!hide_noise || s >= noise_level) && s > samples[i - 1 + offset] && s > samples[i + 1 + offset]) {
            int replace_index = -1;
            int start_range = i - ignore_range;
            int end_range = i + ignore_range;

            // Check if the peak is within the ignore range of any existing peak
            for (int j = 0; j < num_peaks; j++) {
              if (peak_positions[j] >= start_range && peak_positions[j] <= end_range) {
                if (s > peaks[j]) {
                  replace_index = j;
                  break;
                } else {
                  replace_index = -2;
                  break;
                }
              }
            }

            // Replace the existing peak if a higher peak is found within the ignore range
            if (replace_index >= 0) {
              peaks[replace_index] = s;
              peak_positions[replace_index] = i;
            }
            // Add the peak if no peaks are found within the ignore range
            else if (replace_index == -1) {
              // Find the index of the lowest peak
              int lowest_peak_index = 0;

              for (int j = 1; j < num_peaks; j++) {
                if (peaks[j] < peaks[lowest_peak_index]) {
                  lowest_peak_index = j;
                }
              }

              // Replace the lowest peak if the current peak is higher
              if (s > peaks[lowest_peak_index]) {
                peaks[lowest_peak_index] = s;
                peak_positions[lowest_peak_index] = i;
              }
            }
          }
        }
      }

      // Sort peaks in descending order
      for (int i = 0; i < num_peaks - 1; i++) {
        for (int j = i + 1; j < num_peaks; j++) {
          if (peaks[i] < peaks[j]) {
            double temp_peak = peaks[i];
            peaks[i] = peaks[j];
            peaks[j] = temp_peak;
            int temp_pos = peak_positions[i];
            peak_positions[i] = peak_positions[j];
            peak_positions[j] = temp_pos;
          }
        }
      }

      // Draw peak values on the chart
      // #define COLOUR_PAN_TEXT 1.0, 1.0, 1.0, 1.0 // Define white color with full opacity
      cairo_set_source_rgba(cr, COLOUR_PAN_TEXT); // Set text color
      cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
      double previous_text_positions[num_peaks][2]; // Store previous text positions (x, y)

      for (int j = 0; j < num_peaks; j++) {
        previous_text_positions[j][0] = -1; // Initialize x positions
        previous_text_positions[j][1] = -1; // Initialize y positions
      }

      for (int j = 0; j < num_peaks; j++) {
        if (peak_positions[j] > 0) {
          char peak_label[32];
          snprintf(peak_label, sizeof(peak_label), "%.1f dBm", peaks[j]);
          cairo_text_extents_t extents;
          cairo_text_extents(cr, peak_label, &extents);
          // Calculate initial text position: slightly above the peak
          double text_x = peak_positions[j];
          double text_y = floor((tx->panadapter_high - peaks[j])
                                * (double)myheight
                                / (tx->panadapter_high - tx->panadapter_low)) - 5;

          // Ensure text stays within the drawing area
          if (text_y < extents.height) {
            text_y = extents.height; // Push text down to fit inside the top boundary
          }

          // Adjust position to avoid overlap with previous labels
          for (int k = 0; k < j; k++) {
            double prev_x = previous_text_positions[k][0];
            double prev_y = previous_text_positions[k][1];

            if (prev_x >= 0 && prev_y >= 0) {
              double distance_x = fabs(text_x - prev_x);
              double distance_y = fabs(text_y - prev_y);

              if (distance_y < extents.height && distance_x < extents.width) {
                // Try moving vertically first
                if (text_y + extents.height < myheight) {
                  text_y += extents.height + 5; // Move below
                } else if (text_y - extents.height > 0) {
                  text_y -= extents.height + 5; // Move above
                } else {
                  // Move horizontally if no vertical space is available
                  if (text_x + extents.width < mywidth) {
                    text_x += extents.width + 5; // Move right
                  } else if (text_x - extents.width > 0) {
                    text_x -= extents.width + 5; // Move left
                  }
                }
              }
            }
          }

          // Draw text
          cairo_move_to(cr, text_x - (extents.width / 2.0), text_y);
          cairo_show_text(cr, peak_label);
          // Store current text position for overlap checks
          previous_text_positions[j][0] = text_x;
          previous_text_positions[j][1] = text_y;
        }
      }
    }

    if (tx->dialog == NULL) {
      display_panadapter_messages(cr, mywidth, tx->fps);
    }

    cairo_destroy (cr);
    gtk_widget_queue_draw (tx->panadapter);
  }

  // Zusatzfenster aktualisieren, falls aktiv
  if (tx->levels_surface && tx->levels_area) {
    tx_levels_render(tx);
    gtk_widget_queue_draw(tx->levels_area);
  }
}

void tx_panadapter_init(TRANSMITTER *tx, int width, int height) {
  t_print("tx_panadapter_init: %d x %d\n", width, height);
  tx->panadapter_surface = NULL;
  tx->panadapter = gtk_drawing_area_new ();
  gtk_widget_set_size_request (tx->panadapter, width, height);
  /* Signals used to handle the backing surface */
  g_signal_connect (tx->panadapter, "draw", G_CALLBACK (tx_panadapter_draw_cb), tx);
  g_signal_connect (tx->panadapter, "configure-event", G_CALLBACK (tx_panadapter_configure_event_cb), tx);
  /* Event signals */
  //
  // The only signal we do process is to start the TX menu if clicked with the right mouse button
  //
  g_signal_connect (tx->panadapter, "button-press-event", G_CALLBACK (tx_panadapter_button_press_event_cb), tx);
  /*
   * Enable button press events
   */
  gtk_widget_set_events (tx->panadapter, gtk_widget_get_events (tx->panadapter) | GDK_BUTTON_PRESS_MASK);
}
