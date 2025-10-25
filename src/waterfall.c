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
#include <semaphore.h>
#include <string.h>
#include "radio.h"
#include "vfo.h"
#include "band.h"
#include "appearance.h"
#include "audio.h"
#include "toolset.h"
#include "waterfall.h"
#include "rx_panadapter.h"
#include "message.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif

static int colorLowR = 0; // black
static int colorLowG = 0;
static int colorLowB = 0;

static int colorHighR = 255; // yellow
static int colorHighG = 255;
static int colorHighB = 0;

static double hz_per_pixel;

static int my_width;
static int my_height;

/* Create a new surface of the appropriate size to store our scribbles */
static gboolean
waterfall_configure_event_cb (GtkWidget         *widget,
                              GdkEventConfigure *event,
                              gpointer           data) {
  RECEIVER *rx = (RECEIVER *)data;
  my_width = gtk_widget_get_allocated_width (widget);
  my_height = gtk_widget_get_allocated_height (widget);
  rx->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, my_width, my_height);
  unsigned char *pixels = gdk_pixbuf_get_pixels (rx->pixbuf);
  memset(pixels, 0, my_width * my_height * 3);
  return TRUE;
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static gboolean
waterfall_draw_cb (GtkWidget *widget,
                   cairo_t   *cr,
                   gpointer   data) {
  const RECEIVER *rx = (RECEIVER *)data;
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  GtkAllocation allocation;
  gtk_widget_get_allocation(rx->waterfall, &allocation);
  int b_width = allocation.width;
  int b_height = allocation.height;
  int box_height = 30;
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  gdk_cairo_set_source_pixbuf (cr, rx->pixbuf, 0, 0);
  cairo_paint (cr); // vor dem Zeichnen der Box aufrufen, sinst wird der pixbuf überschrieben !

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (display_info_bar && active_receiver->display_waterfall && (active_receiver->display_panadapter == 0
      || active_receiver->display_panadapter == 1) && rx->id == 0 && !rx_stack_horizontal) {
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_rectangle(cr, 0.0, b_height - box_height, b_width, box_height);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, COLOUR_WHITE);
    cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#if defined (__APPLE__)
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
    cairo_move_to(cr, (b_width / 2) + 100, b_height - 10);
#else
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
    cairo_move_to(cr, b_width / 2, b_height - 10);
#endif

    if (can_transmit) {
      cairo_show_text(cr, "[T]une  [b]and  [M]ode  [v]fo  [f]ilter  [n]oise  [a]nf  n[r]  [w]binaural  [e]SNB");
    } else {
      cairo_show_text(cr, "[b]and  [M]ode  [v]fo  [f]ilter  [n]oise  [a]nf  n[r]  [w]binaural  [e]SNB");
    }

    char _text[128];

    if (can_transmit) {
      cairo_set_source_rgba(cr, COLOUR_ORANGE);
      cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#if defined (__APPLE__)
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
#else
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
#endif
#if defined (__APPLE__)
      snprintf(_text, 128, "[%d] %s", active_receiver->id, truncate_text_3p(transmitter->microphone_name, 36));
#else
      int _audioindex = 0;

      if (n_input_devices > 0) {
        for (int i = 0; i < n_input_devices; i++) {
          if (strcmp(transmitter->microphone_name, input_devices[i].name) == 0) {
            _audioindex = i;
          }
        }

        snprintf(_text, 128, "[%d] %s", active_receiver->id, truncate_text_3p(input_devices[_audioindex].description, 28));
      } else {
        snprintf(_text, 128, "NO AUDIO INPUT DETECTED");
      }

#endif
      cairo_move_to(cr, 10.0, b_height - 10);
      cairo_show_text(cr, _text);
    }

    if (display_solardata) {
      check_and_run(1);  // 0=no_log_output, 1=print_to_log
      // g_idle_add(check_and_run_idle_cb, GINT_TO_POINTER(1));
#if defined (__APPLE__)
      cairo_move_to(cr, (b_width / 4) + 20, b_height - 10);
#else
      cairo_move_to(cr, (b_width / 4) - 50, b_height - 10);
#endif

      if (sunspots != -1) {
        snprintf(_text, 128, "SN:%d SFI:%d A:%d K:%d X:%s GmF:%s", sunspots, solar_flux, a_index, k_index, xray, geomagfield);
      } else {
        snprintf(_text, 128, " ");
      }

      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, _text);
    }
  }

  if (active_receiver->display_waterfall && (active_receiver->display_panadapter == 0
      || active_receiver->display_panadapter == 1) && rx->id == 0 && active_receiver->panadapter_autoscale_enabled
      && !rx_stack_horizontal) {
    char _text[128];
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#if defined (__APPLE__)
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
#else
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
#endif
    snprintf(_text, 128, "%d db", g_noise_level);
    cairo_text_extents_t nf_extents;
    cairo_text_extents(cr, _text, &nf_extents);
    double _x =  60 - nf_extents.width;
    cairo_move_to(cr, _x, 15);
    cairo_show_text(cr, _text);
  }

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  return FALSE;
}

static gboolean
waterfall_button_press_event_cb (GtkWidget      *widget,
                                 GdkEventButton *event,
                                 gpointer        data) {
  return rx_button_press_event(widget, event, data);
}

static gboolean
waterfall_button_release_event_cb (GtkWidget      *widget,
                                   GdkEventButton *event,
                                   gpointer        data) {
  return rx_button_release_event(widget, event, data);
}

static gboolean waterfall_motion_notify_event_cb (GtkWidget      *widget,
    GdkEventMotion *event,
    gpointer        data) {
  return rx_motion_notify_event(widget, event, data);
}

// cppcheck-suppress constParameterCallback
static gboolean waterfall_scroll_event_cb (GtkWidget *widget, GdkEventScroll *event, gpointer data) {
  return rx_scroll_event(widget, event, data);
}

void waterfall_update(RECEIVER *rx) {
  if (rx->pixbuf) {
    const float *samples;
    long long vfofreq = vfo[rx->id].frequency; // access only once to be thread-safe
    int  freq_changed = 0;                    // flag whether we have just "rotated"
    int pan = rx->pan;
    int zoom = rx->zoom;
    unsigned char *pixels = gdk_pixbuf_get_pixels (rx->pixbuf);
    int width = gdk_pixbuf_get_width(rx->pixbuf);
    int height = gdk_pixbuf_get_height(rx->pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(rx->pixbuf);
    hz_per_pixel = (double)rx->sample_rate / ((double)my_width * rx->zoom);

    //
    // The existing waterfall corresponds to a VFO frequency rx->waterfall_frequency, a zoom value rx->waterfall_zoom and
    // a pan value rx->waterfall_pan. If the zoom value changes, or if the waterfill needs horizontal shifting larger
    // than the width of the waterfall (band change or big frequency jump), re-init the waterfall.
    // Otherwise, shift the waterfall by an appropriate number of pixels.
    //
    // Note that VFO frequency changes can occur in very many very small steps, such that in each step, the horizontal
    // shifting is only a fraction of one pixel. In this case, there will be every now and then a horizontal shift that
    // corrects for a number of VFO update steps.
    //
    if (rx->waterfall_frequency != 0 && (rx->sample_rate == rx->waterfall_sample_rate)
        && (rx->zoom == rx->waterfall_zoom)) {
      if (rx->waterfall_frequency != vfofreq || rx->waterfall_pan != pan) {
        //
        // Frequency and/or PAN value changed: possibly shift waterfall
        //
        int rotfreq = (int)((double)(rx->waterfall_frequency - vfofreq) / hz_per_pixel); // shift due to freq. change
        int rotpan  = rx->waterfall_pan - pan;                                        // shift due to pan   change
        int rotate_pixels = rotfreq + rotpan;

        if (rotate_pixels >= my_width || rotate_pixels <= -my_width) {
          //
          // If horizontal shift is too large, re-init waterfall
          //
          memset(pixels, 0, my_width * my_height * 3);
          rx->waterfall_frequency = vfofreq;
          rx->waterfall_pan = pan;
        } else {
          //
          // If rotate_pixels != 0, shift waterfall horizontally and set "freq changed" flag
          // calculated which VFO/pan value combination the shifted waterfall corresponds to
          //
          //
          if (rotate_pixels < 0) {
            // shift left, and clear the right-most part
            memmove(pixels, &pixels[-rotate_pixels * 3], ((my_width * my_height) + rotate_pixels) * 3);

            for (int i = 0; i < my_height; i++) {
              memset(&pixels[((i * my_width) + (width + rotate_pixels)) * 3], 0, -rotate_pixels * 3);
            }
          } else if (rotate_pixels > 0) {
            // shift right, and clear left-most part
            memmove(&pixels[rotate_pixels * 3], pixels, ((my_width * my_height) - rotate_pixels) * 3);

            for (int i = 0; i < my_height; i++) {
              memset(&pixels[(i * my_width) * 3], 0, rotate_pixels * 3);
            }
          }

          if (rotfreq != 0) {
            freq_changed = 1;
            rx->waterfall_frequency -= lround(rotfreq * hz_per_pixel); // this is not necessarily vfofreq!
          }

          rx->waterfall_pan = pan;
        }
      }
    } else {
      //
      // waterfall frequency not (yet) set, sample rate changed, or zoom value changed:
      // (re-) init waterfall
      //
      memset(pixels, 0, my_width * my_height * 3);
      rx->waterfall_frequency = vfofreq;
      rx->waterfall_pan = pan;
      rx->waterfall_zoom = zoom;
      rx->waterfall_sample_rate = rx->sample_rate;
    }

    //
    // If we have just shifted the waterfall befause the VFO frequency has changed,
    // there are  still IQ samples in the input queue corresponding to the "old"
    // VFO frequency, and this produces artifacts both on the panadaper and on the
    // waterfall. However, for the panadapter these are overwritten in due course,
    // while artifacts "stay" on the waterfall. We therefore refrain from updating
    // the waterfall *now* and continue updating when the VFO frequency has
    // stabilized. This will not remove the artifacts in any case but is a big
    // improvement.
    //
    if (!freq_changed) {
      memmove(&pixels[rowstride], pixels, (height - 1)*rowstride);
      float soffset;
      unsigned char *p;
      p = pixels;
      samples = rx->pixel_samples;
      float wf_low, wf_high, rangei;
      int id = rx->id;
      int b = vfo[id].band;
      const BAND *band = band_get_band(b);
      int calib = rx_gain_calibration - band->gain;
      //
      // soffset contains all corrections due to attenuation, preamps, etc.
      //
#ifdef SOAPYSDR

      if (device == SOAPYSDR_USB_DEVICE && strcmp(radio->name, "sdrplay") == 0) {
        int v_Gain = (int)soapy_protocol_get_gain_element(active_receiver, "CURRENT");
        adc[rx->adc].gain = 0;
        adc[rx->adc].attenuation = 0;
        adc[rx->adc].gain = v_Gain;
        // t_print("%s: adc[rx->adc].gain = %f adc[rx->adc].attenuation = %f calib = %f\n", __FUNCTION__, adc[rx->adc].gain,adc[rx->adc].attenuation, calib);
      }

#endif
      soffset = (float)(calib + adc[rx->adc].attenuation - adc[rx->adc].gain);

      if (filter_board == ALEX && rx->adc == 0) {
        soffset += (float)(10 * rx->alex_attenuation - 20 * rx->preamp);
      }

      if (filter_board == CHARLY25 && rx->adc == 0) {
        soffset += (float)(12 * rx->alex_attenuation - 18 * rx->preamp - 18 * rx->dither);
      }

      if (rx->waterfall_automatic) {
        float average = 0.0F;

        for (int i = 0; i < width; i++) {
          average += samples[i];
        }

        wf_low = (average / (float)width) + soffset - 5.0F;
        wf_high = wf_low + 55.0F;
      } else {
        wf_low  = (float) rx->waterfall_low;
        wf_high = (float) rx->waterfall_high;
      }

      rangei = 1.0F / (wf_high - wf_low);

      for (int i = 0; i < width; i++) {
        float sample = samples[i + pan] + soffset;

        if (sample < wf_low) {
          *p++ = colorLowR;
          *p++ = colorLowG;
          *p++ = colorLowB;
        } else if (sample > wf_high) {
          *p++ = colorHighR;
          *p++ = colorHighG;
          *p++ = colorHighB;
        } else {
          float percent = (sample - wf_low) * rangei;

          if (percent < 0.222222f) {
            float local_percent = percent * 4.5f;
            *p++ = (int)((1.0f - local_percent) * colorLowR);
            *p++ = (int)((1.0f - local_percent) * colorLowG);
            *p++ = (int)(colorLowB + local_percent * (255 - colorLowB));
          } else if (percent < 0.333333f) {
            float local_percent = (percent - 0.222222f) * 9.0f;
            *p++ = 0;
            *p++ = (int)(local_percent * 255);
            *p++ = 255;
          } else if (percent < 0.444444f) {
            float local_percent = (percent - 0.333333) * 9.0f;
            *p++ = 0;
            *p++ = 255;
            *p++ = (int)((1.0f - local_percent) * 255);
          } else if (percent < 0.555555f) {
            float local_percent = (percent - 0.444444f) * 9.0f;
            *p++ = (int)(local_percent * 255);
            *p++ = 255;
            *p++ = 0;
          } else if (percent < 0.777777f) {
            float local_percent = (percent - 0.555555f) * 4.5f;
            *p++ = 255;
            *p++ = (int)((1.0f - local_percent) * 255);
            *p++ = 0;
          } else if (percent < 0.888888f) {
            float local_percent = (percent - 0.777777f) * 9.0f;
            *p++ = 255;
            *p++ = 0;
            *p++ = (int)(local_percent * 255);
          } else {
            float local_percent = (percent - 0.888888f) * 9.0f;
            *p++ = (int)((0.75f + 0.25f * (1.0f - local_percent)) * 255.0f);
            *p++ = (int)(local_percent * 255.0f * 0.5f);
            *p++ = 255;
          }
        }
      }
    }

    gtk_widget_queue_draw (rx->waterfall);
  }
}

void waterfall_init(RECEIVER *rx, int width, int height) {
  my_width = width;
  my_height = height;
  rx->pixbuf = NULL;
  rx->waterfall_frequency = 0;
  rx->waterfall_sample_rate = 0;
  rx->waterfall = gtk_drawing_area_new ();
  gtk_widget_set_size_request (rx->waterfall, width, height);
  /* Signals used to handle the backing surface */
  g_signal_connect (rx->waterfall, "draw",
                    G_CALLBACK (waterfall_draw_cb), rx);
  g_signal_connect (rx->waterfall, "configure-event",
                    G_CALLBACK (waterfall_configure_event_cb), rx);
  /* Event signals */
  g_signal_connect (rx->waterfall, "motion-notify-event",
                    G_CALLBACK (waterfall_motion_notify_event_cb), rx);
  g_signal_connect (rx->waterfall, "button-press-event",
                    G_CALLBACK (waterfall_button_press_event_cb), rx);
  g_signal_connect (rx->waterfall, "button-release-event",
                    G_CALLBACK (waterfall_button_release_event_cb), rx);
  g_signal_connect(rx->waterfall, "scroll_event",
                   G_CALLBACK(waterfall_scroll_event_cb), rx);
  /* Ask to receive events the drawing area doesn't normally
   * subscribe to. In particular, we need to ask for the
   * button press and motion notify events that want to handle.
   */
  gtk_widget_set_events (rx->waterfall, gtk_widget_get_events (rx->waterfall)
                         | GDK_BUTTON_PRESS_MASK
                         | GDK_BUTTON_RELEASE_MASK
                         | GDK_BUTTON1_MOTION_MASK
                         | GDK_SCROLL_MASK
                         | GDK_POINTER_MOTION_MASK
                         | GDK_POINTER_MOTION_HINT_MASK);
}
