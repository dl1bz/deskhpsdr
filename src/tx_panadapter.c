/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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
  if (tx->panadapter_surface) {
    int mywidth = gtk_widget_get_allocated_width (tx->panadapter);
    int myheight = gtk_widget_get_allocated_height (tx->panadapter);
    int txvfo = vfo_get_tx_vfo();
    int txmode = vfo_get_tx_mode();
    double filter_left, filter_right;
    float *samples = tx->pixel_samples;
    double hz_per_pixel = (double)tx->iq_output_rate / (double)tx->pixels;
    cairo_t *cr;
    cr = cairo_create (tx->panadapter_surface);
    cairo_set_source_rgba(cr, COLOUR_PAN_BACKGND);
    cairo_paint (cr);
    // filter
    filter_left = filter_right = 0.5 * mywidth;

    if (txmode != modeCWU && txmode != modeCWL) {
      cairo_set_source_rgba(cr, COLOUR_PAN_FILTER);
      filter_left = (double)mywidth / 2.0 + ((double)tx->filter_low / hz_per_pixel);
      filter_right = (double)mywidth / 2.0 + ((double)tx->filter_high / hz_per_pixel);
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
    double s1;
    int offset = (tx->pixels / 2) - (mywidth / 2);
    samples[offset] = -200.0;
    samples[offset + mywidth - 1] = -200.0;
    s1 = (double)samples[offset];
    s1 = floor((tx->panadapter_high - s1)
               * (double) myheight
               / (tx->panadapter_high - tx->panadapter_low));
    cairo_move_to(cr, 0.0, s1);

    for (int i = 1; i < mywidth; i++) {
      double s2;
      s2 = (double)samples[i + offset];
      s2 = floor((tx->panadapter_high - s2)
                 * (double) myheight
                 / (tx->panadapter_high - tx->panadapter_low));
      cairo_line_to(cr, (double)i, s2);
    }

    if (tx->display_filled) {
      cairo_set_source_rgba(cr, COLOUR_PAN_FILL2);
      cairo_close_path (cr);
      cairo_fill_preserve (cr);
      cairo_set_line_width(cr, PAN_LINE_THIN);
    } else {
      cairo_set_source_rgba(cr, COLOUR_PAN_FILL3);
      cairo_set_line_width(cr, PAN_LINE_THICK);
    }

    cairo_stroke(cr);
    /*
     * Controller1:
     * Draw at the right edge of the TX panadapter the
     * functions associated with the three encoders.
     *
    #ifdef GPIO
      if(controller==CONTROLLER1 && tx->dialog == NULL) {
        char text[64];

        cairo_set_source_rgba(cr,COLOUR_ATTN);
        cairo_set_font_size(cr,DISPLAY_FONT_SIZE3);
        if(ENABLE_E2_ENCODER) {
          cairo_move_to(cr, mywidth-200,70);
          snprintf(text, 64, "%s (%s)",encoder_string[e2_encoder_action],sw_string[e2_sw_action]);
          cairo_show_text(cr, text);
        }

        if(ENABLE_E3_ENCODER) {
          cairo_move_to(cr, mywidth-200,90);
          snprintf(text, 64, "%s (%s)",encoder_string[e3_encoder_action],sw_string[e3_sw_action]);
          cairo_show_text(cr, text);
        }

        if(ENABLE_E4_ENCODER) {
          cairo_move_to(cr, mywidth-200,110);
          snprintf(text, 64, "%s (%s)",encoder_string[e4_encoder_action],sw_string[e4_sw_action]);
          cairo_show_text(cr, text);
        }
      }
    #endif
    */
    //
    // When doing CW, the signal is produced outside WDSP, so
    // it makes no sense to display a PureSignal status. The
    // only exception is if we are running twotone from
    // within the PS menu.
    //
    int cwmode = (txmode == modeCWL || txmode == modeCWU) && !tune && !tx->twotone;

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
      cairo_set_source_rgba(cr, COLOUR_ALARM);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
      int row = 0;

      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        //
        // Power values not available for SoapySDR
        //
        snprintf(text, 64, "FWD %0.1f W", transmitter->fwd);
        row += 15;
        cairo_move_to(cr, 10, row);
        cairo_show_text(cr, text);
        //
        // Since colour is already red, no special
        // action for "high SWR" warning
        //
        snprintf(text, 64, "SWR 1:%1.1f", transmitter->swr);
        row += 15;
        cairo_move_to(cr, 10, row);
        cairo_show_text(cr, text);
      }

      row += 15;
      snprintf(text, 64, "ALC %2.1f dB", transmitter->alc);
      cairo_move_to(cr, 10, row);
      cairo_show_text(cr, text);
    }

    if (tx->dialog == NULL) {
      display_panadapter_messages(cr, mywidth, tx->fps);
    }

    cairo_destroy (cr);
    gtk_widget_queue_draw (tx->panadapter);
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
