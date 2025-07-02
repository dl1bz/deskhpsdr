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
#if defined (__LDESK__)
  #include <wdsp.h>
#endif

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
#if defined (__LDESK__)
    static double _mic_av;
    static double _eq_av;
    static double _lvlr_av;
    static double _cfc_av;
    static double _proc_av;
    static double _out_av;
#endif

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
#if defined (__LDESK__)
      int _xpos = 0;
      int _ypos = 80;

      if (vfo_get_tx_mode() == modeLSB || vfo_get_tx_mode() == modeDIGL) {
        _xpos = 130;
      } else {
        _xpos = 10;
      }

      cairo_set_source_rgba(cr, COLOUR_ORANGE);
      cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#else
      cairo_set_source_rgba(cr, COLOUR_ALARM);
#endif
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

#if defined (__LDESK__)

      if (duplex && !cwmode) {
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

#endif
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
