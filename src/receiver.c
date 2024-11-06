/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
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
#include <stdio.h>
#include <stdlib.h>

#include <wdsp.h>

#include "agc.h"
#include "audio.h"
#include "band.h"
#include "bandstack.h"
#include "channel.h"
#include "discovered.h"
#include "filter.h"
#include "main.h"
#include "meter.h"
#include "mode.h"
#include "property.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "vfo.h"
#include "meter.h"
#include "rx_panadapter.h"
#include "zoompan.h"
#include "sliders.h"
#include "waterfall.h"
#include "new_protocol.h"
#include "old_protocol.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "ext.h"
#include "new_menu.h"
#ifdef CLIENT_SERVER
  #include "client_server.h"
#endif
#include "message.h"
#include "mystring.h"

#define min(x,y) (x<y?x:y)
#define max(x,y) (x<y?y:x)

static int last_x;
static gboolean has_moved = FALSE;
static gboolean pressed = FALSE;
static gboolean making_active = FALSE;

//
// PART 1. Functions releated to the receiver display
//

void rx_weak_notify(gpointer data, GObject  *obj) {
  RECEIVER *rx = (RECEIVER *)data;
  t_print("%s: id=%d obj=%p\n", __FUNCTION__, rx->id, obj);
}

// cppcheck-suppress constParameterPointer
gboolean rx_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  const RECEIVER *rx = (RECEIVER *)data;

  if (rx == active_receiver) {
    if (event->button == GDK_BUTTON_PRIMARY) {
      last_x = (int)event->x;
      has_moved = FALSE;
      pressed = TRUE;
    } else if (event->button == GDK_BUTTON_SECONDARY) {
      g_idle_add(ext_start_rx, NULL);
    }
  } else {
    making_active = TRUE;
  }

  return TRUE;
}

void rx_set_active(RECEIVER *rx) {
  //
  // Abort any frequency entering in the current receiver
  //
  vfo_num_pad(-1, active_receiver->id);
  //
  // Make rx the new active receiver
  //
  active_receiver = rx;
  g_idle_add(menu_active_receiver_changed, NULL);
  g_idle_add(ext_vfo_update, NULL);
  g_idle_add(zoompan_active_receiver_changed, NULL);
  g_idle_add(sliders_active_receiver_changed, NULL);
  //
  // Changing the active receiver flips the TX vfo
  //
  radio_tx_vfo_changed();
  radio_set_alex_antennas();
}

// cppcheck-suppress constParameterPointer
gboolean rx_button_release_event(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  RECEIVER *rx = (RECEIVER *)data;

  if (making_active) {
    making_active = FALSE;

    if (radio_is_remote) {
#ifdef CLIENT_SERVER
      send_rx_select(client_socket, rx->id);
#endif
    } else {
      rx_set_active(rx);

      if (event->button == GDK_BUTTON_SECONDARY) {
        g_idle_add(ext_start_rx, NULL);
      }
    }
  } else {
    if (pressed) {
      int x = (int)event->x;

      if (event->button == GDK_BUTTON_PRIMARY) {
        if (has_moved) {
          // drag
          vfo_move((long long)((float)(x - last_x)*rx->hz_per_pixel), TRUE);
        } else {
          // move to this frequency
          vfo_move_to((long long)((float)x * rx->hz_per_pixel));
        }

        last_x = x;
        pressed = FALSE;
      }
    }
  }

  return TRUE;
}

gboolean rx_motion_notify_event(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
  int x, y;
  GdkModifierType state;
  const RECEIVER *rx = (RECEIVER *)data;
  //
  // This solves a problem observed since with GTK about mid-2023:
  // when re-focusing a (sub-)menu window after it has lost focus,
  // it may happen that the button press event for "re-focusing"
  // the menu window also ends up in the receiver panel, and that
  // the subsequent button release event is not forwarded to the
  // receiver panal.
  // Subsequent mouse moves (without a button pressed) then led to
  // wild VFO frequency changes. The first temporary solution was to
  // ignore all button press events if a (sub-)menu window is open, but
  // this reduces piHPSDR functionality. Now we solve this problem by
  // looking HERE if the mouse button is pressed, and if not, ignore the
  // move.
  //
  int button_down = (event->state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK)) != 0;

  //
  // if !pressed, we may come from the destruction
  // of a menu, and should not move the VFO.
  //
  if (!making_active && pressed && button_down) {
    gdk_window_get_device_position (event->window,
                                    event->device,
                                    &x,
                                    &y,
                                    &state);
    //
    // Sometimes it turned out to be difficult to "jump" to a
    // new frequency by just clicking in the panadaper. Futher analysis
    // showed that there were "moves" with zero offset arriving between
    // pressing and releasing the mouse button.
    // Accepting such a "move" between a  "press" and the next "release" event
    // sets "has_moved" and results in a "VFO drag" instead of a "VFO set".
    //
    // So we do the following:
    // - "moves" with zero offset are always ignored
    // - the first "move" to be accepted after a "press" must lead us
    //   at least 2 pixels away from the original position.
    //
    int moved = x - last_x;

    if (moved) {
      if (has_moved || moved < -1 || moved > 1) {
        vfo_move((long long)((float)moved * rx->hz_per_pixel), FALSE);
        last_x = x;
        has_moved = TRUE;
      }
    }
  }

  return TRUE;
}

// cppcheck-suppress constParameterPointer
gboolean rx_scroll_event(GtkWidget *widget, const GdkEventScroll *event, gpointer data) {
  #if defined (__LDESK__)
  if (event->state) {
  if (event->direction == GDK_SCROLL_UP) {
    vfo_step(1);
  } else {
    vfo_step(-1);
  }
  }
  #else
    if (event->direction == GDK_SCROLL_UP) {
      vfo_step(1);
    } else {
      vfo_step(-1);
    }
  #endif
  return TRUE;
}

void rx_save_state(const RECEIVER *rx) {
  SetPropI1("receiver.%d.alex_antenna", rx->id,                 rx->alex_antenna);
  SetPropI1("receiver.%d.adc", rx->id,                          rx->adc);

  //
  // For a PS_RX_FEEDBACK, we only store/restore the alex antenna and ADC
  //
  if (rx->id == PS_RX_FEEDBACK) { return; }

  SetPropI1("receiver.%d.audio_channel", rx->id,                rx->audio_channel);
  SetPropI1("receiver.%d.local_audio", rx->id,                  rx->local_audio);
  SetPropS1("receiver.%d.audio_name", rx->id,                   rx->audio_name);
  SetPropI1("receiver.%d.audio_device", rx->id,                 rx->audio_device);
  SetPropI1("receiver.%d.mute_when_not_active", rx->id,         rx->mute_when_not_active);
  SetPropI1("receiver.%d.mute_radio", rx->id,                   rx->mute_radio);

  //
  // no further settings are
  // needed if this is a remote receiver
  //
  if (radio_is_remote) {
    return;
  }

  SetPropI1("receiver.%d.smetermode", rx->id,                   rx->smetermode);
  SetPropI1("receiver.%d.low_latency", rx->id,                  rx->low_latency);
  SetPropI1("receiver.%d.fft_size", rx->id,                     rx->fft_size);
  SetPropI1("receiver.%d.sample_rate", rx->id,                  rx->sample_rate);
  SetPropI1("receiver.%d.filter_low", rx->id,                   rx->filter_low);
  SetPropI1("receiver.%d.filter_high", rx->id,                  rx->filter_high);
  SetPropI1("receiver.%d.fps", rx->id,                          rx->fps);
  SetPropI1("receiver.%d.panadapter_low", rx->id,               rx->panadapter_low);
  SetPropI1("receiver.%d.panadapter_high", rx->id,              rx->panadapter_high);
  SetPropI1("receiver.%d.panadapter_step", rx->id,              rx->panadapter_step);
  SetPropI1("receiver.%d.display_waterfall", rx->id,            rx->display_waterfall);
  SetPropI1("receiver.%d.display_panadapter", rx->id,           rx->display_panadapter);
  SetPropI1("receiver.%d.display_filled", rx->id,               rx->display_filled);
  SetPropI1("receiver.%d.display_gradient", rx->id,             rx->display_gradient);
  SetPropI1("receiver.%d.display_detector_mode", rx->id,        rx->display_detector_mode);
  SetPropI1("receiver.%d.display_average_mode", rx->id,         rx->display_average_mode);
  SetPropF1("receiver.%d.display_average_time", rx->id,         rx->display_average_time);
  SetPropI1("receiver.%d.waterfall_low", rx->id,                rx->waterfall_low);
  SetPropI1("receiver.%d.waterfall_high", rx->id,               rx->waterfall_high);
  SetPropI1("receiver.%d.waterfall_automatic", rx->id,          rx->waterfall_automatic);

  if (have_alex_att) {
    SetPropI1("receiver.%d.alex_attenuation", rx->id,           rx->alex_attenuation);
  }

  SetPropF1("receiver.%d.volume", rx->id,                       rx->volume);
  SetPropI1("receiver.%d.agc", rx->id,                          rx->agc);
  SetPropF1("receiver.%d.agc_gain", rx->id,                     rx->agc_gain);
  SetPropF1("receiver.%d.agc_slope", rx->id,                    rx->agc_slope);
  SetPropF1("receiver.%d.agc_hang_threshold", rx->id,           rx->agc_hang_threshold);
  SetPropI1("receiver.%d.dither", rx->id,                       rx->dither);
  SetPropI1("receiver.%d.random", rx->id,                       rx->random);
  SetPropI1("receiver.%d.preamp", rx->id,                       rx->preamp);
  SetPropI1("receiver.%d.nb", rx->id,                           rx->nb);
  SetPropI1("receiver.%d.nr", rx->id,                           rx->nr);
  SetPropI1("receiver.%d.anf", rx->id,                          rx->anf);
  SetPropI1("receiver.%d.snb", rx->id,                          rx->snb);
  SetPropI1("receiver.%d.nr_agc", rx->id,                       rx->nr_agc);
  SetPropI1("receiver.%d.nr2_gain_method", rx->id,              rx->nr2_gain_method);
  SetPropI1("receiver.%d.nr2_npe_method", rx->id,               rx->nr2_npe_method);
  SetPropI1("receiver.%d.nr2_ae", rx->id,                       rx->nr2_ae);
  SetPropF1("receiver.%d.nr2_trained_threshold", rx->id,        rx->nr2_trained_threshold);
  SetPropF1("receiver.%d.nr2_trained_t2", rx->id,               rx->nr2_trained_t2);
  SetPropI1("receiver.%d.nb2_mode", rx->id,                     rx->nb2_mode);
  SetPropF1("receiver.%d.nb_tau", rx->id,                       rx->nb_tau);
  SetPropF1("receiver.%d.nb_advtime", rx->id,                   rx->nb_advtime);
  SetPropF1("receiver.%d.nb_hang", rx->id,                      rx->nb_hang);
  SetPropF1("receiver.%d.nb_thresh", rx->id,                    rx->nb_thresh);
#ifdef EXTNR
  SetPropF1("receiver.%d.nr4_reduction_amount", rx->id,         rx->nr4_reduction_amount);
  SetPropF1("receiver.%d.nr4_smoothing_factor", rx->id,         rx->nr4_smoothing_factor);
  SetPropF1("receiver.%d.nr4_whitening_factor", rx->id,         rx->nr4_whitening_factor);
  SetPropF1("receiver.%d.nr4_noise_rescale", rx->id,            rx->nr4_noise_rescale);
  SetPropF1("receiver.%d.nr4_post_filter_threshold", rx->id,    rx->nr4_post_filter_threshold);
#endif
  SetPropI1("receiver.%d.deviation", rx->id,                    rx->deviation);
  SetPropI1("receiver.%d.squelch_enable", rx->id,               rx->squelch_enable);
  SetPropF1("receiver.%d.squelch", rx->id,                      rx->squelch);
  SetPropI1("receiver.%d.binaural", rx->id,                     rx->binaural);
  SetPropI1("receiver.%d.zoom", rx->id,                         rx->zoom);
  SetPropI1("receiver.%d.pan", rx->id,                          rx->pan);
  SetPropI1("receiver.%d.eq_enable", rx->id,                    rx->eq_enable);

  for (int i = 0; i < 11; i++) {
    SetPropF2("receiver.%d.eq_freq[%d]", rx->id, i,             rx->eq_freq[i]);
    SetPropF2("receiver.%d.eq_gain[%d]", rx->id, i,             rx->eq_gain[i]);
  }
}

void rx_restore_state(RECEIVER *rx) {
  t_print("%s: id=%d\n", __FUNCTION__, rx->id);
  GetPropI1("receiver.%d.alex_antenna", rx->id,                 rx->alex_antenna);
  GetPropI1("receiver.%d.adc", rx->id,                          rx->adc);

  // Sanity Check
  if (n_adc == 1) { rx->adc = 0; }

  //
  // For a PS_RX_FEEDBACK, we only store/restore the alex antenna and ADC
  //
  if (rx->id == PS_RX_FEEDBACK) { return; }

  GetPropI1("receiver.%d.audio_channel", rx->id,                rx->audio_channel);
  GetPropI1("receiver.%d.local_audio", rx->id,                  rx->local_audio);
  GetPropS1("receiver.%d.audio_name", rx->id,                   rx->audio_name);
  GetPropI1("receiver.%d.audio_device", rx->id,                 rx->audio_device);
  GetPropI1("receiver.%d.mute_when_not_active", rx->id,         rx->mute_when_not_active);
  GetPropI1("receiver.%d.mute_radio", rx->id,                   rx->mute_radio);

  //
  // After restoring local audio settings, no further settings are
  // needed if this is a remote receiver
  //
  if (radio_is_remote) {
    return;
  }

  GetPropI1("receiver.%d.smetermode", rx->id,                   rx->smetermode);
  GetPropI1("receiver.%d.low_latency", rx->id,                  rx->low_latency);
  GetPropI1("receiver.%d.fft_size", rx->id,                     rx->fft_size);
  GetPropI1("receiver.%d.sample_rate", rx->id,                  rx->sample_rate);

  //
  // This may happen if the firmware was down-graded from P2 to P1
  //
  if (protocol == ORIGINAL_PROTOCOL && rx->sample_rate > 384000) {
    rx->sample_rate = 384000;
  }

  GetPropI1("receiver.%d.filter_low", rx->id,                   rx->filter_low);
  GetPropI1("receiver.%d.filter_high", rx->id,                  rx->filter_high);
  GetPropI1("receiver.%d.fps", rx->id,                          rx->fps);
  GetPropI1("receiver.%d.panadapter_low", rx->id,               rx->panadapter_low);
  GetPropI1("receiver.%d.panadapter_high", rx->id,              rx->panadapter_high);
  GetPropI1("receiver.%d.panadapter_step", rx->id,              rx->panadapter_step);
  GetPropI1("receiver.%d.display_waterfall", rx->id,            rx->display_waterfall);
  GetPropI1("receiver.%d.display_panadapter", rx->id,           rx->display_panadapter);
  GetPropI1("receiver.%d.display_filled", rx->id,               rx->display_filled);
  GetPropI1("receiver.%d.display_gradient", rx->id,             rx->display_gradient);
  GetPropI1("receiver.%d.display_detector_mode", rx->id,        rx->display_detector_mode);
  GetPropI1("receiver.%d.display_average_mode", rx->id,         rx->display_average_mode);
  GetPropF1("receiver.%d.display_average_time", rx->id,         rx->display_average_time);
  GetPropI1("receiver.%d.waterfall_low", rx->id,                rx->waterfall_low);
  GetPropI1("receiver.%d.waterfall_high", rx->id,               rx->waterfall_high);
  GetPropI1("receiver.%d.waterfall_automatic", rx->id,          rx->waterfall_automatic);

  if (have_alex_att) {
    GetPropI1("receiver.%d.alex_attenuation", rx->id,           rx->alex_attenuation);
  }

  GetPropF1("receiver.%d.volume", rx->id,                       rx->volume);
  GetPropI1("receiver.%d.agc", rx->id,                          rx->agc);
  GetPropF1("receiver.%d.agc_gain", rx->id,                     rx->agc_gain);
  GetPropF1("receiver.%d.agc_slope", rx->id,                    rx->agc_slope);
  GetPropF1("receiver.%d.agc_hang_threshold", rx->id,           rx->agc_hang_threshold);
  GetPropI1("receiver.%d.dither", rx->id,                       rx->dither);
  GetPropI1("receiver.%d.random", rx->id,                       rx->random);
  GetPropI1("receiver.%d.preamp", rx->id,                       rx->preamp);
  GetPropI1("receiver.%d.nb", rx->id,                           rx->nb);
  GetPropI1("receiver.%d.nr", rx->id,                           rx->nr);
  GetPropI1("receiver.%d.anf", rx->id,                          rx->anf);
  GetPropI1("receiver.%d.snb", rx->id,                          rx->snb);
  GetPropI1("receiver.%d.nr_agc", rx->id,                       rx->nr_agc);
  GetPropI1("receiver.%d.nr2_gain_method", rx->id,              rx->nr2_gain_method);
  GetPropI1("receiver.%d.nr2_npe_method", rx->id,               rx->nr2_npe_method);
  GetPropI1("receiver.%d.nr2_ae", rx->id,                       rx->nr2_ae);
  GetPropF1("receiver.%d.nr2_trained_threshold", rx->id,        rx->nr2_trained_threshold);
  GetPropF1("receiver.%d.nr2_trained_t2", rx->id,               rx->nr2_trained_t2);
  GetPropI1("receiver.%d.nb2_mode", rx->id,                     rx->nb2_mode);
  GetPropF1("receiver.%d.nb_tau", rx->id,                       rx->nb_tau);
  GetPropF1("receiver.%d.nb_advtime", rx->id,                   rx->nb_advtime);
  GetPropF1("receiver.%d.nb_hang", rx->id,                      rx->nb_hang);
  GetPropF1("receiver.%d.nb_thresh", rx->id,                    rx->nb_thresh);
#ifdef EXTNR
  GetPropF1("receiver.%d.nr4_reduction_amount", rx->id,         rx->nr4_reduction_amount);
  GetPropF1("receiver.%d.nr4_smoothing_factor", rx->id,         rx->nr4_smoothing_factor);
  GetPropF1("receiver.%d.nr4_whitening_factor", rx->id,         rx->nr4_whitening_factor);
  GetPropF1("receiver.%d.nr4_noise_rescale", rx->id,            rx->nr4_noise_rescale);
  GetPropF1("receiver.%d.nr4_post_filter_threshold", rx->id,    rx->nr4_post_filter_threshold);
#endif
  GetPropI1("receiver.%d.deviation", rx->id,                    rx->deviation);
  GetPropI1("receiver.%d.squelch_enable", rx->id,               rx->squelch_enable);
  GetPropF1("receiver.%d.squelch", rx->id,                      rx->squelch);
  GetPropI1("receiver.%d.binaural", rx->id,                     rx->binaural);
  GetPropI1("receiver.%d.zoom", rx->id,                         rx->zoom);
  GetPropI1("receiver.%d.pan", rx->id,                          rx->pan);
  GetPropI1("receiver.%d.eq_enable", rx->id,                    rx->eq_enable);

  for (int i = 0; i < 11; i++) {
    GetPropF2("receiver.%d.eq_freq[%d]", rx->id, i,             rx->eq_freq[i]);
    GetPropF2("receiver.%d.eq_gain[%d]", rx->id, i,             rx->eq_gain[i]);
  }
}

void rx_reconfigure(RECEIVER *rx, int height) {
  int y = 0;
  //
  // myheight is the size of the waterfall or the panadapter
  // which is the full or half of the height depending on whether BOTH
  // are displayed
  //
  t_print("%s: rx=%d width=%d height=%d\n", __FUNCTION__, rx->id, rx->width, rx->height);
  g_mutex_lock(&rx->display_mutex);
  int myheight = (rx->display_panadapter && rx->display_waterfall) ? height / 2 : height;
  rx->height = height; // total height
  gtk_widget_set_size_request(rx->panel, rx->width, rx->height);

  if (rx->display_panadapter) {
    if (rx->panadapter == NULL) {
      t_print("%s: panadapter_init: width:%d height:%d\n", __FUNCTION__, rx->width, myheight);
      rx_panadapter_init(rx, rx->width, myheight);
      gtk_fixed_put(GTK_FIXED(rx->panel), rx->panadapter, 0, y); // y=0 here always
    } else {
      // set the size
      gtk_widget_set_size_request(rx->panadapter, rx->width, myheight);
      // move the current one
      gtk_fixed_move(GTK_FIXED(rx->panel), rx->panadapter, 0, y);
    }

    y += myheight;
  } else {
    if (rx->panadapter != NULL) {
      gtk_container_remove(GTK_CONTAINER(rx->panel), rx->panadapter);
      rx->panadapter = NULL;
    }
  }

  if (rx->display_waterfall) {
    if (rx->waterfall == NULL) {
      t_print("%s: waterfall_init: width:%d height:%d\n", __FUNCTION__, rx->width, myheight);
      waterfall_init(rx, rx->width, myheight);
      gtk_fixed_put(GTK_FIXED(rx->panel), rx->waterfall, 0, y); // y=0 if ONLY waterfall is present
    } else {
      // set the size
      t_print("%s: waterfall set_size_request: width:%d height:%d\n", __FUNCTION__, rx->width, myheight);
      gtk_widget_set_size_request(rx->waterfall, rx->width, myheight);
      // move the current one
      gtk_fixed_move(GTK_FIXED(rx->panel), rx->waterfall, 0, y);
    }
  } else {
    if (rx->waterfall != NULL) {
      gtk_container_remove(GTK_CONTAINER(rx->panel), rx->waterfall);
      rx->waterfall = NULL;
    }
  }

  gtk_widget_show_all(rx->panel);
  g_mutex_unlock(&rx->display_mutex);
}

static int rx_update_display(gpointer data) {
  RECEIVER *rx = (RECEIVER *)data;

  if (rx->displaying) {
    if (rx->pixels > 0) {
      int rc;
      g_mutex_lock(&rx->display_mutex);
      rc = rx_get_pixels(rx);

      if (rc) {
        if (rx->display_panadapter) {
          rx_panadapter_update(rx);
        }

        if (rx->display_waterfall) {
          waterfall_update(rx);
        }
      }

      g_mutex_unlock(&rx->display_mutex);

      if (active_receiver == rx) {
        //
        // since rx->meter is used in other places as well (e.g. rigctl),
        // the value obtained from WDSP is best corrected HERE for
        // possible gain and attenuation
        //
        int id = rx->id;
        int b  = vfo[id].band;
        const BAND *band = band_get_band(b);
        int calib = rx_gain_calibration - band->gain;
        double level = rx_get_smeter(rx);
        level += (double)calib + (double)adc[rx->adc].attenuation - adc[rx->adc].gain;

        if (filter_board == CHARLY25 && rx->adc == 0) {
          level += (double)(12 * rx->alex_attenuation - 18 * rx->preamp - 18 * rx->dither);
        }

        if (filter_board == ALEX && rx->adc == 0) {
          level += (double)(10 * rx->alex_attenuation);
        }

        rx->meter = level;
        meter_update(rx, SMETER, rx->meter, 0.0, 0.0);
      }

      return TRUE;
    }
  }

  return FALSE;
}

#ifdef CLIENT_SERVER
void rx_remote_update_display(RECEIVER *rx) {
  if (rx->displaying) {
    if (rx->pixels > 0) {
      g_mutex_lock(&rx->display_mutex);

      if (rx->display_panadapter) {
        rx_panadapter_update(rx);
      }

      if (rx->display_waterfall) {
        waterfall_update(rx);
      }

      if (active_receiver == rx) {
        meter_update(rx, SMETER, rx->meter, 0.0, 0.0);
      }

      g_mutex_unlock(&rx->display_mutex);
    }
  }
}

#endif

void rx_set_displaying(RECEIVER *rx) {
  if (radio_is_remote) {
    return;
  }

  if (rx->displaying) {
    if (rx->update_timer_id > 0) {
      g_source_remove(rx->update_timer_id);
    }

    rx->update_timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE, 1000 / rx->fps, rx_update_display, rx, NULL);
  } else {
    if (rx->update_timer_id > 0) {
      g_source_remove(rx->update_timer_id);
      rx->update_timer_id = 0;
    }
  }
}

static void rx_create_visual(RECEIVER *rx) {
  int y = 0;
  rx->panel = gtk_fixed_new();
  t_print("%s: RXid=%d width=%d height=%d %p\n", __FUNCTION__, rx->id, rx->width, rx->height, rx->panel);
  g_object_weak_ref(G_OBJECT(rx->panel), rx_weak_notify, (gpointer)rx);
  gtk_widget_set_size_request (rx->panel, rx->width, rx->height);
  rx->panadapter = NULL;
  rx->waterfall = NULL;
  int height = rx->height;

  if (rx->display_waterfall) {
    height = height / 2;
  }

  rx_panadapter_init(rx, rx->width, height);
  t_print("%s: panadapter height=%d y=%d %p\n", __FUNCTION__, height, y, rx->panadapter);
  g_object_weak_ref(G_OBJECT(rx->panadapter), rx_weak_notify, (gpointer)rx);
  gtk_fixed_put(GTK_FIXED(rx->panel), rx->panadapter, 0, y);
  y += height;

  if (rx->display_waterfall) {
    waterfall_init(rx, rx->width, height);
    t_print("%s: waterfall height=%d y=%d %p\n", __FUNCTION__, height, y, rx->waterfall);
    g_object_weak_ref(G_OBJECT(rx->waterfall), rx_weak_notify, (gpointer)rx);
    gtk_fixed_put(GTK_FIXED(rx->panel), rx->waterfall, 0, y);
  }

  gtk_widget_show_all(rx->panel);
}

RECEIVER *rx_create_pure_signal_receiver(int id, int sample_rate, int width, int fps) {
  //
  // For a PureSignal receiver, most parameters are not needed
  // so we fill the entire data with zeroes
  //
  RECEIVER *rx = malloc(sizeof(RECEIVER));
  memset (rx, 0, sizeof(RECEIVER));
  //
  // Setting the non-zero parameters.
  // a PS_TX_FEEDBACK receiver only needs and id and the iq_input_buffer (nothing else).
  // a PS_RX_FEEDBACK receiver needs an analyzer (for the MON button), and
  // it needs an alex_rx_antenna setting.
  //
  rx->id = id;
  rx->buffer_size = 1024;
  rx->iq_input_buffer = g_new(double, 2 * rx->buffer_size);

  if (id == PS_RX_FEEDBACK) {
    //
    // The analyzer is only used if
    // displaying the RX feedback samples (MON button in PS menu).
    //
    rx->alex_antenna = 0;
    rx->adc = 0;
    rx_restore_state(rx);
    g_mutex_init(&rx->mutex);
    g_mutex_init(&rx->display_mutex);
    rx->sample_rate = sample_rate;
    rx->fps = fps;
    rx->width = width; // used to re-calculate rx->pixels upon sample rate change
    rx->pixels = (sample_rate / 24000) * width;
    rx->pixel_samples = g_new(float, rx->pixels);
    //
    // These values (including fps) should match those of the TX display
    //
    rx->display_detector_mode = DET_PEAK;
    rx->display_average_time  = 120.0;
    rx->display_average_mode  = AVG_LOGRECURSIVE;
    rx_create_analyzer(rx);
    rx_set_detector(rx);
    rx_set_average(rx);
  }

  return rx;
}

RECEIVER *rx_create_receiver(int id, int pixels, int width, int height) {
  t_print("%s: RXid=%d pixels=%d width=%d height=%d\n", __FUNCTION__, id, pixels, width, height);
  RECEIVER *rx = malloc(sizeof(RECEIVER));
  //
  // This is to guard against programming errors
  // (missing initializations)
  //
  memset(rx, 0, sizeof(RECEIVER));
  //
  rx->id = id;
  g_mutex_init(&rx->mutex);
  g_mutex_init(&rx->display_mutex);

  switch (id) {
  case 0:
    rx->adc = 0;
    break;

  default:
    switch (device) {
    case DEVICE_METIS:
    case DEVICE_OZY:
    case DEVICE_HERMES:
    case DEVICE_HERMES_LITE:
    case DEVICE_HERMES_LITE2:
    case NEW_DEVICE_ATLAS:
    case NEW_DEVICE_HERMES:
      rx->adc = 0;
      break;

    default:
      rx->adc = 1;
      break;
    }
  }

  //t_print("%s: RXid=%d default adc=%d\n",__FUNCTION__,rx->id, rx->adc);
  rx->sample_rate = 48000;

  if (device == SOAPYSDR_USB_DEVICE) {
#ifdef SOAPYSDR
    rx->sample_rate = radio->info.soapy.sample_rate;
    t_print("%s: RXid=%d sample_rate=%d\n", __FUNCTION__, rx->id, rx->sample_rate);
#endif
    rx->resampler = NULL;
    rx->resample_buffer = NULL;
  }

  //
  // For larger sample rates we could use a larger buffer_size, since then
  // the number of audio samples per batch is rather small. However, the buffer
  // size is not changed when the sample rate is changed.
  //
  rx->buffer_size = 1024;
  rx->dsp_size = 2048;
  rx->fft_size = 2048;
  rx->low_latency = 0;
  rx->smetermode = SMETER_AVERAGE;
  rx->fps = 10;
  rx->update_timer_id = 0;
  rx->width = width;
  rx->height = height;
  rx->samples = 0;
  rx->displaying = 0;
  rx->display_panadapter = 1;
  rx->display_waterfall = 1;
  rx->panadapter_high = -40;
  rx->panadapter_low = -140;
  rx->panadapter_step = 20;
  rx->waterfall_high = -40;
  rx->waterfall_low = -140;
  rx->waterfall_automatic = 1;
  rx->display_filled = 1;
  rx->display_gradient = 1;
  rx->display_detector_mode = DET_AVERAGE;
  rx->display_average_mode = AVG_LOGRECURSIVE;
  rx->display_average_time = 120.0;
  rx->volume = -20.0;
  rx->dither = 0;
  rx->random = 0;
  rx->preamp = 0;
  rx->nb = 0;
  rx->nr = 0;
  rx->anf = 0;
  rx->snb = 0;
  rx->nr_agc = 0;                   // NR/NR2/ANF before AGC
  rx->nr2_gain_method = 2;          // Gamma
  rx->nr2_npe_method = 0;           // OSMS
  rx->nr2_ae = 1;                   // Artifact Elimination is "on"
  rx->nr2_trained_threshold = -0.5; // Threshold if gain method is "Trained"
  rx->nr2_trained_t2 = 0.2;         // t2 value for trained threshold
  //
  // It has been reported that the piHPSDR noise blankers do not function
  // satisfactorily. I could reproduce this after building an "impulse noise source"
  // into the HPSDR simulator, and also confirmed that a popular Windows SDR program
  // has much better NB/NB2 performance.
  //
  // Digging into it, I found these SDR programs used NB default parameters *very*
  // different from those recommended in the WDSP manual: slewtime, hangtime and advtime
  // default to 0.01 msec, and the threshold to 30 (which is internally multiplied with 0.165
  // to obtain the WDSP threshold parameter).
  //
  rx->nb_tau =     0.00001;       // Slew=0.01    in the DSP menu
  rx->nb_advtime = 0.00001;       // Lead=0.01    in the DSP menu
  rx->nb_hang =    0.00001;       // Lag=0.01     in the DSP menu
  rx->nb_thresh =  4.95;          // Threshold=30 in the DSP menu
  rx->nb2_mode = 0;               // Zero mode
#ifdef EXTNR
  rx->nr4_reduction_amount = 10.0;
  rx->nr4_smoothing_factor = 0.0;
  rx->nr4_whitening_factor = 0.0;
  rx->nr4_noise_rescale = 2.0;
  rx->nr4_post_filter_threshold = -10.0;
#endif
  const BAND *b = band_get_band(vfo[rx->id].band);
  rx->alex_antenna = b->alexRxAntenna;

  if (have_alex_att) {
    rx->alex_attenuation = b->alexAttenuation;
  } else {
    rx->alex_attenuation = 0;
  }

  rx->agc = AGC_MEDIUM;
  rx->agc_gain = 80.0;
  rx->agc_slope = 35.0;
  rx->agc_hang_threshold = 0.0;
  rx->local_audio = 0;
  g_mutex_init(&rx->local_audio_mutex);
  rx->local_audio_buffer = NULL;
  STRLCPY(rx->audio_name, "NO AUDIO", sizeof(rx->audio_name));
  rx->mute_when_not_active = 0;
  rx->audio_channel = STEREO;
  rx->audio_device = -1;
  rx->squelch_enable = 0;
  rx->squelch = 0;
  rx->binaural = 0;
  rx->filter_high = 525;
  rx->filter_low = 275;
  rx->deviation = 2500;
  rx->mute_radio = 0;
  rx->zoom = 1;
  rx->pan = 0;
  rx->eq_enable = 0;
  rx->eq_freq[0]  =     0.0;
  rx->eq_freq[1]  =    50.0;
  rx->eq_freq[2]  =   100.0;
  rx->eq_freq[3]  =   200.0;
  rx->eq_freq[4]  =   500.0;
  rx->eq_freq[5]  =  1000.0;
  rx->eq_freq[6]  =  1500.0;
  rx->eq_freq[7]  =  2000.0;
  rx->eq_freq[8]  =  2500.0;
  rx->eq_freq[9]  =  3000.0;
  rx->eq_freq[10] =  5000.0;
  rx->eq_gain[0]  = 0.0;
  rx->eq_gain[1]  = 0.0;
  rx->eq_gain[2]  = 0.0;
  rx->eq_gain[3]  = 0.0;
  rx->eq_gain[4]  = 0.0;
  rx->eq_gain[5]  = 0.0;
  rx->eq_gain[6]  = 0.0;
  rx->eq_gain[7]  = 0.0;
  rx->eq_gain[8]  = 0.0;
  rx->eq_gain[9]  = 0.0;
  rx->eq_gain[10] = 0.0;
  //
  // Overwrite all these values with data from the props file
  //
  rx_restore_state(rx);

  //
  // If this is the second receiver in P1, over-write sample rate
  // with that of the first  receiver. Different sample rates in
  // the props file may arise due to illegal hand editing or
  // firmware downgrade from P2 to P1.
  //
  if (protocol == ORIGINAL_PROTOCOL && id == 1) {
    rx->sample_rate = receiver[0]->sample_rate;
  }

  //
  // allocate buffers
  //
  rx->iq_input_buffer = g_new(double, 2 * rx->buffer_size);
  rx->pixels = pixels * rx->zoom;
  rx->pixel_samples = g_new(float, rx->pixels);
  t_print("%s (after restore): id=%d local_audio=%d\n", __FUNCTION__, rx->id, rx->local_audio);
  int scale = rx->sample_rate / 48000;
  rx->output_samples = rx->buffer_size / scale;
  rx->audio_output_buffer = g_new(double, 2 * rx->output_samples);
  t_print("%s: RXid=%d output_samples=%d audio_output_buffer=%p\n", __FUNCTION__, rx->id, rx->output_samples,
          rx->audio_output_buffer);
  rx->hz_per_pixel = (double)rx->sample_rate / (double)rx->pixels;
  // setup wdsp for this receiver
  t_print("%s: RXid=%d after restore adc=%d\n", __FUNCTION__, rx->id, rx->adc);
  t_print("%s: OpenChannel RXid=%d buffer_size=%d dsp_size=%d fft_size=%d sample_rate=%d\n",
          __FUNCTION__,
          rx->id,
          rx->buffer_size,
          rx->dsp_size,
          rx->fft_size,
          rx->sample_rate);
  OpenChannel(rx->id,                     // channel
              rx->buffer_size,            // in_size
              rx->dsp_size,               // dsp_size
              rx->sample_rate,            // input_samplerate
              48000,                      // dsp rate
              48000,                      // output_samplerate
              0,                          // type (0=receive)
              1,                          // state (run)
              0.010, 0.025, 0.0, 0.010,   // DelayUp, SlewUp, DelayDown, SlewDown
              1);                         // Wait for data in fexchange0
  //
  // noise blankers
  //
  create_anbEXT(rx->id, 1, rx->buffer_size, rx->sample_rate, 0.0001, 0.0001, 0.0001, 0.05, 20);
  create_nobEXT(rx->id, 1, 0, rx->buffer_size, rx->sample_rate, 0.0001, 0.0001, 0.0001, 0.05, 20);
  //
  // Some WDSP settings that are never changed
  //
  SetRXABandpassWindow(rx->id, 1);    // use 7-term BlackmanHarris Window
  SetRXABandpassRun(rx->id, 1);       // enable Bandbass
  SetRXAAMDSBMode(rx->id, 0);         // use both sidebands in SAM
  SetRXAPanelRun(rx->id, 1);          // turn on RXA panel
  SetRXAPanelSelect(rx->id, 3);       // use both I and Q input
  //
  // Apply initial settings
  //
  rx_set_noise(rx);
  rx_set_fft_size(rx);
  rx_set_fft_latency(rx);
  rx_set_offset(rx, 0);
  rx_set_af_gain(rx);
  rx_set_af_binaural(rx);
  rx_set_equalizer(rx);
  rx_mode_changed(rx);   // this will call rx_filter_changed() as well
  rx_create_analyzer(rx);
  rx_set_detector(rx);
  rx_set_average(rx);
  rx_create_visual(rx);

  if (rx->local_audio) {
    if (audio_open_output(rx) < 0) {
      rx->local_audio = 0;
    }
  }

  // defer set_agc until here, otherwise the AGC threshold is not computed correctly
  rx_set_agc(rx);
  rx->txrxcount = 0;
  rx->txrxmax = 0;
  return rx;
}

void rx_change_adc(const RECEIVER *rx) {
  schedule_high_priority();
  schedule_receive_specific();
}

void rx_set_frequency(RECEIVER *rx, long long f) {
  int id = rx->id;

  //
  // update VFO frequency, and let rx_frequency_changed do the rest
  //
  if (vfo[id].ctun) {
    vfo[id].ctun_frequency = f;
  } else {
    vfo[id].frequency = f;
  }

  rx_frequency_changed(rx);
}

void rx_frequency_changed(RECEIVER *rx) {
  int id = rx->id;

  if (vfo[id].ctun) {
    long long frequency = vfo[id].frequency;
    long long half = (long long)rx->sample_rate / 2LL;
    long long rx_low = vfo[id].ctun_frequency + rx->filter_low;
    long long rx_high = vfo[id].ctun_frequency + rx->filter_high;

    if (rx_low < frequency - half || rx_high > frequency + half) {
      //
      // Perhaps this is paranoia, but a "legal" VFO might turn
      // into an "illegal" when when reducing the sample rate,
      // thus narrowing the CTUN window.
      // If the "filter window" has left the CTUN range, CTUN is
      // reset such that the CTUN center frequency is placed at
      // the new frequency
      //
      t_print("%s: CTUN freq out of range\n", __FUNCTION__);
      vfo[id].frequency = vfo[id].ctun_frequency;
    }

    if (rx->zoom > 1) {
      //
      // Adjust PAN if new filter width has moved out of
      // current display range
      // TODO: what if this happens with CTUN "off"?
      //
      long long min_display = frequency - half + (long long)((double)rx->pan * rx->hz_per_pixel);
      long long max_display = min_display + (long long)((double)rx->width * rx->hz_per_pixel);

      if (rx_low <= min_display) {
        rx->pan = rx->pan - (rx->width / 2);

        if (rx->pan < 0) { rx->pan = 0; }

        set_pan(id, rx->pan);
      } else if (rx_high >= max_display) {
        rx->pan = rx->pan + (rx->width / 2);

        if (rx->pan > (rx->pixels - rx->width)) { rx->pan = rx->pixels - rx->width; }

        set_pan(id, rx->pan);
      }
    }

    //
    // Compute new offset
    //
    vfo[id].offset = vfo[id].ctun_frequency - vfo[id].frequency;

    if (vfo[id].rit_enabled) {
      vfo[id].offset += vfo[id].rit;
    }
  } else {
    //
    // This may be part of a CTUN ON->OFF transition
    //
    vfo[id].offset = 0;

    if (vfo[id].rit_enabled) {
      vfo[id].offset = vfo[id].rit;
    }
  }

  //
  // To make this bullet-proof, report the (possibly new) offset to WDSP
  // and send the (possibly changed) frequency to the radio in any case.
  //
  rx_set_offset(rx, vfo[id].offset);

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    // P1 does this automatically
    break;

  case NEW_PROTOCOL:
    schedule_high_priority(); // send new frequency
    break;
#if SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    soapy_protocol_set_rx_frequency(rx, id);
    break;
#endif
  }
}

void rx_filter_changed(RECEIVER *rx) {
  rx_set_filter(rx);

  if (can_transmit) {
    if ((transmitter->use_rx_filter && rx == active_receiver) || vfo_get_tx_mode() == modeFMN) {
      tx_set_filter(transmitter);
    }
  }

  //
  // TODO: Filter window has possibly moved outside CTUN range
  //
}

void rx_mode_changed(RECEIVER *rx) {
  rx_set_mode(rx);
  rx_filter_changed(rx);
}

void rx_vfo_changed(RECEIVER *rx) {
  //
  // Called when the VFO controlling rx has changed,
  // e.g. after a "swap VFO" action
  //
  rx_frequency_changed(rx);
  rx_mode_changed(rx);
}

//////////////////////////////////////////////////////////////////////////////////////
//
// rx_add_iq_samples (rx_add_div_iq_samples),  rx_full_buffer, and rx_process_buffer
// form the "RX engine".
//
//////////////////////////////////////////////////////////////////////////////////////

static void rx_process_buffer(RECEIVER *rx) {
  double left_sample, right_sample;
  short left_audio_sample, right_audio_sample;
  int i;

  //t_print("%s: rx=%p id=%d output_samples=%d audio_output_buffer=%p\n",__FUNCTION__,rx,rx->id,rx->output_samples,rx->audio_output_buffer);
  for (i = 0; i < rx->output_samples; i++) {
    if (radio_is_transmitting() && (!duplex || mute_rx_while_transmitting)) {
      left_sample = 0.0;
      right_sample = 0.0;
      left_audio_sample = 0;
      right_audio_sample = 0;
    } else {
      left_sample = rx->audio_output_buffer[i * 2];
      right_sample = rx->audio_output_buffer[(i * 2) + 1];
      left_audio_sample = (short)(left_sample * 32767.0);
      right_audio_sample = (short)(right_sample * 32767.0);
    }

    if (rx->local_audio) {
      if (rx != active_receiver && rx->mute_when_not_active) {
        left_sample = 0.0;
        right_sample = 0.0;
      } else {
        switch (rx->audio_channel) {
        case STEREO:
          break;

        case LEFT:
          right_sample = 0.0;
          break;

        case RIGHT:
          left_sample = 0.0;
          break;
        }
      }

      audio_write(rx, (float)left_sample, (float)right_sample);
    }

#ifdef CLIENT_SERVER

    if (remoteclients != NULL) {
      remote_audio(rx, left_audio_sample, right_audio_sample);
    }

#endif

    if (rx == active_receiver && capture_state == CAP_RECORDING) {
      if (capture_record_pointer < capture_max) {
        //
        // normalize samples:
        // when using AGC, the samples of strong s9 signals are about 0.8
        //
        double scale = 0.6 * pow(10.0, -0.05 * rx->volume);
        capture_data[capture_record_pointer++] = scale * (left_sample + right_sample);
      } else {
        // switching the state to RECORD_DONE takes care that the
        // CAPTURE switch is "pressed" only once
        capture_state = CAP_RECORD_DONE;
        schedule_action(CAPTURE, PRESSED, 0);
      }
    }

    if (rx == active_receiver && !pre_mox) {
      //
      // Note the "Mute Radio" checkbox in the RX menu mutes the
      // audio in the HPSDR data stream *only*, local audio is
      // not affected.
      //
      switch (protocol) {
      case ORIGINAL_PROTOCOL:
        if (rx->mute_radio) {
          old_protocol_audio_samples((short)0, (short)0);
        } else {
          old_protocol_audio_samples(left_audio_sample, right_audio_sample);
        }

        break;

      case NEW_PROTOCOL:
        if (rx->mute_radio) {
          new_protocol_audio_samples((short)0, (short)0);
        } else {
          new_protocol_audio_samples(left_audio_sample, right_audio_sample);
        }

        break;

      case SOAPYSDR_PROTOCOL:
        break;
      }
    }
  }
}

void rx_full_buffer(RECEIVER *rx) {
  int error;

  //t_print("%s: rx=%p\n",__FUNCTION__,rx);
  //
  // rx->mutex is locked if a sample rate change is currently going on,
  // in this case we should not block the receiver thread
  //
  if (g_mutex_trylock(&rx->mutex)) {
    //
    // noise blanker works on original IQ samples with input sample rate
    //
    switch (rx->nb) {
    case 1:
      xanbEXT (rx->id, rx->iq_input_buffer, rx->iq_input_buffer);
      break;

    case 2:
      xnobEXT (rx->id, rx->iq_input_buffer, rx->iq_input_buffer);
      break;

    default:
      // do nothing
      break;
    }

    fexchange0(rx->id, rx->iq_input_buffer, rx->audio_output_buffer, &error);

    if (error != 0) {
      t_print("%s: id=%d fexchange0: error=%d\n", __FUNCTION__, rx->id, error);
    }

    if (rx->displaying) {
      g_mutex_lock(&rx->display_mutex);
      Spectrum0(1, rx->id, 0, 0, rx->iq_input_buffer);
      g_mutex_unlock(&rx->display_mutex);
    }

    rx_process_buffer(rx);
    g_mutex_unlock(&rx->mutex);
  }
}

void rx_add_iq_samples(RECEIVER *rx, double i_sample, double q_sample) {
  //
  // At the end of a TX/RX transition, txrxcount is set to zero,
  // and txrxmax to some suitable value.
  // Then, the first txrxmax RXIQ samples are "silenced"
  // This is necessary on systems where RX feedback samples
  // from cross-talk at the TRX relay arrive with some delay.
  //
  // If txrxmax is zero, no "silencing" takes place here,
  // this is the case for radios not showing this problem,
  // and generally if in CW mode or using duplex.
  //
  if (rx->txrxcount < rx->txrxmax) {
    i_sample = 0.0;
    q_sample = 0.0;
    rx->txrxcount++;
  }

  rx->iq_input_buffer[rx->samples * 2] = i_sample;
  rx->iq_input_buffer[(rx->samples * 2) + 1] = q_sample;
  rx->samples = rx->samples + 1;

  if (rx->samples >= rx->buffer_size) {
    rx_full_buffer(rx);
    rx->samples = 0;
  }
}

void rx_add_div_iq_samples(RECEIVER *rx, double i0, double q0, double i1, double q1) {
  //
  // Note that we sum the second channel onto the first one
  // and then simply pass to add_iq_samples
  //
  double i_sample = i0 + (div_cos * i1 - div_sin * q1);
  double q_sample = q0 + (div_sin * i1 + div_cos * q1);
  rx_add_iq_samples(rx, i_sample, q_sample);
}

void rx_update_zoom(RECEIVER *rx) {
  //
  // This is called whenever rx->zoom or rx->width changes,
  // since in both cases the analyzer must be restarted.
  //
  rx->pixels = rx->width * rx->zoom;
  rx->hz_per_pixel = (double)rx->sample_rate / (double)rx->pixels;

  if (rx->zoom == 1) {
    rx->pan = 0;
  } else {
    if (vfo[rx->id].ctun) {
      long long min_frequency = vfo[rx->id].frequency - (long long)(rx->sample_rate / 2);
      rx->pan = ((vfo[rx->id].ctun_frequency - min_frequency) / rx->hz_per_pixel) - (rx->width / 2);

      if (rx->pan < 0) { rx->pan = 0; }

      if (rx->pan > (rx->pixels - rx->width)) { rx->pan = rx->pixels - rx->width; }
    } else {
      rx->pan = (rx->pixels / 2) - (rx->width / 2);
    }
  }

  if (!radio_is_remote) {
    if (rx->pixel_samples != NULL) {
      g_free(rx->pixel_samples);
    }

    rx->pixel_samples = g_new(float, rx->pixels);
    rx_set_analyzer(rx);
  }
}

void rx_set_filter(RECEIVER *rx) {
  //
  // - set filter edges and deviation in rx
  // - determine on the use of the CW peak filter
  // - re-caloc AGC since this depends on the filter width
  //
  int id = rx->id;
  int m = vfo[id].mode;
  FILTER *mode_filters = filters[m];
  const FILTER *filter = &mode_filters[vfo[id].filter]; // ignored in FMN
  int have_peak = 0;

  switch (m) {
  case modeCWL:
    //
    // translate CW filter edges to the CW pitch frequency
    //
    rx->filter_low = -cw_keyer_sidetone_frequency + filter->low;
    rx->filter_high = -cw_keyer_sidetone_frequency + filter->high;
    have_peak = vfo[id].cwAudioPeakFilter;
    break;

  case modeCWU:
    rx->filter_low = cw_keyer_sidetone_frequency + filter->low;
    rx->filter_high = cw_keyer_sidetone_frequency + filter->high;
    have_peak = vfo[id].cwAudioPeakFilter;
    break;

  case  modeFMN:
    //
    // FM filter settings are ignored, instead, the filter
    // size is calculated from the deviation
    //
    rx->deviation = vfo[id].deviation;

    if (rx->deviation == 2500) {
      rx->filter_low = -5500;
      rx->filter_high = 5500;
    } else {
      rx->filter_low = -8000;
      rx->filter_high = 8000;
    }

    break;

  default:
    rx->filter_low = filter->low;
    rx->filter_high = filter->high;
    break;
  }

  rx_set_deviation(rx);
  rx_set_bandpass(rx);
  rx_set_cw_peak(rx, have_peak, (double) cw_keyer_sidetone_frequency);
  rx_set_agc(rx);
}

void rx_set_framerate(RECEIVER *rx) {
  //
  // When changing the frame rate, the RX display update timer needs
  // be restarted, the averaging re-calculated, and the analyzer
  // parameter re-set
  //
  if (radio_is_remote) {
    return;
  }

  rx_set_displaying(rx);
  rx_set_average(rx);
  rx_set_analyzer(rx);
}

#ifdef CLIENT_SERVER
void rx_create_remote(RECEIVER *rx) {
  //
  // receiver structure already setup via INFO_RECEIVER packet.
  // since everything is done on the "local" side, we only need
  // to set-up the panadapter
  //
  rx_create_visual(rx);
}

#endif

///////////////////////////////////////////////////////
//
// WDSP wrappers.
// Calls to WDSP functions above this line should be
// kept to a minimum, and in general a call to a WDSP
// function should only occur *once* in the program,
// at best in a wrapper.
//
// WDSPRXDEBUG should only be activated for debugging
//
// TODO: If the radio is remote, make them a no-op.
//
////////////////////////////////////////////////////////

//#define WDSPRXDEBUG

void rx_change_sample_rate(RECEIVER *rx, int sample_rate) {
  // ToDo: move this outside of the WDSP wrappers and encapsulate WDSP calls
  //       in this function
#ifdef WDSPRXDEBUG
  t_print("RX id=%d SampleRate=%d\n", rx->id, sample_rate);
#endif
  g_mutex_lock(&rx->mutex);
  rx->sample_rate = sample_rate;
  schedule_receive_specific();
  int scale = rx->sample_rate / 48000;
  rx->output_samples = rx->buffer_size / scale;
  rx->hz_per_pixel = (double)rx->sample_rate / (double)rx->width;
  t_print("%s: id=%d rate=%d scale=%d buffer_size=%d output_samples=%d\n", __FUNCTION__, rx->id, sample_rate, scale,
          rx->buffer_size, rx->output_samples);

  //
  // In the old protocol, the RX_FEEDBACK sample rate is tied
  // to the radio's sample rate and therefore may vary.
  // Since there is no downstream WDSP receiver her, the only thing
  // we have to do here is to adapt the spectrum display of the
  // feedback and *must* then return (rx->id is not a WDSP channel!)
  //
  if (rx->id == PS_RX_FEEDBACK && protocol == ORIGINAL_PROTOCOL) {
    rx->pixels = (sample_rate / 24000) * rx->width;
    g_free(rx->pixel_samples);
    rx->pixel_samples = g_new(float, rx->pixels);
    rx_set_analyzer(rx);
    t_print("%s: PS RX FEEDBACK: id=%d rate=%d buffer_size=%d output_samples=%d\n",
            __FUNCTION__, rx->id, rx->sample_rate, rx->buffer_size, rx->output_samples);
    g_mutex_unlock(&rx->mutex);
    return;
  }

  //
  // re-calculate AGC line for panadapter since it depends on sample rate
  //
  GetRXAAGCThresh(rx->id, &rx->agc_thresh, 4096.0, (double)rx->sample_rate);

  //
  // If the sample rate is reduced, the size of the audio output buffer must ber increased
  //
  if (rx->audio_output_buffer != NULL) {
    g_free(rx->audio_output_buffer);
  }

  rx->audio_output_buffer = g_new(double, 2 * rx->output_samples);
  rx_off(rx);
  rx_set_analyzer(rx);
  SetInputSamplerate(rx->id, sample_rate);
  SetEXTANBSamplerate (rx->id, sample_rate);
  SetEXTNOBSamplerate (rx->id, sample_rate);
#ifdef SOAPYSDR

  if (protocol == SOAPYSDR_PROTOCOL) {
    soapy_protocol_change_sample_rate(rx);
    soapy_protocol_set_mic_sample_rate(rx->sample_rate);
  }

#endif
  rx_on(rx);
  //
  // for a non-PS receiver, adjust pixels and hz_per_pixel depending on the zoom value
  //
  rx->pixels = rx->width * rx->zoom;
  rx->hz_per_pixel = (double)rx->sample_rate / (double)rx->pixels;
  g_mutex_unlock(&rx->mutex);
  t_print("%s: RXid=%d rate=%d buffer_size=%d output_samples=%d\n", __FUNCTION__, rx->id, rx->sample_rate,
          rx->buffer_size, rx->output_samples);
}

void rx_close(const RECEIVER *rx) {
#ifdef WDSPRXDEBUG
#endif
  CloseChannel(rx->id);
}

int rx_get_pixels(RECEIVER *rx) {
  int rc;
  GetPixels(rx->id, 0, rx->pixel_samples, &rc);
  return rc;
}

double rx_get_smeter(const RECEIVER *rx) {
  double level;

  switch (rx->smetermode) {
  case SMETER_PEAK:
    level = GetRXAMeter(rx->id, RXA_S_PK);
    break;

  case SMETER_AVERAGE:
  default:
    level = GetRXAMeter(rx->id, RXA_S_AV);
    break;
  }

  return level;
}

void rx_create_analyzer(const RECEIVER *rx) {
#ifdef WDSPRXDEBUG
#endif
  //
  // After the analyzer has been created, its parameters
  // are set via rx_set_analyzer
  //
  int rc;
  XCreateAnalyzer(rx->id, &rc, 262144, 1, 1, NULL);

  if (rc != 0) {
    t_print("CreateAnalyzer failed for RXid=%d\n", rx->id);
  } else {
    rx_set_analyzer(rx);
  }
}

void rx_set_analyzer(const RECEIVER *rx) {
#ifdef WDSPRXDEBUG
#endif
  //
  // The analyzer depends on the framerate (fps), the
  // width (pixels), and the sample rate, as well as the
  // buffer size (this is constant).
  // So rx_set_analyzer() has to be called whenever fps, pixels,
  // or sample_rate change in rx
  //
  int flp[] = {0};
  const double keep_time = 0.1;
  const int n_pixout = 1;
  const int spur_elimination_ffts = 1;
  const int data_type = 1;
  const double kaiser_pi = 14.0;
  const double fscLin = 0;
  const double fscHin = 0;
  const int stitches = 1;
  const int calibration_data_set = 0;
  const double span_min_freq = 0.0;
  const double span_max_freq = 0.0;
  const int clip = 0;
  const int window_type = 5;
  const int afft_size = 16384;
  const int pixels = rx->pixels;
  int overlap;
  int max_w = afft_size + (int) min(keep_time * (double) rx->sample_rate,
                                    keep_time * (double) afft_size * (double) rx->fps);
  overlap = (int)fmax(0.0, ceil(afft_size - (double)rx->sample_rate / (double)rx->fps));
  SetAnalyzer(rx->id,
              n_pixout,
              spur_elimination_ffts,                // number of LO frequencies = number of ffts used in elimination
              data_type,                            // 0 for real input data (I only); 1 for complex input data (I & Q)
              flp,                                  // vector with one element for each LO frequency, 1 if high-side LO, 0 otherwise
              afft_size,                            // size of the fft, i.e., number of input samples
              rx->buffer_size,                      // number of samples transferred for each OpenBuffer()/CloseBuffer()
              window_type,                          // integer specifying which window function to use
              kaiser_pi,                            // PiAlpha parameter for Kaiser window
              overlap,                              // number of samples each fft (other than the first) is to re-use from the previous
              clip,                                 // number of fft output bins to be clipped from EACH side of each sub-span
              fscLin,                               // number of bins to clip from low end of entire span
              fscHin,                               // number of bins to clip from high end of entire span
              pixels,                               // number of pixel values to return.  may be either <= or > number of bins
              stitches,                             // number of sub-spans to concatenate to form a complete span
              calibration_data_set,                 // identifier of which set of calibration data to use
              span_min_freq,                        // frequency at first pixel value
              span_max_freq,                        // frequency at last pixel value
              max_w                                 // max samples to hold in input ring buffers
             );
}

void rx_off(const RECEIVER *rx) {
#ifdef WDSPRXDEBUG
#endif
  // switch receiver OFF, wait until slew-down completet
  SetChannelState(rx->id, 0, 1);
}

void rx_on(const RECEIVER *rx) {
#ifdef WDSPRXDEBUG
#endif
  // switch receiver ON
  SetChannelState(rx->id, 1, 0);
}

void rx_set_af_binaural(const RECEIVER *rx) {
#ifdef WDSPRXDEBUG
#endif
  SetRXAPanelBinaural(rx->id, rx->binaural);
}

void rx_set_af_gain(const RECEIVER *rx) {
#ifdef WDSPRXDEBUG
#endif
  //
  // volume is in dB from 0 ... -40 and this is
  // converted to  an amplitude from 0 ... 1.
  //
  // volume < -39.5  ==> amplitude = 0
  // volume >   0.0  ==> amplitude = 1
  //
  double volume = rx->volume;
  double amplitude;

  if (volume <= -39.5) {
    amplitude = 0.0;
  } else if (volume > 0.0) {
    amplitude = 1.0;
  } else {
    amplitude = pow(10.0, 0.05 * volume);
  }

  SetRXAPanelGain1 (rx->id, amplitude);
}

void rx_set_agc(RECEIVER *rx) {
#ifdef WDSPRXDEBUG
#endif
  //
  // Apply the AGC settings stored in rx.
  // Calculate new AGC and "hang" line levels
  // and store these in rx.
  //
  int id = rx->id;
  SetRXAAGCMode(id, rx->agc);
  SetRXAAGCSlope(id, rx->agc_slope);
  SetRXAAGCTop(id, rx->agc_gain);

  switch (rx->agc) {
  case AGC_OFF:
    break;

  case AGC_LONG:
    SetRXAAGCAttack(id, 2);
    SetRXAAGCHang(id, 2000);
    SetRXAAGCDecay(id, 2000);
    SetRXAAGCHangThreshold(id, (int)rx->agc_hang_threshold);
    break;

  case AGC_SLOW:
    SetRXAAGCAttack(id, 2);
    SetRXAAGCHang(id, 1000);
    SetRXAAGCDecay(id, 500);
    SetRXAAGCHangThreshold(id, (int)rx->agc_hang_threshold);
    break;

  case AGC_MEDIUM:
    SetRXAAGCAttack(id, 2);
    SetRXAAGCHang(id, 0);
    SetRXAAGCDecay(id, 250);
    SetRXAAGCHangThreshold(id, 100);
    break;

  case AGC_FAST:
    SetRXAAGCAttack(id, 2);
    SetRXAAGCHang(id, 0);
    SetRXAAGCDecay(id, 50);
    SetRXAAGCHangThreshold(id, 100);
    break;
  }

  //
  // Recalculate the "panadapter" AGC line positions.
  //
  GetRXAAGCHangLevel(id, &rx->agc_hang);
  GetRXAAGCThresh(id, &rx->agc_thresh, 4096.0, (double)rx->sample_rate);

  //
  // Update mode settings, if this is RX1
  //
  if (id == 0) {
    int mode = vfo[id].mode;
    mode_settings[mode].agc = rx->agc;
    copy_mode_settings(mode);
  }
}

void rx_set_average(const RECEIVER *rx) {
  //
  // avgmode refers to the _display_enum, while
  // wdspmode reflects the internal encoding in WDSP
  //
  int wdspmode;
  double t = 0.001 * rx->display_average_time;
  double display_avb = exp(-1.0 / ((double)rx->fps * t));
  int display_average = max(2, (int)fmin(60, (double)rx->fps * t));
  SetDisplayAvBackmult(rx->id, 0, display_avb);
  SetDisplayNumAverage(rx->id, 0, display_average);

  switch (rx->display_average_mode) {
  case AVG_NONE:
    wdspmode = AVERAGE_MODE_NONE;
    break;

  case AVG_RECURSIVE:
    wdspmode = AVERAGE_MODE_RECURSIVE;
    break;

  case AVG_LOGRECURSIVE:
  default:
    wdspmode = AVERAGE_MODE_LOG_RECURSIVE;
    break;

  case AVG_TIMEWINDOW:
    wdspmode = AVERAGE_MODE_TIME_WINDOW;
    break;
  }

#ifdef WDSPRXDEBUG
#endif
  //
  // I observed artifacts when changing the mode from "Log Recursive"
  // to "Time Window", so I generally switch to NONE first, and then
  // to the target averaging mode
  //
  SetDisplayAverageMode(rx->id, 0, AVERAGE_MODE_NONE);
  usleep(50000);
  SetDisplayAverageMode(rx->id, 0, wdspmode);
}

void rx_set_bandpass(const RECEIVER *rx) {
  RXASetPassband(rx->id, (double)rx->filter_low, (double)rx->filter_high);
#ifdef WDSPRXDEBUG
#endif
}

void rx_set_cw_peak(const RECEIVER *rx, int state, double freq) {
#ifdef WDSPRXDEBUG
#endif

  if (state) {
    double w = 0.25 * (rx->filter_high - rx->filter_low);

    if (w < 0.0) { w = -w; }      // This happens with CWL

    if (w < 25.0) { w = 25.0; }   // Do not go below 25 Hz

    SetRXASPCWFreq(rx->id, freq);
    SetRXASPCWBandwidth(rx->id, w);
    SetRXASPCWGain(rx->id, 1.50);
  }

  SetRXASPCWRun(rx->id, state);
}

void rx_set_detector(const RECEIVER *rx) {
  //
  // Apply display detector mode stored in rx
  //
  int wdspmode;

  switch (rx->display_detector_mode) {
  case DET_PEAK:
    wdspmode = DETECTOR_MODE_PEAK;
    break;

  case DET_AVERAGE:
  default:
    wdspmode = DETECTOR_MODE_AVERAGE;
    break;

  case DET_ROSENFELL:
    wdspmode = DETECTOR_MODE_ROSENFELL;
    break;

  case DET_SAMPLEHOLD:
    wdspmode = DETECTOR_MODE_SAMPLE;
    break;
  }

#ifdef WDSPRXDEBUG
#endif
  SetDisplayDetectorMode(rx->id, 0, wdspmode);
}

void rx_set_deviation(const RECEIVER *rx) {
  SetRXAFMDeviation(rx->id, (double)rx->deviation);
#ifdef WDSPRXDEBUG
#endif
}

void rx_set_equalizer(RECEIVER *rx) {
  //
  // Apply the equalizer parameters stored in rx
  //
  SetRXAEQProfile(rx->id, 10, rx->eq_freq, rx->eq_gain);
  SetRXAEQRun(rx->id, rx->eq_enable);
#ifdef WDSPRXDEBUG
  t_print("WDSP:RX id=%d Equalizer enable=%d allgain=%g\n", rx->id, rx->eq_enable, rx->eq_gain[0]);

  for (int i = 1; i < 11; i++) {
    t_print("... Chan=%2d Freq=%6g Gain=%4g\n", i, rx->eq_freq[i], rx->eq_gain[i]);
  }

#endif
}

void rx_set_fft_latency(const RECEIVER *rx) {
  RXASetMP(rx->id, rx->low_latency);
#ifdef WDSPRXDEBUG
#endif
}

void rx_set_fft_size(const RECEIVER *rx) {
  RXASetNC(rx->id, rx->fft_size);
#ifdef WDSPRXDEBUG
#endif
}

void rx_set_mode(const RECEIVER *rx) {
  //
  // Change the  mode of a running receiver according to what it stored
  // in its controlling VFO.
  //
  SetRXAMode(rx->id, vfo[rx->id].mode);
  rx_set_squelch(rx);
#ifdef WDSPRXDEBUG
#endif
}

void rx_set_noise(const RECEIVER *rx) {
  //
  // Set/Update all parameters stored  in rx
  // that areassociated with the "QRM fighters"
  //
  // a) NB
  //
  SetEXTANBTau(rx->id, rx->nb_tau);
  SetEXTANBHangtime(rx->id, rx->nb_hang);
  SetEXTANBAdvtime(rx->id, rx->nb_advtime);
  SetEXTANBThreshold(rx->id, rx->nb_thresh);
  SetEXTANBRun(rx->id, (rx->nb == 1));
  //
  // b) NB2
  //
  SetEXTNOBMode(rx->id, rx->nb2_mode);
  SetEXTNOBTau(rx->id, rx->nb_tau);
  SetEXTNOBHangtime(rx->id, rx->nb_hang);
  SetEXTNOBAdvtime(rx->id, rx->nb_advtime);
  SetEXTNOBThreshold(rx->id, rx->nb_thresh);
  SetEXTNOBRun(rx->id, (rx->nb == 2));
  //
  // c) NR
  //
  SetRXAANRVals(rx->id, 64, 16, 16e-4, 10e-7);
  SetRXAANRPosition(rx->id, rx->nr_agc);
  SetRXAANRRun(rx->id, (rx->nr == 1));
  //
  // d) NR2
  //
  SetRXAEMNRPosition(rx->id, rx->nr_agc);
  SetRXAEMNRgainMethod(rx->id, rx->nr2_gain_method);
  SetRXAEMNRnpeMethod(rx->id, rx->nr2_npe_method);
  SetRXAEMNRtrainZetaThresh(rx->id, rx->nr2_trained_threshold);
  SetRXAEMNRtrainT2(rx->id, rx->nr2_trained_t2);
  SetRXAEMNRaeRun(rx->id, rx->nr2_ae);
  SetRXAEMNRRun(rx->id, (rx->nr == 2));
  //
  // e) ANF
  //
  SetRXAANFRun(rx->id, rx->anf);
  SetRXAANFPosition(rx->id, rx->nr_agc);
  //
  // f) SNB
  //
  SetRXASNBARun(rx->id, rx->snb);
#ifdef EXTNR
  //
  // These WDSP functions only exist in a special, non-official version
  //
  // g) NR3
  //
  SetRXARNNRRun(rx->id, (rx->nr == 3));
  //
  // NR4
  //
  SetRXASBNRreductionAmount(rx->id,     rx->nr4_reduction_amount);
  SetRXASBNRsmoothingFactor(rx->id,     rx->nr4_smoothing_factor);
  SetRXASBNRwhiteningFactor(rx->id,     rx->nr4_whitening_factor);
  SetRXASBNRnoiseRescale(rx->id,        rx->nr4_noise_rescale);
  SetRXASBNRpostFilterThreshold(rx->id, rx->nr4_post_filter_threshold);
  SetRXASBNRRun(rx->id, (rx->nr == 4));
#endif
#ifdef WDSPRXDEBUG
#endif
}

void rx_set_offset(const RECEIVER *rx, long long offset) {
  if (offset == 0) {
    SetRXAShiftFreq(rx->id, (double)offset);
    RXANBPSetShiftFrequency(rx->id, (double)offset);
    SetRXAShiftRun(rx->id, 0);
  } else {
    SetRXAShiftFreq(rx->id, (double)offset);
    RXANBPSetShiftFrequency(rx->id, (double)offset);
    SetRXAShiftRun(rx->id, 1);
  }

#ifdef WDSPRXDEBUG
#endif
}

void rx_set_squelch(const RECEIVER *rx) {
  //
  // This applies the squelch mode stored in rx
  //
  double value;
  int    fm_squelch = 0;
  int    am_squelch = 0;
  int    voice_squelch = 0;

  //
  // the "slider" value goes from 0 (no squelch) to 100 (fully engaged)
  // and has to be mapped to
  //
  // AM    squelch:   -160.0 ... 0.00 dBm  linear interpolation
  // FM    squelch:      1.0 ... 0.01      expon. interpolation
  // Voice squelch:      0.0 ... 0.75      linear interpolation
  //
  switch (vfo[rx->id].mode) {
  case modeAM:
  case modeSAM:

  // My personal experience is that "Voice squelch" is of very
  // little use  when doing CW (this may apply to "AM squelch", too).
  case modeCWU:
  case modeCWL:
    //
    // Use AM squelch
    //
    value = ((rx->squelch / 100.0) * 160.0) - 160.0;
    SetRXAAMSQThreshold(rx->id, value);
    am_squelch = rx->squelch_enable;
#ifdef WDSPRXDEBUG
#endif
    break;

  case modeLSB:
  case modeUSB:
  case modeDSB:
    //
    // Use Voice squelch (new in WDSP 1.21)
    //
    value = 0.0075 * rx->squelch;
    voice_squelch = rx->squelch_enable;
    SetRXASSQLThreshold(rx->id, value);
    SetRXASSQLTauMute(rx->id, 0.1);
    SetRXASSQLTauUnMute(rx->id, 0.1);
#ifdef WDSPRXDEBUG
#endif
    break;

  case modeFMN:
    //
    // Use FM squelch
    //
    value = pow(10.0, -2.0 * rx->squelch / 100.0);
    SetRXAFMSQThreshold(rx->id, value);
    fm_squelch = rx->squelch_enable;
#ifdef WDSPRXDEBUG
#endif
    break;

  default:
    // no squelch for digital and other modes
    // (this can be discussed).
    break;
  }

  //
  // activate the desired squelch, and deactivate
  // all others
  //
  SetRXAAMSQRun(rx->id, am_squelch);
  SetRXAFMSQRun(rx->id, fm_squelch);
  SetRXASSQLRun(rx->id, voice_squelch);
}

