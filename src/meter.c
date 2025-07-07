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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "appearance.h"
#include "band.h"
#include "receiver.h"
#include "meter.h"
#include "radio.h"
#include "radio.h"
#include "version.h"
#include "mode.h"
#include "vox.h"
#include "new_menu.h"
#include "vfo.h"
#include "message.h"

#include <wdsp.h>

#if defined (__LDESK__)
//----------------------------------------------------------------------------------------------
// Reference of calculate S-Meter values: https://de.wikipedia.org/wiki/S-Meter
// <= 30 MHz: S9 = -73dbm
//  > 30 MHz: S9 = -93dbm

#define NUM_SWERTE 19   /* Number of S-Werte */

// lower limits <= 30 MHz
static short int lowlimitsHF[NUM_SWERTE] = {
  -200, -121, -115, -109, -103, -97, -91, -85, -79, -73, -68, -63, -58, -53, -48, -43, -33, -23, -13
    //      S1    S2    S3    S4    S5   S6   S7   S8   S9   +5   +10  +15  +20  +25  +30  +40  +50  +60
  };
// upper limits <= 30 MHz
static short int uplimitsHF[NUM_SWERTE] = {
  -122, -116, -110, -104, -98, -92, -86, -80, -74, -69, -64, -59, -54, -49, -44, -34, -24, -14, 0
  };

// lower limits > 30 MHz
static short int lowlimitsUKW[NUM_SWERTE] = {
  -200, -141, -135, -129, -123, -117, -111, -105, -99, -93, -88, -83, -78, -73, -68, -63, -53, -43, -33
    //      S1    S2    S3    S4    S5    S6    S7    S8   S9   +5   +10  +15  +20  +25  +30  +40  +50  +60
  };
// upper limits > 30 MHz
static short int uplimitsUKW[NUM_SWERTE] = {
  -142, -136, -130, -124, -118, -112, -106, -100, -94, -89, -84, -79, -74, -69, -64, -54, -44, -34, 0
  };

static const char* (dbm2smeter[NUM_SWERTE + 1]) = {
  "no signal", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "S9", "S9+5db", "S9+10db", "S9+15db", "S9+20db", "S9+25db", "S9+30db", "S9+40db", "S9+50db", "S9+60db", "out of range"
};


static unsigned char get_SWert(short int dbm) {
  int i;

  for (i = 0; i < NUM_SWERTE; i++) {
    // if VFO > 30 MHz reference S9 = -93dbm
    if (vfo[active_receiver->id].frequency > 30000000LL) {
      if ((dbm >= lowlimitsUKW[i]) && (dbm <= uplimitsUKW[i])) {
        return i;
      }

      // if VFO <= 30 MHz reference S9 = -73dbm
    } else {
      if ((dbm >= lowlimitsHF[i]) && (dbm <= uplimitsHF[i])) {
        return i;
      }
    }
  }

  return NUM_SWERTE; // no valid S-Werte -> return not defined
}

//----------------------------------------------------------------------------------------------
#endif

static GtkWidget *meter;
static cairo_surface_t *meter_surface = NULL;

static int last_meter_type = SMETER;

#define min_rxlvl -200.0
#define min_alc   -100.0
#define min_pwr      0.0

static double max_rxlvl = min_rxlvl;
static double max_alc   = min_alc;
static double max_pwr   = min_pwr;

static int max_count = 0;

static gboolean
meter_configure_event_cb (GtkWidget         *widget,
                          GdkEventConfigure *event,
                          gpointer           data) {
  if (meter_surface) {
    cairo_surface_destroy (meter_surface);
  }

  meter_surface = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
                  CAIRO_CONTENT_COLOR, METER_WIDTH, METER_HEIGHT);
  /* Initialize the surface to black */
  cairo_t *cr;
  cr = cairo_create (meter_surface);
  cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
  cairo_paint (cr);
  cairo_destroy (cr);
  return TRUE;
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static gboolean
meter_draw_cb (GtkWidget *widget, cairo_t   *cr, gpointer   data) {
  cairo_set_source_surface (cr, meter_surface, 0.0, 0.0);
  cairo_paint (cr);
  return FALSE;
}

// cppcheck-suppress constParameterCallback
static gboolean meter_press_event_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_meter();
  return TRUE;
}

GtkWidget* meter_init(int width, int height) {
  t_print("meter_init: width=%d height=%d\n", width, height);
  meter = gtk_drawing_area_new ();
  gtk_widget_set_size_request (meter, width, height);
  /* Signals used to handle the backing surface */
  g_signal_connect (meter, "draw",
                    G_CALLBACK (meter_draw_cb), NULL);
  g_signal_connect (meter, "configure-event",
                    G_CALLBACK (meter_configure_event_cb), NULL);
  /* Event signals */
  g_signal_connect (meter, "button-press-event",
                    G_CALLBACK (meter_press_event_cb), NULL);
  gtk_widget_set_events (meter, gtk_widget_get_events (meter)
                         | GDK_BUTTON_PRESS_MASK);
  return meter;
}

void meter_update(RECEIVER *rx, int meter_type, double value, double alc, double swr) {
  /* Sofort-Fix: nichts machen, wenn noch kein Surface existiert */
  if (!rx || !meter_surface) {
    return;
  }

  cairo_t *cr = cairo_create (meter_surface);
  double rxlvl;   // only used for RX input level, clones "value"
  double pwr;     // only used for TX power, clones "value"
  char sf[32];
  int txvfo = vfo_get_tx_vfo();
  int txmode = vfo[txvfo].mode;
  int cwmode = (txmode == modeCWU || txmode == modeCWL);
  const BAND *band = band_get_band(vfo[txvfo].band);
#if defined (__LDESK__)
  double _mic_av;
  double _eq_av;
  double _lvlr_av;
  double _cfc_av;
  double _proc_av;
  double _out_av;
#endif

  //
  // First, do all the work that  does not depend on whether the
  // meter is analog or digital.
  //
  if (last_meter_type != meter_type) {
    last_meter_type = meter_type;
    //
    // reset max values
    //
    max_rxlvl = min_rxlvl;
    max_pwr   = min_pwr;
    max_alc   = min_alc;
    max_count =    0;
  }

  //
  // Only the values max_rxlvl/max_pwr/max_alc are "on display"
  // The algorithm to calculate these "sedated" values from the
  // (possibly fluctuating)  input ones is as follows:
  //
  // - if counter > CNTMAX then move max_value towards current_value by exponential averaging
  //                            with parameter EXPAV1, EXPAV2 (but never go below the minimum value)
  // - if current_value >  max_value then set max_value to current_value and reset counter
  //
  // A new max value will therefore be immediately visible, the display stays (if not surpassed) for
  // CNTMAX cycles and then the displayed value will gradually approach the new one(s).
#define CNTMAX 5
#define EXPAV1 0.75
#define EXPAV2 0.25

  switch (meter_type) {
  case POWER:
    pwr = value;

    if (max_count > CNTMAX) {
      max_pwr = EXPAV1 * max_pwr + EXPAV2 * pwr;
      max_alc = EXPAV1 * max_alc + EXPAV2 * alc;

      // This is perhaps not necessary ...
      if (max_pwr < min_pwr) { max_pwr = min_pwr; }

      // ... but alc goes to -Infinity during CW
      if (max_alc < min_alc) { max_alc = min_alc; }
    }

    if (pwr > max_pwr) {
      max_pwr = pwr;
      max_count = 0;
    }

    if (alc > max_alc) {
      max_alc = alc;
      max_count = 0;
    }

    break;

  case SMETER:
    rxlvl = value; // all corrections now in receiver.c

    if (max_count > CNTMAX) {
      max_rxlvl = EXPAV1 * max_rxlvl + EXPAV2 * rxlvl;

      if (max_rxlvl < min_rxlvl) { max_rxlvl = min_rxlvl; }
    }

    if (rxlvl > max_rxlvl) {
      max_rxlvl = rxlvl;
      max_count = 0;
    }

    break;
  }

  max_count++;

  //
  // From now on, DO NOT USE rxlvl,pwr,alc but use max_rxlvl etc.
  //
  if (analog_meter) {
    cairo_text_extents_t extents;
    cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
    cairo_paint (cr);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);

    switch (meter_type) {
    case SMETER: {
      //
      // Analog RX display
      //
      int i;
      double x;
      double y;
      double angle;
      double radians;
      double cx = (double)METER_WIDTH / 2.0;
      double radius = cx - 25.0;
      double min_angle, max_angle, bydb;

      if (cx - 0.342 * radius < METER_HEIGHT - 5) {
        min_angle = 200.0;
        max_angle = 340.0;
      } else if (cx - 0.5 * radius < METER_HEIGHT - 5) {
        min_angle = 210.0;
        max_angle = 330.0;
      } else {
        min_angle = 220.0;
        max_angle = 320.0;
      }

      bydb = (max_angle - min_angle) / 114.0;
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);
      cairo_arc(cr, cx, cx, radius, (min_angle + 6.0 * bydb) * M_PI / 180.0, max_angle * M_PI / 180.0);
      cairo_stroke(cr);
#if defined (__LDESK__)
      cairo_set_line_width(cr, 4.0);
#else
      cairo_set_line_width(cr, PAN_LINE_EXTRA);
#endif
      cairo_set_source_rgba(cr, COLOUR_ALARM);
      cairo_arc(cr, cx, cx, radius + 2, (min_angle + 54.0 * bydb) * M_PI / 180.0, max_angle * M_PI / 180.0);
      cairo_stroke(cr);
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);

      for (i = 1; i < 10; i++) {
        angle = ((double)i * 6.0 * bydb) + min_angle;
        radians = angle * M_PI / 180.0;

        if ((i % 2) == 1) {
          cairo_arc(cr, cx, cx, radius + 4, radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_arc(cr, cx, cx, radius, radians, radians);
          cairo_line_to(cr, x, y);
          cairo_stroke(cr);
          snprintf(sf, 32, "%d", i);
          cairo_text_extents(cr, sf, &extents);
          cairo_arc(cr, cx, cx, radius + 5, radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_new_path(cr);
          //
          // At x=0, move left the whole width, at x==cx half of the width, and at x=2 cx do not move
          //
          x += extents.width * (x / (2.0 * cx) - 1.0);
          cairo_move_to(cr, x, y);
          cairo_show_text(cr, sf);
        } else {
          cairo_arc(cr, cx, cx, radius + 2, radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_arc(cr, cx, cx, radius, radians, radians);
          cairo_line_to(cr, x, y);
          cairo_stroke(cr);
        }

        cairo_new_path(cr);
      }

      for (i = 20; i <= 60; i += 20) {
        angle = bydb * ((double)i + 54.0) + min_angle;
        radians = angle * M_PI / 180.0;
        cairo_arc(cr, cx, cx, radius + 4, radians, radians);
        cairo_get_current_point(cr, &x, &y);
        cairo_arc(cr, cx, cx, radius, radians, radians);
        cairo_line_to(cr, x, y);
        cairo_stroke(cr);
        snprintf(sf, 32, "+%d", i);
        cairo_text_extents(cr, sf, &extents);
        cairo_arc(cr, cx, cx, radius + 5, radians, radians);
        cairo_get_current_point(cr, &x, &y);
        cairo_new_path(cr);
        x += extents.width * (x / (2.0 * cx) - 1.0);
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, sf);
        cairo_new_path(cr);
      }

      cairo_set_line_width(cr, PAN_LINE_EXTRA);
      cairo_set_source_rgba(cr, COLOUR_METER);

      if (vfo[active_receiver->id].frequency > 30000000LL) {
        //
        // VHF/UHF (beyond 30 MHz): -147 dBm is S0
        //
        angle = (fmax(-147.0, max_rxlvl) + 147.0) * bydb + min_angle;
      } else {
        //
        // HF (up to 30 MHz): -127 dBm is S0
        //
        angle = (fmax(-127.0, max_rxlvl) + 127.0) * bydb + min_angle;
      }

      radians = angle * M_PI / 180.0;
      cairo_arc(cr, cx, cx, radius + 8, radians, radians);
      cairo_line_to(cr, cx, cx);
      cairo_stroke(cr);
#if defined (__LDESK__)
      cairo_set_source_rgba(cr, COLOUR_ORANGE);
#else
      cairo_set_source_rgba(cr, COLOUR_METER);
#endif
      snprintf(sf, 32, "%d dBm", (int)(max_rxlvl - 0.5)); // assume max_rxlvl < 0 in roundig

      if (METER_WIDTH < 210) {
        cairo_set_font_size(cr, 16);
        cairo_move_to(cr, cx - 32, cx - radius + 30);
      } else {
        cairo_set_font_size(cr, 20);
        cairo_move_to(cr, cx - 40, cx - radius + 34);
      }

      cairo_show_text(cr, sf);
#if defined (__LDESK__)
      cairo_set_source_rgba(cr, COLOUR_ORANGE);
#if defined (__APPLE__)
      cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#endif
      // cairo_set_font_size(cr, 16);
      snprintf(sf, 32, "%s", dbm2smeter[get_SWert((int)(max_rxlvl - 0.5))]);
      cairo_move_to(cr, cx - 90, cx - radius + 64);
      cairo_show_text(cr, sf);

      if (active_receiver->smetermode == 100) {
        snprintf(sf, 32, "S=Peak");
      } else if (active_receiver->smetermode == 101) {
        snprintf(sf, 32, "S=Avg");
      }

      cairo_set_font_size(cr, 16);
      cairo_move_to(cr, cx + 65, cx - radius - 3);
      cairo_show_text(cr, sf);
#endif
    }
    break;

    case POWER: {
      //
      // Analog TX display
      //
      int  units;          // 1: x.y W, 2: xxx W
      double interval;     // 1/10 of full reflection
      int i;
      double x;
      double y;
      double angle;
      double radians;
      double cx = (double)METER_WIDTH / 2.0;
      double radius = cx - 25.0;
      double min_angle, max_angle;

      if (band->disablePA || !pa_enabled) {
        units = 1;
        interval = 0.1;
      } else {
        int pp = pa_power_list[pa_power];
        units = (pp <= 1) ? 1 : 2;
        interval = 0.1 * pp;
      }

      if (cx - 0.342 * radius < METER_HEIGHT - 5) {
        min_angle = 200.0;
        max_angle = 340.0;
      } else if (cx - 0.5 * radius < METER_HEIGHT - 5) {
        min_angle = 210.0;
        max_angle = 330.0;
      } else {
        min_angle = 220.0;
        max_angle = 320.0;
      }

      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);
      cairo_arc(cr, cx, cx, radius, min_angle * M_PI / 180.0, max_angle * M_PI / 180.0);
      cairo_stroke(cr);
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);

      for (i = 0; i <= 100; i++) {
        angle = (double)i * 0.01 * max_angle + (double)(100 - i) * 0.01 * min_angle;
        radians = angle * M_PI / 180.0;

        if ((i % 10) == 0) {
          cairo_arc(cr, cx, cx, radius + 4, radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_arc(cr, cx, cx, radius, radians, radians);
          cairo_line_to(cr, x, y);
          cairo_stroke(cr);

          if ((i % 20) == 0) {
            switch (units) {
            case 1:
              snprintf(sf, 32, "%0.1f", 0.1 * interval * i);
              break;

            case 2: {
              int p = (int) (0.1 * interval * i);

              // "1000" overwrites the right margin, replace by "1K"
              if (p == 1000) {
                snprintf(sf, 32, "1K");
              } else {
                snprintf(sf, 32, "%d", p);
              }
            }
            break;
            }

            cairo_text_extents(cr, sf, &extents);
            cairo_arc(cr, cx, cx, radius + 5, radians, radians);
            cairo_get_current_point(cr, &x, &y);
            cairo_new_path(cr);
            x += extents.width * (x / (2.0 * cx) - 1.0);
            cairo_move_to(cr, x, y);
            cairo_show_text(cr, sf);
          }
        }

        cairo_new_path(cr);
      }

      cairo_set_line_width(cr, PAN_LINE_EXTRA);
      cairo_set_source_rgba(cr, COLOUR_METER);
      angle = max_pwr * (max_angle - min_angle) / (10.0 * interval) + min_angle;

      if (angle > max_angle + 5) { angle = max_angle + 5; }

      radians = angle * M_PI / 180.0;
      cairo_arc(cr, cx, cx, radius + 8, radians, radians);
      cairo_line_to(cr, cx, cx);
      cairo_stroke(cr);
      cairo_set_source_rgba(cr, COLOUR_METER);

      switch (pa_power) {
      case PA_1W:
        snprintf(sf, 32, "%dmW", (int)(1000.0 * max_pwr + 0.5));
        break;

      case PA_5W:
      case PA_10W:
        snprintf(sf, 32, "%0.1fW", max_pwr);
        break;

      default:
        snprintf(sf, 32, "%dW", (int)(max_pwr + 0.5));
        break;
      }

      cairo_move_to(cr, cx - 20, cx - radius + 15);
      cairo_show_text(cr, sf);

      if (can_transmit) {
        if (swr > transmitter->swr_alarm) {
          cairo_set_source_rgba(cr, COLOUR_ALARM);  // display SWR in red color
        } else {
          cairo_set_source_rgba(cr, COLOUR_METER); // display SWR in white color
        }
      }

      snprintf(sf, 32, "SWR %1.1f:1", swr);
      cairo_move_to(cr, cx - 40, cx - radius + 28);
      cairo_show_text(cr, sf);

      if (!cwmode) {
        cairo_set_source_rgba(cr, COLOUR_METER);
        snprintf(sf, 32, "ALC %2.1f dB", max_alc);
        cairo_move_to(cr, cx - 40, cx - radius + 41);
        cairo_show_text(cr, sf);
      }
    }
    break;
    }

    //
    // Both analog and digital, VOX status
    // The mic level meter is not shown in CW modes,
    // otherwise it is always shown while TX, and shown
    // during RX only if VOX is enabled
    //
    // ANALOG-ANZEIGE
    if (((meter_type == POWER) || vox_enabled) && !cwmode) {
      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        double x_offset = 5.0;
        double y_offset = 10.0;
        cairo_set_source_rgba(cr, COLOUR_METER);
        cairo_move_to(cr, x_offset, y_offset);
        cairo_line_to(cr, x_offset, y_offset + 80.0);
        cairo_move_to(cr, x_offset, y_offset);
        cairo_line_to(cr, x_offset + 20.0, y_offset);
        cairo_move_to(cr, x_offset, y_offset + 20.0);
        cairo_line_to(cr, x_offset + 3.0, y_offset + 20.0);
        cairo_move_to(cr, x_offset, y_offset + 40.0);
        cairo_line_to(cr, x_offset + 3.0, y_offset + 40.0);
        cairo_move_to(cr, x_offset, y_offset + 60.0);
        cairo_line_to(cr, x_offset + 3.0, y_offset + 60.0);
        cairo_move_to(cr, x_offset, y_offset + 80.0);
        cairo_line_to(cr, x_offset + 20.0, y_offset + 80.0);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, COLOUR_ALARM_WEAK);
        cairo_rectangle(cr, x_offset, y_offset, 20.0, 20.0);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, COLOUR_OK_WEAK);
        cairo_rectangle(cr, x_offset, y_offset + 20.0, 20.0, 20.0);
        cairo_fill(cr);
        double peak = GetTXAMeter(transmitter->id, TXA_MIC_AV);

        if (peak < -30.0) { peak = -30.0; }

        if (peak > 5.0) { peak = 5.0; }

        peak = 0.0571 * peak * peak + 3.7143 * peak + 60;

        if (peak < 0.0) { peak = 0.0; }

        if (peak > 80.0) { peak = 80.0; }

        cairo_set_source_rgba(cr, COLOUR_METER);
        cairo_rectangle(cr, x_offset + 4.0, (y_offset + 80) - peak, 4.0, peak);
        cairo_fill(cr);
        double alc_val;

        switch (transmitter->alcmode) {
        case ALC_PEAK:
        default:
          alc_val = GetTXAMeter(transmitter->id, TXA_ALC_PK);
          break;

        case ALC_AVERAGE:
          alc_val = GetTXAMeter(transmitter->id, TXA_ALC_AV);
          break;

        case ALC_GAIN:
          alc_val = GetTXAMeter(transmitter->id, TXA_ALC_GAIN);
          break;
        }

        if (alc_val > 5.0) { alc_val = 5.0; }

        if (alc_val < -30.0) { alc_val = -30.0; }

        alc_val = 0.0571 * alc_val * alc_val + 3.7143 * alc_val + 60;

        if (alc_val < 0.0) { alc_val = 0.0; }

        if (alc_val > 80.0) { alc_val = 80.0; }

        cairo_rectangle(cr, x_offset + 13.0, (y_offset + 80) - alc_val, 4.0, alc_val);
        cairo_fill(cr);
        cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
        cairo_set_source_rgba(cr, COLOUR_METER);
        cairo_move_to(cr, x_offset + 25.0, y_offset + 10.0);
        cairo_show_text(cr, "Mic | ALC");
        double current_line_width = cairo_get_line_width(cr);

        if (vox_enabled) {
          cairo_set_source_rgba(cr, COLOUR_ATTN);
          cairo_set_line_width(cr, current_line_width + 1.5);
          cairo_move_to(cr, x_offset, y_offset + 80.0 * (1 - vox_threshold));
          cairo_line_to(cr, x_offset + 10.0, y_offset + 80.0 * (1 - vox_threshold));
          cairo_stroke(cr);
          cairo_set_line_width(cr, current_line_width);
        }
      } else {
        double offset = ((double)METER_WIDTH - 100.0) / 2.0;
        double peak = vox_get_peak();

        if (peak > 1.0) { peak = 1.0; }

        // peak = peak * 100.0; // old
        peak = 50.0 * log(peak) + 100.0;  // 0-100 maps to -40...0 dB

        if (peak < 0.0) { peak = 0.0; } // add new

        cairo_set_source_rgba(cr, COLOUR_OK);
        cairo_rectangle(cr, offset, 0.0, peak, 5.0);
        cairo_fill(cr);
        cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
        cairo_set_source_rgba(cr, COLOUR_METER);
        cairo_move_to(cr, offset + 105.0, 10.0);
        cairo_show_text(cr, "Mic Lvl");
        cairo_move_to(cr, offset, 0.0);
        cairo_line_to(cr, offset, 5.0);
        cairo_stroke(cr);
        cairo_move_to(cr, offset + 50.0, 0.0);
        cairo_line_to(cr, offset + 50.0, 5.0);
        cairo_stroke(cr);
        cairo_move_to(cr, offset + 100.0, 0.0);
        cairo_line_to(cr, offset + 100.0, 5.0);
        cairo_stroke(cr);
        cairo_move_to(cr, offset, 5.0);
        cairo_line_to(cr, offset + 100.0, 5.0);
        cairo_stroke(cr);

        if (vox_enabled) {
          cairo_set_source_rgba(cr, COLOUR_ALARM);
          cairo_move_to(cr, offset + (vox_threshold * 100.0), 0.0);
          cairo_line_to(cr, offset + (vox_threshold * 100.0), 5.0);
          cairo_stroke(cr);
        }
      }
    }
  } else {
    //
    // Digital meter, both RX and TX:
    // Mic level display
    //
    // DIGITAL-ANZEIGE
    int text_location;
    int Y1 = METER_HEIGHT / 4;
    int Y2 = Y1 + METER_HEIGHT / 3;
    int Y4 = 4 * Y1 - 6;
    int size;
    cairo_text_extents_t extents;
    cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
    cairo_paint (cr);
    cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_line_width(cr, PAN_LINE_THICK);

    if (((meter_type == POWER) || vox_enabled) && !cwmode) {
      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) { // new designed MicLvl and ALC meter
        cairo_set_source_rgba(cr, COLOUR_METER);
        cairo_move_to(cr, 5.0, Y1);
        cairo_line_to(cr, 105.0, Y1);
        cairo_move_to(cr, 5.0, Y1);
        cairo_line_to(cr, 5.0, Y1 - 15);
        cairo_move_to(cr, 5.0 + 20.0, Y1);
        cairo_line_to(cr, 5.0 + 20.0, Y1 - 3);
        cairo_move_to(cr, 5.0 + 40.0, Y1);
        cairo_line_to(cr, 5.0 + 40.0, Y1 - 3);
        cairo_move_to(cr, 5.0 + 60.0, Y1);
        cairo_line_to(cr, 5.0 + 60.0, Y1 - 3);
        cairo_stroke(cr);
        // cairo_move_to(cr, 5.0 + 80.0, Y1);
        // cairo_line_to(cr, 5.0 + 80.0, Y1 - 10);
        cairo_move_to(cr, 5.0 + 100.0, Y1);
        cairo_line_to(cr, 5.0 + 100.0, Y1 - 15);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, COLOUR_OK_WEAK);
        cairo_rectangle(cr, 5.0 + 64.0, Y1 - 15, 16, 15);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, COLOUR_ALARM_WEAK);
        cairo_rectangle(cr, 5.0 + 80.0, Y1 - 15, 19, 15);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, COLOUR_METER);
        double peak = GetTXAMeter(transmitter->id, TXA_MIC_AV);

        if (peak > 5.0) { peak = 5.0; }

        if (peak < -40.0) { peak = -40.0; }

        // peak = 1.6 * peak + 80; // 0-100 meter: 0 is -50db, 100 is +5db, from 0-5db is red
        peak = 0.0436 * peak * peak + 3.7818 * peak + 80;

        if (peak < 0.0) { peak = 0.00; }

        if (peak > 100.0) { peak = 100.0; }

        // 64 is -10db, 72 is -5db, 80 is 0db and 100 is +5db
        cairo_set_source_rgba(cr, COLOUR_METER);
        cairo_rectangle(cr, 5.0, Y1 - 12, peak, 3);
        cairo_fill(cr);
        double alc_val;

        switch (transmitter->alcmode) {
        case ALC_PEAK:
        default:
          alc_val = GetTXAMeter(transmitter->id, TXA_ALC_PK);
          break;

        case ALC_AVERAGE:
          alc_val = GetTXAMeter(transmitter->id, TXA_ALC_AV);
          break;

        case ALC_GAIN:
          alc_val = GetTXAMeter(transmitter->id, TXA_ALC_GAIN);
          break;
        }

        if (alc_val > 5.0) { alc_val = 5.0; }

        if (alc_val < -40.0) { alc_val = -40.0; }

        // alc_val = 1.6 * alc_val + 80;
        alc_val = 0.0436 * alc_val * alc_val + 3.7818 * alc_val + 80;

        if (alc_val < 0.0) { alc_val = 0.00; }

        if (alc_val > 100.0) { alc_val = 100.0; }

        cairo_set_source_rgba(cr, COLOUR_METER);
        cairo_rectangle(cr, 5.0, Y1 - 6, alc_val, 3);
        cairo_fill(cr);
        double current_line_width = cairo_get_line_width(cr);

        if (vox_enabled) {
          cairo_set_source_rgba(cr, COLOUR_ATTN);
          cairo_set_line_width(cr, current_line_width + 1.5);
          cairo_move_to(cr, 5.0 + (vox_threshold * 100.0), Y1 - 15);
          cairo_line_to(cr, 5.0 + (vox_threshold * 100.0), Y1 - 6);
          cairo_stroke(cr);
          cairo_set_line_width(cr, current_line_width);
        }
      } else { // original MicLvl meter
        cairo_set_source_rgba(cr, COLOUR_METER);
        cairo_move_to(cr, 5.0, Y1);
        cairo_line_to(cr, 5.0, Y1 - 10);
        cairo_move_to(cr, 5.0 + 25.0, Y1);
        cairo_line_to(cr, 5.0 + 25.0, Y1 - 5);
        cairo_move_to(cr, 5.0 + 50.0, Y1);
        cairo_line_to(cr, 5.0 + 50.0, Y1 - 10);
        cairo_move_to(cr, 5.0 + 75.0, Y1);
        cairo_line_to(cr, 5.0 + 75.0, Y1 - 5);
        cairo_move_to(cr, 5.0 + 100.0, Y1);
        cairo_line_to(cr, 5.0 + 100.0, Y1 - 10);
        cairo_stroke(cr);
        double peak = vox_get_peak();

        if (peak > 1.0) { peak = 1.0; }

        peak = 50.0 * log(peak) + 100.0;  // 0-100 maps to -40...0 dB

        if (peak < 0.0) { peak = 0.0; } // add

        cairo_set_source_rgba(cr, COLOUR_OK);
        cairo_rectangle(cr, 5.0, Y1 - 10, peak, 5);
        cairo_fill(cr);

        if (vox_enabled) {
          cairo_set_source_rgba(cr, COLOUR_ALARM);
          cairo_move_to(cr, 5.0 + (vox_threshold * 100.0), Y1 - 10);
          cairo_line_to(cr, 5.0 + (vox_threshold * 100.0), Y1);
          cairo_stroke(cr);
        }
      }

      cairo_set_source_rgba(cr, COLOUR_METER);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);

      // cairo_move_to(cr, 150.0, Y1);
      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        cairo_move_to(cr, 110.0, Y1 - 8);
        cairo_set_source_rgba(cr, COLOUR_METER);
        cairo_show_text(cr, "Mic Lvl");
        cairo_move_to(cr, 110.0, Y1 + 4);
        cairo_show_text(cr, "ALC");
      } else {
        cairo_move_to(cr, 110.0, Y1);
        cairo_set_source_rgba(cr, COLOUR_METER);
        cairo_show_text(cr, "Mic Lvl");
      }
    }

    cairo_set_source_rgba(cr, COLOUR_METER);

    switch (meter_type) {
    case SMETER:
      //
      // Digital meter, RX
      //
      // value is dBm
      text_location = 10;

      if (METER_WIDTH >= 150) {
        int i;
        cairo_set_line_width(cr, PAN_LINE_THICK);
        cairo_set_source_rgba(cr, COLOUR_METER);

        for (i = 0; i < 55; i++) {
          cairo_move_to(cr, 5 + i, Y4 - 10);

          if (i % 18 == 0) {
            cairo_line_to(cr, 5 + i, Y4 - 20);
          } else if (i % 6 == 0) {
            cairo_line_to(cr, 5 + i, Y4 - 15);
          }
        }

        cairo_stroke(cr);
#if defined (__LDESK__)
        cairo_select_font_face(cr, "FreeSans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#endif
        cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
        cairo_move_to(cr, 20, Y4);
        cairo_show_text(cr, "3");
        cairo_move_to(cr, 38, Y4);
        cairo_show_text(cr, "6");
        cairo_move_to(cr, 56, Y4);
        cairo_show_text(cr, "9");
        // cairo_set_source_rgba(cr, COLOUR_ALARM);
        cairo_set_source_rgba(cr, COLOUR_ORANGE);
        // cairo_move_to(cr, 5 + 54, Y4 - 10);
        // cairo_line_to(cr, 5 + 54, Y4 - 20);
        cairo_move_to(cr, 5 + 74, Y4 - 10);
        cairo_line_to(cr, 5 + 74, Y4 - 20);
        cairo_move_to(cr, 5 + 94, Y4 - 10);
        cairo_line_to(cr, 5 + 94, Y4 - 20);
        cairo_move_to(cr, 5 + 114, Y4 - 10);
        cairo_line_to(cr, 5 + 114, Y4 - 20);
        cairo_stroke(cr);
        // cairo_move_to(cr, 56, Y4);
        // cairo_show_text(cr, "9");
        cairo_move_to(cr, 5 + 74 - 12, Y4);
        cairo_show_text(cr, "+20");
        cairo_move_to(cr, 5 + 94 - 9, Y4);
        cairo_show_text(cr, "+40");
        cairo_move_to(cr, 5 + 114 - 6, Y4);
        cairo_show_text(cr, "+60");
        //
        // The scale for l is:
        //   0.0  --> S0
        //  54.0  --> S9
        // 114.0  --> S9+60
        //
        double l = max_rxlvl + 127.0;

        if (vfo[active_receiver->id].frequency > 30000000LL) {
          // S9 is -93 dBm for frequencies above 30 MHz
          l = max_rxlvl + 147.0;
        }

        //
        // Restrict bar to S9+60
        //
        if (l > 114.0) { l = 114.0; }

        // use colours from the "gradient" panadapter display,
        // but use no gradient: S0-S9 first colour, beyond S9 last colour
        cairo_pattern_t *pat = cairo_pattern_create_linear(0.0, 0.0, 114.0, 0.0);
        // Definiere Farben für den Verlauf
        cairo_pattern_add_color_stop_rgba(pat, 0.00, COLOUR_GRAD1);       // green
        // cairo_pattern_add_color_stop_rgba(pat, 0.50, COLOUR_GRAD1);
        // cairo_pattern_add_color_stop_rgba(pat, 0.50, COLOUR_GRAD4);
        cairo_pattern_add_color_stop_rgba(pat, 0.20, 1.0, 1.0, 0.0, 1.0); // yellow
        cairo_pattern_add_color_stop_rgba(pat, 0.40, 1.0, 0.5, 0.0, 1.0); // orange
        cairo_pattern_add_color_stop_rgba(pat, 0.75, COLOUR_GRAD4);       // red
        cairo_pattern_add_color_stop_rgba(pat, 1.00, COLOUR_GRAD4);       // red
        cairo_set_source(cr, pat);
        // cairo_rectangle(cr, 5, Y2 - 20, l, 20.0);
        cairo_rectangle(cr, 5, Y2 - 20, l, 30.0);               // add by DL1BZ
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
        //
        // Mark right edge of S-meter bar with a line in ATTN colour
        //
        cairo_set_source_rgba(cr, COLOUR_ATTN);
        // cairo_move_to(cr, 5 + l, (double)Y2);
        /*
        cairo_move_to(cr, 5 + l, (double)Y2 + 15);              // add by DL1BZ
        cairo_line_to(cr, 5 + l, (double)Y2 - 20);
        cairo_stroke(cr);
        */
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);                // add by DL1BZ
        cairo_rectangle(cr, 5 + l, (double)Y2 - 20, 2.0, 35.0); // add by DL1BZ
        cairo_fill(cr);                                         // add by DL1BZ
        text_location = 124;
      }

      //
      // Compute largest size for the digital S-meter reading such that
      // it fits, both horizontally and vertically
      //
      size = (METER_WIDTH - text_location) / 5;

      if (size > METER_HEIGHT / 3) { size = METER_HEIGHT / 3; }

#if defined (__LDESK__)
      cairo_set_source_rgba(cr, COLOUR_ORANGE);
#if defined (__APPLE__)
      cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#endif
      cairo_set_font_size(cr, size - 4);
      snprintf(sf, 32, "%s", dbm2smeter[get_SWert((int)(max_rxlvl - 0.5))]); // assume max_rxlvl < 0 in roundig
      cairo_text_extents(cr, sf, &extents);
      cairo_move_to(cr, METER_WIDTH - extents.width - 15, Y2 - 8);
      cairo_show_text(cr, sf);
      snprintf(sf, 32, "%-3d dBm", (int)(max_rxlvl - 0.5));  // assume max_rxlvl < 0 in rounding
      cairo_text_extents(cr, sf, &extents);
      cairo_move_to(cr, METER_WIDTH - extents.width - 15, Y2 + 15);
      cairo_show_text(cr, sf);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);

      if (active_receiver->smetermode == 100) {
        snprintf(sf, 32, "S=Peak");
        cairo_text_extents(cr, sf, &extents);
        cairo_move_to(cr, METER_WIDTH - extents.width - 15, Y2 - 32);
        cairo_show_text(cr, sf);
      } else if (active_receiver->smetermode == 101) {
        snprintf(sf, 32, "S=Avg");
        cairo_text_extents(cr, sf, &extents);
        cairo_move_to(cr, METER_WIDTH - extents.width - 15, Y2 - 32);
        cairo_show_text(cr, sf);
      }

#else
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_set_font_size(cr, size);
      snprintf(sf, 32, "%-3d dBm", (int)(max_rxlvl - 0.5));  // assume max_rxlvl < 0 in rounding
      cairo_text_extents(cr, sf, &extents);
      cairo_move_to(cr, METER_WIDTH - extents.width - 5, Y2);
      cairo_show_text(cr, sf);
#endif
      break;

    case POWER:
      cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);

      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        //
        // Power/Alc/SWR not available for SOAPY.
        //
        switch (pa_power) {
        case PA_1W:
          snprintf(sf, 32, "FWD %dmW", (int)(1000.0 * max_pwr + 0.5));
          break;

        case PA_5W:
        case PA_10W:
          snprintf(sf, 32, "FWD %0.1fW", max_pwr);
          break;

        default:
          snprintf(sf, 32, "FWD %dW", (int)(max_pwr + 0.5));
          break;
        }

        cairo_set_source_rgba(cr, COLOUR_ATTN);
#if defined (__LDESK__)
        cairo_move_to(cr, 5, Y2 - 12);
#else
        cairo_move_to(cr, 5, Y2);
#endif
        cairo_show_text(cr, sf);

        if (can_transmit) {
          if (swr > transmitter->swr_alarm) {
            cairo_set_source_rgba(cr, COLOUR_ALARM);  // display SWR in red color
          } else {
            cairo_set_source_rgba(cr, COLOUR_OK); // display SWR in white color
          }
        }

        snprintf(sf, 32, "SWR %1.1f:1", swr);
#if defined (__LDESK__)
        cairo_move_to(cr, METER_WIDTH / 2, Y2 - 12);
#else
        cairo_move_to(cr, METER_WIDTH / 2, Y2);
#endif
        cairo_show_text(cr, sf);
      }

      if (!cwmode) {
        cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
        cairo_set_source_rgba(cr, COLOUR_METER);  // revert to white color
        snprintf(sf, 32, "ALC %2.1f dB", max_alc);
        cairo_move_to(cr, METER_WIDTH / 2, Y4);
        cairo_show_text(cr, sf);
      }

#if defined (__LDESK__)

      if (!duplex && !cwmode) {
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

          snprintf(sf, 32, "Mic %.0f", _mic_av);
          cairo_move_to(cr, 5, Y4 - 30);
          cairo_show_text(cr, sf);
          cairo_set_source_rgba(cr, COLOUR_METER);

          if (vfo_get_tx_mode() == modeDIGL || vfo_get_tx_mode() == modeDIGU) {
            cairo_set_source_rgba(cr, COLOUR_SHADE);
          }

          snprintf(sf, 32, "EQ  %.0f", _eq_av);
          cairo_move_to(cr, 5, Y4 - 15);
          cairo_show_text(cr, sf);
          snprintf(sf, 32, "Lev %.0f", _lvlr_av);
          cairo_move_to(cr, 65, Y4 - 30);
          cairo_show_text(cr, sf);
          snprintf(sf, 32, "CFC %.0f", _cfc_av);
          cairo_move_to(cr, 65, Y4 - 15);
          cairo_show_text(cr, sf);
          snprintf(sf, 32, "PROC %.0f", _proc_av);
          cairo_move_to(cr, METER_WIDTH / 2, Y4 - 30);
          cairo_show_text(cr, sf);
          cairo_set_source_rgba(cr, COLOUR_METER);

          if (_out_av > 0.0) {
            cairo_set_source_rgba(cr, COLOUR_ALARM);
          } else {
            cairo_set_source_rgba(cr, COLOUR_METER);
          }

          snprintf(sf, 32, "Out %.0f", _out_av);
          cairo_move_to(cr, METER_WIDTH / 2, Y4 - 15);
          cairo_show_text(cr, sf);
          cairo_set_source_rgba(cr, COLOUR_METER);
        }
      }

#endif
      break;
    }
  }

  //
  // This is the same for analog and digital metering
  //
  cairo_destroy(cr);
  gtk_widget_queue_draw (meter);
}
