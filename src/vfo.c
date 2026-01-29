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
#include <semaphore.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <ifaddrs.h>
#include "appearance.h"
#include "discovered.h"
#include "main.h"
#include "agc.h"
#include "mode.h"
#include "filter.h"
#include "bandstack.h"
#include "band.h"
#include "property.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "new_protocol.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "vfo.h"
#include "channel.h"
#include "toolbar.h"
#include "new_menu.h"
#include "rigctl.h"
#include "ext.h"
#include "filter.h"
#include "actions.h"
#include "noise_menu.h"
#include "equalizer_menu.h"
#include "message.h"
#include "sliders.h"
#include "audio.h"
#include "wdsp.h"


GMutex copy_string_mutex;

static int my_width;
static int my_height;

static GtkWidget *vfo_panel;
static cairo_surface_t *vfo_surface = NULL;

int steps[] = {1, 10, 25, 50, 100, 250, 500, 1000, 5000, 6250, 9000, 10000, 12500, 100000, 250000, 500000, 1000000};
char *step_labels[] = {"1Hz", "10Hz", "25Hz", "50Hz", "100Hz", "250Hz", "500Hz", "1kHz",
                       "5kHz", "6.25k", "9kHz", "10kHz", "12.5k", "100kHz", "250kHz", "500kHz", "1MHz"
                      };

static inline long long ROUND(long long freq, int nsteps, int step) {
  //
  // Move frequency f by n steps, adjust to multiple of step size
  // This should replace *all* divisions by the step size
  // If nsteps is zero, this is simply rounding.
  //
  long  long f;
  f = (freq + step / 2) / step;
  f = f + nsteps;
  f = f * step;
  return f;
}

struct _vfo vfo[MAX_VFOS];
struct _mode_settings mode_settings[MODES];

const char* getModeName(int mode) {
  switch (mode) {
  case modeLSB:
    return "LSB";

  case modeUSB:
    return "USB";

  case modeDSB:
    return "DSB";

  case modeCWL:
    return "CW-L";

  case modeCWU:
    return "CW-U";

  case modeFMN:
    return "FM-N";

  case modeAM:
    return "AM";

  case modeDIGU:
    return "DIG-U";

  case modeSPEC:
    return "SPEC";

  case modeDIGL:
    return "DIG-L";

  case modeSAM:
    return "SAM";

  case modeDRM:
    return "DRM";

  default:
    return "Mode_not_supported";  // wenn der Wert nicht gefunden wird
  }
}

static void vfo_save_bandstack(void) {
  BANDSTACK *bandstack = bandstack_get_bandstack(vfo[0].band);
  bandstack->current_entry = vfo[0].bandstack;
  BANDSTACK_ENTRY *entry = &bandstack->entry[vfo[0].bandstack];
  entry->frequency = vfo[0].frequency;
  entry->mode = vfo[0].mode;
  entry->filter = vfo[0].filter;
  entry->ctun = vfo[0].ctun;
  entry->ctun_frequency = vfo[0].ctun_frequency;
  entry->deviation = vfo[0].deviation;

  if (can_transmit) {
    entry->ctcss_enabled = transmitter->ctcss_enabled;
    entry->ctcss = transmitter->ctcss;
  }
}

static void modesettingsSaveState(void) {
  for (int i = 0; i < MODES; i++) {
    mode_settings[i].desc = getModeName(i); // save description of numeric mode number for better reading
    SetPropI1("modeset.%d.filter", i,                mode_settings[i].filter);
    SetPropI1("modeset.%d.cwPeak", i,                mode_settings[i].cwPeak);
    SetPropI1("modeset.%d.step", i,                  mode_settings[i].step);
    SetPropI1("modeset.%d.rit_step", i,              mode_settings[i].rit_step);
    SetPropI1("modeset.%d.nb", i,                    mode_settings[i].nb);
    SetPropF1("modeset.%d.nb_tau", i,                mode_settings[i].nb_tau);
    SetPropF1("modeset.%d.nb_hang", i,               mode_settings[i].nb_hang);
    SetPropF1("modeset.%d.nb_advtime", i,            mode_settings[i].nb_advtime);
    SetPropF1("modeset.%d.nb_thresh", i,             mode_settings[i].nb_thresh);
    SetPropI1("modeset.%d.nb2_mode", i,              mode_settings[i].nb2_mode);
    SetPropI1("modeset.%d.nr", i,                    mode_settings[i].nr);
    SetPropI1("modeset.%d.nr_agc", i,                mode_settings[i].nr_agc);
    SetPropI1("modeset.%d.nr2_gain_method", i,       mode_settings[i].nr2_gain_method);
    SetPropI1("modeset.%d.nr2_npe_method", i,        mode_settings[i].nr2_npe_method);
    SetPropI1("modeset.%d.nr2_ae", i,                mode_settings[i].nr2_ae);

    if ((GetWDSPVersion() % 100) > 26) {
      SetPropI1("modeset.%d.nr2_post", i,            mode_settings[i].nr2_post);
      SetPropI1("modeset.%d.nr2_post_taper", i,      mode_settings[i].nr2_post_taper);
      SetPropI1("modeset.%d.nr2_post_nlevel", i,     mode_settings[i].nr2_post_nlevel);
      SetPropI1("modeset.%d.nr2_post_factor", i,     mode_settings[i].nr2_post_factor);
      SetPropI1("modeset.%d.nr2_post_rate", i,       mode_settings[i].nr2_post_rate);
    }

    SetPropF1("modeset.%d.nr2_trained_threshold", i, mode_settings[i].nr2_trained_threshold);
    SetPropF1("modeset.%d.nr2_trained_t2", i,        mode_settings[i].nr2_trained_t2);
    SetPropS1("modeset.%d.mode_description", i,      mode_settings[i].desc);
    SetPropF1("modeset.%d.nr4_reduction_amount", i,  mode_settings[i].nr4_reduction_amount);
    SetPropF1("modeset.%d.nr4_smoothing_factor", i,  mode_settings[i].nr4_smoothing_factor);
    SetPropF1("modeset.%d.nr4_whitening_factor", i,  mode_settings[i].nr4_whitening_factor);
    SetPropF1("modeset.%d.nr4_noise_rescale", i,     mode_settings[i].nr4_noise_rescale);
    SetPropF1("modeset.%d.nr4_post_filter_threshold", i, mode_settings[i].nr4_post_filter_threshold);
    SetPropI1("modeset.%d.anf", i,                   mode_settings[i].anf);
    SetPropI1("modeset.%d.snb", i,                   mode_settings[i].snb);
    SetPropI1("modeset.%d.agc", i,                   mode_settings[i].agc);
    SetPropI1("modeset.%d.en_rxeq", i,               mode_settings[i].en_rxeq);
    SetPropI1("modeset.%d.en_txeq", i,               mode_settings[i].en_txeq);
    SetPropI1("modeset.%d.compressor", i,            mode_settings[i].compressor);
    SetPropF1("modeset.%d.compressor_level", i,      mode_settings[i].compressor_level);
    SetPropI1("modeset.%d.dexp", i,                  mode_settings[i].dexp);
    SetPropI1("modeset.%d.dexp_trigger", i,          mode_settings[i].dexp_trigger);
    SetPropF1("modeset.%d.dexp_tau", i,              mode_settings[i].dexp_tau);
    SetPropF1("modeset.%d.dexp_attack", i,           mode_settings[i].dexp_attack);
    SetPropF1("modeset.%d.dexp_release", i,          mode_settings[i].dexp_release);
    SetPropF1("modeset.%d.dexp_hold", i,             mode_settings[i].dexp_hold);
    SetPropI1("modeset.%d.dexp_exp", i,              mode_settings[i].dexp_exp);
    SetPropF1("modeset.%d.dexp_hyst", i,             mode_settings[i].dexp_hyst);
    SetPropI1("modeset.%d.dexp_filter", i,           mode_settings[i].dexp_filter);
    SetPropI1("modeset.%d.dexp_filter_low", i,       mode_settings[i].dexp_filter_low);
    SetPropI1("modeset.%d.dexp_filter_high", i,      mode_settings[i].dexp_filter_high);
    SetPropI1("modeset.%d.lev_enable", i,            mode_settings[i].lev_enable);
    SetPropF1("modeset.%d.lev_gain", i,              mode_settings[i].lev_gain);
    SetPropI1("modeset.%d.phrot_enable", i,          mode_settings[i].phrot_enable);
#if defined (__CPYMODE__)
    SetPropI1("modeset.%d.local_microphone", i,      mode_settings[i].local_microphone);
    SetPropS1("modeset.%d.microphone_name", i,       mode_settings[i].microphone_name);
    SetPropI1("modeset.%d.puresignal", i,            mode_settings[i].puresignal);
    SetPropI1("modeset.%d.use_rx_filter", i,         mode_settings[i].use_rx_filter);
#endif
    SetPropI1("modeset.%d.cfc", i,                   mode_settings[i].cfc);
    SetPropI1("modeset.%d.cfc_eq", i,                mode_settings[i].cfc_eq);

    for (int j = 0; j < 11; j++) {
      SetPropF2("modeset.%d.txeq.%d", i, j,          mode_settings[i].tx_eq_gain[j]);
      SetPropF2("modeset.%d.txeqfrq.%d", i, j,       mode_settings[i].tx_eq_freq[j]);
      SetPropF2("modeset.%d.rxeq.%d", i, j,          mode_settings[i].rx_eq_gain[j]);
      SetPropF2("modeset.%d.rxeqfrq.%d", i, j,       mode_settings[i].rx_eq_freq[j]);
      SetPropF2("modeset.%d.cfc_frq.%d", i, j,       mode_settings[i].cfc_freq[j]);
      SetPropF2("modeset.%d.cfc_lvl.%d", i, j,       mode_settings[i].cfc_lvl[j]);
      SetPropF2("modeset.%d.cfc_post.%d", i, j,      mode_settings[i].cfc_post[j]);
    }

#if defined (__EQ12__)

    for (int jj = 11; jj < 13; jj++) {
      SetPropF2("modeset.%d.txeq.%d", i, jj,          mode_settings[i].tx_eq_gain[jj]);
      SetPropF2("modeset.%d.txeqfrq.%d", i, jj,       mode_settings[i].tx_eq_freq[jj]);
      SetPropF2("modeset.%d.rxeq.%d", i, jj,          mode_settings[i].rx_eq_gain[jj]);
      SetPropF2("modeset.%d.rxeqfrq.%d", i, jj,       mode_settings[i].rx_eq_freq[jj]);
      SetPropF2("modeset.%d.cfc_frq.%d", i, jj,       mode_settings[i].cfc_freq[jj]);
      SetPropF2("modeset.%d.cfc_lvl.%d", i, jj,       mode_settings[i].cfc_lvl[jj]);
      SetPropF2("modeset.%d.cfc_post.%d", i, jj,      mode_settings[i].cfc_post[jj]);
    }

#endif
  }
}

static void modesettingsRestoreState(void) {
  for (int i = 0; i < MODES; i++) {
    //
    // set defaults that depend on  the mode: filter, agc, step
    //
    switch (i) {
    case modeLSB:
    case modeUSB:
    case modeDSB:
      mode_settings[i].agc      = AGC_MEDIUM;
      mode_settings[i].filter   = filterF5; //  2700 Hz
      mode_settings[i].step     = 100;
      mode_settings[i].rit_step = 100;
      break;

    case modeDIGL:
    case modeDIGU:
      mode_settings[i].agc      = AGC_FAST;
      mode_settings[i].filter   = filterF6; //  1000 Hz
      mode_settings[i].step     = 50;
      mode_settings[i].rit_step = 100;
      break;

    case modeCWL:
    case modeCWU:
      mode_settings[i].agc      = AGC_FAST;
      mode_settings[i].filter   = filterF4; //   500 Hz
      mode_settings[i].step     = 25;
      mode_settings[i].rit_step = 10;
      break;

    case modeAM:
    case modeSAM:
    case modeSPEC:
    case modeDRM:
    case modeFMN:  // nowhere used for FM
      mode_settings[i].agc      = AGC_MEDIUM;
      mode_settings[i].filter   = filterF3; //  8000 Hz
      mode_settings[i].step     = 100;
      mode_settings[i].rit_step = 100;
      break;
    }

    //
    // set defaults that are the same  for all modes
    //
    mode_settings[i].cwPeak = 0;
    mode_settings[i].nb = 0;
    mode_settings[i].nb_tau = 0.00001;
    mode_settings[i].nb_advtime = 0.00001;
    mode_settings[i].nb_hang = 0.00001;
    mode_settings[i].nb_thresh = 4.95;
    mode_settings[i].nb2_mode = 0;
    mode_settings[i].nr = 0;
    mode_settings[i].nr_agc = 0;
    mode_settings[i].nr2_gain_method = 0;
    mode_settings[i].nr2_npe_method = 0;
    mode_settings[i].nr2_ae = 1;

    if ((GetWDSPVersion() % 100) > 26) {
      mode_settings[i].nr2_post = 0;
      mode_settings[i].nr2_post_taper = 12;
      mode_settings[i].nr2_post_nlevel = 15;
      mode_settings[i].nr2_post_factor = 15;
      mode_settings[i].nr2_post_rate = 5;
    }

    mode_settings[i].nr2_trained_threshold = -0.5;
    mode_settings[i].nr2_trained_t2 = 0.2;
    mode_settings[i].nr4_reduction_amount = 10.0;
    mode_settings[i].nr4_smoothing_factor = 0.0;
    mode_settings[i].nr4_whitening_factor = 0.0;
    mode_settings[i].nr4_noise_rescale = 2.0;
    mode_settings[i].nr4_post_filter_threshold = 2.0;
    mode_settings[i].anf = 0;
    mode_settings[i].snb = 0;
    mode_settings[i].en_rxeq = 0;
    mode_settings[i].en_txeq = 0;
    mode_settings[i].compressor = 0;
    mode_settings[i].compressor_level = 4.0;
    mode_settings[i].dexp = 0;
    mode_settings[i].dexp_trigger = -25;
    mode_settings[i].dexp_tau = 0.01;
    mode_settings[i].dexp_attack = 0.025;
    mode_settings[i].dexp_release = 0.100;
    mode_settings[i].dexp_hold = 0.800;
    mode_settings[i].dexp_exp = 20;
    mode_settings[i].dexp_hyst = 0.75;
    mode_settings[i].dexp_filter = 0;
    mode_settings[i].dexp_filter_low = 1000;
    mode_settings[i].dexp_filter_high = 2000;
    mode_settings[i].cfc = 0;
    mode_settings[i].cfc_eq = 0;
    mode_settings[i].lev_gain = 0.0;
    mode_settings[i].lev_enable = 0;
    mode_settings[i].phrot_enable = 0;
#if defined (__CPYMODE__)
    mode_settings[i].local_microphone = 0;
    g_strlcpy(mode_settings[i].microphone_name, "NOMIC", sizeof(mode_settings[i].microphone_name));
    mode_settings[i].puresignal = 0;
    mode_settings[i].use_rx_filter = 0;
#endif

    for (int j = 0; j < 11; j++) {
      mode_settings[i].tx_eq_gain[j] = 0;
      mode_settings[i].rx_eq_gain[j] = 0;
      mode_settings[i].cfc_lvl   [j] = 0;
      mode_settings[i].cfc_post  [j] = 0;
    }

#if defined (__EQ12__)

    for (int jj = 11; jj < 13; jj++) {
      mode_settings[i].tx_eq_gain[jj] = 0;
      mode_settings[i].rx_eq_gain[jj] = 0;
      mode_settings[i].cfc_lvl   [jj] = 0;
      mode_settings[i].cfc_post  [jj] = 0;
    }

#endif

    switch (i) {
    case modeLSB:
    case modeUSB:
    case modeDSB:
      mode_settings[i].tx_eq_gain[ 0] =  0.0;
      mode_settings[i].tx_eq_gain[ 1] = -9.0;
      mode_settings[i].tx_eq_gain[ 2] = -6.0;
      mode_settings[i].tx_eq_gain[ 3] = -6.0;
      mode_settings[i].tx_eq_gain[ 4] = -9.0;
      mode_settings[i].tx_eq_gain[ 5] =  0.0;
      mode_settings[i].tx_eq_gain[ 6] =  0.0;
      mode_settings[i].tx_eq_gain[ 7] =  3.0;
      mode_settings[i].tx_eq_gain[ 8] =  3.0;
      mode_settings[i].tx_eq_gain[ 9] =  3.0;
      mode_settings[i].tx_eq_gain[10] =  3.0;
#if defined (__EQ12__)
      mode_settings[i].tx_eq_gain[11] =  3.0;
      mode_settings[i].tx_eq_gain[12] =  3.0;
#endif
      mode_settings[i].cfc_lvl   [ 0] =  3.0;
      mode_settings[i].cfc_lvl   [ 1] =  0.0;
      mode_settings[i].cfc_lvl   [ 2] =  0.0;
      mode_settings[i].cfc_lvl   [ 3] =  3.0;
      mode_settings[i].cfc_lvl   [ 4] =  3.0;
      mode_settings[i].cfc_lvl   [ 5] =  3.0;
      mode_settings[i].cfc_lvl   [ 6] =  6.0;
      mode_settings[i].cfc_lvl   [ 7] =  6.0;
      mode_settings[i].cfc_lvl   [ 8] =  6.0;
      mode_settings[i].cfc_lvl   [ 9] =  9.0;
      mode_settings[i].cfc_lvl   [10] =  9.0;
#if defined (__EQ12__)
      mode_settings[i].cfc_lvl   [11] =  0.0;
      mode_settings[i].cfc_lvl   [12] =  0.0;
#endif
      mode_settings[i].cfc_post  [ 0] = -9.0;
      break;
    }

    mode_settings[i].tx_eq_freq[0]  =     0.0;
    mode_settings[i].rx_eq_freq[0]  =     0.0;
    mode_settings[i].cfc_freq  [0]  =     0.0;
    mode_settings[i].tx_eq_freq[1]  =    70.0;
    mode_settings[i].rx_eq_freq[1]  =    50.0;
    mode_settings[i].cfc_freq  [1]  =    50.0;
    mode_settings[i].tx_eq_freq[2]  =   150.0;
    mode_settings[i].rx_eq_freq[2]  =   100.0;
    mode_settings[i].cfc_freq  [2]  =   150.0;
    mode_settings[i].tx_eq_freq[3]  =   300.0;
    mode_settings[i].rx_eq_freq[3]  =   200.0;
    mode_settings[i].cfc_freq  [3]  =   300.0;
    mode_settings[i].tx_eq_freq[4]  =   500.0;
    mode_settings[i].rx_eq_freq[4]  =   500.0;
    mode_settings[i].cfc_freq  [4]  =   500.0;
    mode_settings[i].tx_eq_freq[5]  =  1000.0;
    mode_settings[i].rx_eq_freq[5]  =  1000.0;
    mode_settings[i].cfc_freq  [5]  =   750.0;
    mode_settings[i].tx_eq_freq[6]  =  1500.0;
    mode_settings[i].rx_eq_freq[6]  =  1500.0;
    mode_settings[i].cfc_freq  [6]  =  1250.0;
    mode_settings[i].tx_eq_freq[7]  =  2000.0;
    mode_settings[i].rx_eq_freq[7]  =  2000.0;
    mode_settings[i].cfc_freq  [7]  =  1750.0;
    mode_settings[i].tx_eq_freq[8]  =  2500.0;
    mode_settings[i].rx_eq_freq[8]  =  2500.0;
    mode_settings[i].cfc_freq  [8]  =  2300.0;
    mode_settings[i].tx_eq_freq[9]  =  3000.0;
    mode_settings[i].rx_eq_freq[9]  =  3000.0;
    mode_settings[i].cfc_freq  [9]  =  2800.0;
    mode_settings[i].tx_eq_freq[10] =  3500.0;
    mode_settings[i].rx_eq_freq[10] =  5000.0;
    mode_settings[i].cfc_freq  [10] =  3100.0;
#if defined (__EQ12__)
    mode_settings[i].tx_eq_freq[11] =  6000.0;
    mode_settings[i].rx_eq_freq[11] =  6000.0;
    mode_settings[i].cfc_freq  [11] =  6000.0;
    mode_settings[i].tx_eq_freq[12] =  8000.0;
    mode_settings[i].rx_eq_freq[12] =  8000.0;
    mode_settings[i].cfc_freq  [12] =  8000.0;
#endif
    GetPropI1("modeset.%d.filter", i,                mode_settings[i].filter);
    GetPropI1("modeset.%d.cwPeak", i,                mode_settings[i].cwPeak);
    GetPropI1("modeset.%d.step", i,                  mode_settings[i].step);
    GetPropI1("modeset.%d.rit_step", i,              mode_settings[i].rit_step);
    GetPropI1("modeset.%d.nb", i,                    mode_settings[i].nb);
    GetPropF1("modeset.%d.nb_tau", i,                mode_settings[i].nb_tau);
    GetPropF1("modeset.%d.nb_hang", i,               mode_settings[i].nb_hang);
    GetPropF1("modeset.%d.nb_advtime", i,            mode_settings[i].nb_advtime);
    GetPropF1("modeset.%d.nb_thresh", i,             mode_settings[i].nb_thresh);
    GetPropI1("modeset.%d.nb2_mode", i,              mode_settings[i].nb2_mode);
    GetPropI1("modeset.%d.nr", i,                    mode_settings[i].nr);
    GetPropI1("modeset.%d.nr_agc", i,                mode_settings[i].nr_agc);
    GetPropI1("modeset.%d.nr2_gain_method", i,       mode_settings[i].nr2_gain_method);
    GetPropI1("modeset.%d.nr2_npe_method", i,        mode_settings[i].nr2_npe_method);
    GetPropI1("modeset.%d.nr2_ae", i,                mode_settings[i].nr2_ae);

    if ((GetWDSPVersion() % 100) > 26) {
      GetPropI1("modeset.%d.nr2_post", i,            mode_settings[i].nr2_post);
      GetPropI1("modeset.%d.nr2_post_taper", i,      mode_settings[i].nr2_post_taper);
      GetPropI1("modeset.%d.nr2_post_nlevel", i,     mode_settings[i].nr2_post_nlevel);
      GetPropI1("modeset.%d.nr2_post_factor", i,     mode_settings[i].nr2_post_factor);
      GetPropI1("modeset.%d.nr2_post_rate", i,       mode_settings[i].nr2_post_rate);
    }

    GetPropF1("modeset.%d.nr2_trained_threshold", i, mode_settings[i].nr2_trained_threshold);
    GetPropF1("modeset.%d.nr2_trained_t2", i,        mode_settings[i].nr2_trained_t2);
    GetPropF1("modeset.%d.nr4_reduction_amount", i,  mode_settings[i].nr4_reduction_amount);
    GetPropF1("modeset.%d.nr4_smoothing_factor", i,  mode_settings[i].nr4_smoothing_factor);
    GetPropF1("modeset.%d.nr4_whitening_factor", i,  mode_settings[i].nr4_whitening_factor);
    GetPropF1("modeset.%d.nr4_noise_rescale", i,     mode_settings[i].nr4_noise_rescale);
    GetPropF1("modeset.%d.nr4_post_filter_threshold", i, mode_settings[i].nr4_post_filter_threshold);
    GetPropI1("modeset.%d.anf", i,                   mode_settings[i].anf);
    GetPropI1("modeset.%d.snb", i,                   mode_settings[i].snb);
    GetPropI1("modeset.%d.agc", i,                   mode_settings[i].agc);
    GetPropI1("modeset.%d.en_rxeq", i,               mode_settings[i].en_rxeq);
    GetPropI1("modeset.%d.en_txeq", i,               mode_settings[i].en_txeq);
    GetPropI1("modeset.%d.compressor", i,            mode_settings[i].compressor);
    GetPropF1("modeset.%d.compressor_level", i,      mode_settings[i].compressor_level);
    GetPropI1("modeset.%d.dexp", i,                  mode_settings[i].dexp);
    GetPropI1("modeset.%d.dexp_trigger", i,          mode_settings[i].dexp_trigger);
    GetPropF1("modeset.%d.dexp_tau", i,              mode_settings[i].dexp_tau);
    GetPropF1("modeset.%d.dexp_attack", i,           mode_settings[i].dexp_attack);
    GetPropF1("modeset.%d.dexp_release", i,          mode_settings[i].dexp_release);
    GetPropF1("modeset.%d.dexp_hold", i,             mode_settings[i].dexp_hold);
    GetPropI1("modeset.%d.dexp_exp", i,              mode_settings[i].dexp_exp);
    GetPropF1("modeset.%d.dexp_hyst", i,             mode_settings[i].dexp_hyst);
    GetPropI1("modeset.%d.dexp_filter", i,           mode_settings[i].dexp_filter);
    GetPropI1("modeset.%d.dexp_filter_low", i,       mode_settings[i].dexp_filter_low);
    GetPropI1("modeset.%d.dexp_filter_high", i,      mode_settings[i].dexp_filter_high);
    GetPropI1("modeset.%d.cfc", i,                   mode_settings[i].cfc);
    GetPropI1("modeset.%d.cfc_eq", i,                mode_settings[i].cfc_eq);
    GetPropI1("modeset.%d.lev_enable", i,            mode_settings[i].lev_enable);
    GetPropF1("modeset.%d.lev_gain", i,              mode_settings[i].lev_gain);
    GetPropI1("modeset.%d.phrot_enable", i,          mode_settings[i].phrot_enable);
#if defined (__CPYMODE__)
    GetPropI1("modeset.%d.local_microphone", i,      mode_settings[i].local_microphone);
    GetPropS1("modeset.%d.microphone_name", i,       mode_settings[i].microphone_name);
    GetPropI1("modeset.%d.puresignal", i,            mode_settings[i].puresignal);
    GetPropI1("modeset.%d.use_rx_filter", i,         mode_settings[i].use_rx_filter);
#endif

    for (int j = 0; j < 11; j++) {
      GetPropF2("modeset.%d.txeq.%d", i, j,          mode_settings[i].tx_eq_gain[j]);
      GetPropF2("modeset.%d.txeqfrq.%d", i, j,       mode_settings[i].tx_eq_freq[j]);
      GetPropF2("modeset.%d.rxeq.%d", i, j,          mode_settings[i].rx_eq_gain[j]);
      GetPropF2("modeset.%d.rxeqfrq.%d", i, j,       mode_settings[i].rx_eq_freq[j]);
      GetPropF2("modeset.%d.cfc_frq.%d", i, j,       mode_settings[i].cfc_freq[j]);
      GetPropF2("modeset.%d.cfc_lvl.%d", i, j,       mode_settings[i].cfc_lvl[j]);
      GetPropF2("modeset.%d.cfc_post.%d", i, j,      mode_settings[i].cfc_post[j]);
    }

#if defined (__EQ12__)

    for (int jj = 11; jj < 13; jj++) {
      GetPropF2("modeset.%d.txeq.%d", i, jj,         mode_settings[i].tx_eq_gain[jj]);
      GetPropF2("modeset.%d.txeqfrq.%d", i, jj,      mode_settings[i].tx_eq_freq[jj]);
      GetPropF2("modeset.%d.rxeq.%d", i, jj,         mode_settings[i].rx_eq_gain[jj]);
      GetPropF2("modeset.%d.rxeqfrq.%d", i, jj,      mode_settings[i].rx_eq_freq[jj]);
      GetPropF2("modeset.%d.cfc_frq.%d", i, jj,      mode_settings[i].cfc_freq[jj]);
      GetPropF2("modeset.%d.cfc_lvl.%d", i, jj,      mode_settings[i].cfc_lvl[jj]);
      GetPropF2("modeset.%d.cfc_post.%d", i, jj,     mode_settings[i].cfc_post[jj]);
    }

#endif
  }
}

void copy_mode_settings(int mode) {
  //
  // If mode is USB or LSB or DSB, copy settings of that mode to USB and LSB and DSB
  // If mode is CWU or CWL       , copy settings of that mode to CWL and CWU
  // If mode is DIGU or DIGL     , copy settings of that mode to DIGL and DIGU
  //
  switch (mode) {
  case modeCWU:
  case modeCWL:
    mode_settings[modeCWU] = mode_settings[mode];
    mode_settings[modeCWL] = mode_settings[mode];
    break;

  case modeDIGU:
  case modeDIGL:
    mode_settings[modeDIGU] = mode_settings[mode];
    mode_settings[modeDIGL] = mode_settings[mode];
    break;

  case modeLSB:
  case modeUSB:
  case modeDSB:
    mode_settings[modeLSB] = mode_settings[mode];
    mode_settings[modeUSB] = mode_settings[mode];
    mode_settings[modeDSB] = mode_settings[mode];
    break;

  default:
    t_print("%s: Mode %s has no copy condition.\n", __func__, getModeName(mode));
  }
}

void vfo_save_state(void) {
  vfo_save_bandstack();

  for (int i = 0; i < MAX_VFOS; i++) {
    SetPropI1("vfo.%d.band", i,             vfo[i].band);
    SetPropI1("vfo.%d.frequency", i,        vfo[i].frequency);
    SetPropI1("vfo.%d.ctun", i,             vfo[i].ctun);
    SetPropI1("vfo.%d.ctun_frequency", i,   vfo[i].ctun_frequency);
    SetPropI1("vfo.%d.rit", i,              vfo[i].rit);
    SetPropI1("vfo.%d.rit_enabled", i,      vfo[i].rit_enabled);
    SetPropI1("vfo.%d.xit", i,              vfo[i].xit);
    SetPropI1("vfo.%d.xit_enabled", i,      vfo[i].xit_enabled);
    SetPropI1("vfo.%d.lo", i,               vfo[i].lo);
    SetPropI1("vfo.%d.offset", i,           vfo[i].offset);
    SetPropI1("vfo.%d.mode", i,             vfo[i].mode);
    SetPropI1("vfo.%d.filter", i,           vfo[i].filter);
    SetPropI1("vfo.%d.cw_apf", i,           vfo[i].cwAudioPeakFilter);
    SetPropI1("vfo.%d.deviation", i,        vfo[i].deviation);
    SetPropI1("vfo.%d.step", i,             vfo[i].step);
    SetPropI1("vfo.%d.rit_step", i,         vfo[i].rit_step);
  }

  modesettingsSaveState();
}

void vfo_restore_state(void) {
  for (int i = 0; i < MAX_VFOS; i++) {
    //
    // Set defaults, using a simple heuristics to get a
    // band that actually works on the current hardware.
    //
    if (radio->frequency_min <  430E6 && radio->frequency_max > 440E6) {
      vfo[i].band            = band430;
      vfo[i].bandstack       = 0;
      vfo[i].frequency       = 434010000;
    } else if (radio->frequency_min < 144E6 && radio->frequency_max > 146E6 ) {
      vfo[i].band            = band144;
      vfo[i].bandstack       = 0;
      vfo[i].frequency       = 145000000;
    } else {
      vfo[i].band            = band20;
      vfo[i].bandstack       = 0;
      vfo[i].frequency       = 14250000;
    }

    vfo[i].mode              = modeUSB;
    vfo[i].filter            = filterF5;
    vfo[i].cwAudioPeakFilter = 0;
    vfo[i].lo                = 0;
    vfo[i].offset            = 0;
    vfo[i].rit_enabled       = 0;
    vfo[i].rit               = 0;
    vfo[i].xit_enabled       = 0;
    vfo[i].xit               = 0;
    vfo[i].ctun              = 0;
    vfo[i].deviation         = 2500;
    vfo[i].step              = 100;
    vfo[i].rit_step          = 10;
    GetPropI1("vfo.%d.band", i,             vfo[i].band);
    GetPropI1("vfo.%d.frequency", i,        vfo[i].frequency);
    GetPropI1("vfo.%d.ctun", i,             vfo[i].ctun);
    GetPropI1("vfo.%d.ctun_frequency", i,   vfo[i].ctun_frequency);
    GetPropI1("vfo.%d.rit", i,              vfo[i].rit);
    GetPropI1("vfo.%d.rit_enabled", i,      vfo[i].rit_enabled);
    GetPropI1("vfo.%d.xit", i,              vfo[i].xit);
    GetPropI1("vfo.%d.xit_enabled", i,      vfo[i].xit_enabled);
    GetPropI1("vfo.%d.lo", i,               vfo[i].lo);
    GetPropI1("vfo.%d.offset", i,           vfo[i].offset);
    GetPropI1("vfo.%d.mode", i,             vfo[i].mode);
    GetPropI1("vfo.%d.filter", i,           vfo[i].filter);
    GetPropI1("vfo.%d.cw_apf", i,           vfo[i].cwAudioPeakFilter);
    GetPropI1("vfo.%d.deviation", i,        vfo[i].deviation);
    GetPropI1("vfo.%d.step", i,             vfo[i].step);
    GetPropI1("vfo.%d.rit_step", i,         vfo[i].rit_step);

    // Sanity check: if !ctun, offset must be zero
    if (!vfo[i].ctun) {
      vfo[i].offset = 0;
    }
  }

  modesettingsRestoreState();
}

static inline void vfo_adjust_band(int v, long long f) {
  //
  // The purpose of this function is be very quick
  // if the frequency has moved a little inside the
  // band, and invoke get_band_from_frequency() only
  // if the frequency is no longer in the band.
  // In this case, set the new band and bandstack.
  // Do not save any bandstack since this is an implicit
  // band change.
  //
  // Allow a small margin (25 kHz) to both sides of the band,
  // otherwise "leaving" an XVTR band (and thus switching
  // to the GEN band with zero LO frequency) will lead
  // to a frequency not supported by the radio. This margin
  // does not affect the "out of band" detection but it does
  // affect the OC settings.
  // As a result, you can "listen" to signals just beyond the band
  // boundaries without doing a band switching.
  //
  int   b = vfo[v].band;
  const BAND *band;
  const BANDSTACK *bandstack;
  band = band_get_band(b);

  if ((f + 25000LL) >= band->frequencyMin && (f - 25000LL) <= band->frequencyMax) {
    //
    // This "quick return" will occur in >99 percent of the cases
    //
    return;
  }

  //
  // Either we are in bandGen or bandAir, or
  // the VFO frequency has left the band, so
  // determine and set new band. This is involved
  // since one cycles through ALL bands.
  // bandAir is changed to bandGen if we move away from
  // one of the six WWV frequencies,
  // bandGen is changed to another band if we enter
  // the frequency range of the latter.
  //
  // NOTE: you loose the LO offset when moving > 25kHz
  //       out of a transverter band!
  //
  vfo[v].band = get_band_from_frequency(f);

  if (b != vfo[v].band) {
    t_print("%s: Band changed ! VFO id: %d, current band: %d, previous band: %d\n", __func__, (int) v,
            (int) vfo[v].band, (int) b);
#if defined (__HAVEATU__)

    if (can_transmit) {
      transmitter->is_tuned = 0;
      transmitter->stored_drive = radio_get_drive();

      if (!transmitter->is_tuned) {
        set_drive(1.0);
      }

      t_print("%s: stored drive level: %.1f\n", __func__, transmitter->stored_drive);
      t_print("%s: current drive level: %.1f\n", __func__, radio_get_drive());
    }

#endif
#if defined (__AUTOG__)

    if (can_transmit && autogain_enabled && (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2)) {
      autogain_is_adjusted = 0;
      t_print("%s: autogain_is_adjusted=%d\n", __func__, autogain_is_adjusted);
    }

#endif
  }

  bandstack = bandstack_get_bandstack(vfo[v].band);
  vfo[v].bandstack = bandstack->current_entry;
  radio_set_alex_antennas();
}

void vfo_xvtr_changed(void) {
  //
  // It may happen that the XVTR band is messed up in the sense
  // that the resulting radio frequency exceeds the limits.
  // In this case, change the VFO frequencies to get into
  // the allowed range, which is from LO + radio_min_frequency
  // to LO + radio_max_frequency
  //
  if (vfo[0].band >= BANDS) {
    const BAND *band = band_get_band(vfo[0].band);
    vfo[0].lo = band->frequencyLO + band->errorLO;

    if ((vfo[0].frequency > vfo[0].lo + radio->frequency_max)  ||
        (vfo[0].frequency < vfo[0].lo + radio->frequency_min)) {
      vfo[0].frequency = vfo[0].lo + (radio->frequency_min + radio->frequency_max) / 2;
      vfo[0].ctun = 0;
      vfo_adjust_band(0, vfo[0].frequency);
      rx_set_frequency(receiver[0], vfo[0].frequency);
    }
  }

  if (vfo[1].band >= BANDS) {
    const BAND *band = band_get_band(vfo[1].band);
    vfo[1].lo = band->frequencyLO + band->errorLO;

    if ((vfo[1].frequency > vfo[1].lo + radio->frequency_max)  ||
        (vfo[1].frequency < vfo[1].lo + radio->frequency_min)) {
      vfo[1].ctun = 0;
      vfo[1].frequency = vfo[1].lo + (radio->frequency_min + radio->frequency_max) / 2;
      vfo_adjust_band(1, vfo[1].frequency);

      if (receivers == 2) {
        rx_set_frequency(receiver[1], vfo[1].frequency);
      }
    }
  }

  schedule_general();        // for disablePA
  schedule_high_priority();  // for Frequencies
  g_idle_add(ext_vfo_update, NULL);
}

#if defined (__CPYMODE__)
static void audio_reload_input(void) {
  if (transmitter->local_microphone) {
    audio_close_input();
    t_print("%s: audio in closed\n", __func__);
  }

  if (transmitter->local_microphone) {
    if (audio_open_input() < 0) {
      transmitter->local_microphone = 0;
    }

    t_print("%s: audio in re-open\n", __func__);
  }
}

#endif

void vfo_apply_mode_settings(RECEIVER *rx) {
  int id, m;
  id = rx->id;
  m = vfo[id].mode;
  //
  // Apply VFO settings to VFO controlling the receiver
  //
  vfo[id].filter                = mode_settings[m].filter;
  vfo[id].cwAudioPeakFilter     = mode_settings[m].cwPeak;
  vfo[id].step                  = mode_settings[m].step;
  vfo[id].rit_step              = mode_settings[m].rit_step;
  //
  // Apply noise and EQ settings to the receiver
  //
  rx->nb                        = mode_settings[m].nb;
  rx->nb2_mode                  = mode_settings[m].nb2_mode;
  rx->nb_tau                    = mode_settings[m].nb_tau;
  rx->nb_hang                   = mode_settings[m].nb_hang;
  rx->nb_advtime                = mode_settings[m].nb_advtime;
  rx->nb_thresh                 = mode_settings[m].nb_thresh;
  //
  rx->nr                        = mode_settings[m].nr;
  rx->nr_agc                    = mode_settings[m].nr_agc;
  rx->nr2_gain_method           = mode_settings[m].nr2_gain_method;
  rx->nr2_npe_method            = mode_settings[m].nr2_npe_method;
  rx->nr2_ae                    = mode_settings[m].nr2_ae;

  if ((GetWDSPVersion() % 100) > 26) {
    rx->nr2_post                = mode_settings[m].nr2_post;
    rx->nr2_post_taper          = mode_settings[m].nr2_post_taper;
    rx->nr2_post_nlevel         = mode_settings[m].nr2_post_nlevel;
    rx->nr2_post_factor         = mode_settings[m].nr2_post_factor;
    rx->nr2_post_rate           = mode_settings[m].nr2_post_rate;
  }

  rx->nr2_trained_threshold     = mode_settings[m].nr2_trained_threshold;
  rx->nr2_trained_t2            = mode_settings[m].nr2_trained_t2;
  rx->nr4_reduction_amount      = mode_settings[m].nr4_reduction_amount;
  rx->nr4_smoothing_factor      = mode_settings[m].nr4_smoothing_factor;
  rx->nr4_whitening_factor      = mode_settings[m].nr4_whitening_factor;
  rx->nr4_noise_rescale         = mode_settings[m].nr4_noise_rescale;
  rx->nr4_post_filter_threshold = mode_settings[m].nr4_post_filter_threshold;
  rx->anf                       = mode_settings[m].anf;
  rx->snb                       = mode_settings[m].snb;
  rx->agc                       = mode_settings[m].agc;
  rx->eq_enable                 = mode_settings[m].en_rxeq;

  for (int i = 0; i < 11; i++) {
    rx->eq_gain[i] = mode_settings[m].rx_eq_gain[i];
    rx->eq_freq[i] = mode_settings[m].rx_eq_freq[i];
  }

#if defined (__EQ12__)

  for (int ii = 11; ii < 13; ii++) {
    rx->eq_gain[ii] = mode_settings[m].rx_eq_gain[ii];
    rx->eq_freq[ii] = mode_settings[m].rx_eq_freq[ii];
  }

#endif

  //
  // Transmitter-specific settings: TXEQ, CMRP, DEXP, CFC
  // only changed if this VFO controls the TX
  //
  if ((id == vfo_get_tx_vfo()) && can_transmit) {
    transmitter->eq_enable        = mode_settings[m].en_txeq;
    transmitter->compressor       = mode_settings[m].compressor;
    transmitter->compressor_level = mode_settings[m].compressor_level;
    transmitter->dexp             = mode_settings[m].dexp;
    transmitter->dexp_trigger     = mode_settings[m].dexp_trigger;
    transmitter->dexp_tau         = mode_settings[m].dexp_tau;
    transmitter->dexp_attack      = mode_settings[m].dexp_attack;
    transmitter->dexp_release     = mode_settings[m].dexp_release;
    transmitter->dexp_hold        = mode_settings[m].dexp_hold;
    transmitter->dexp_exp         = mode_settings[m].dexp_exp;
    transmitter->dexp_hyst        = mode_settings[m].dexp_hyst;
    transmitter->dexp_filter      = mode_settings[m].dexp_filter;
    transmitter->dexp_filter_low  = mode_settings[m].dexp_filter_low;
    transmitter->dexp_filter_high = mode_settings[m].dexp_filter_high;
    transmitter->cfc              = mode_settings[m].cfc;
    transmitter->cfc_eq           = mode_settings[m].cfc_eq;
#if defined (__CPYMODE__)
    g_mutex_lock(&copy_string_mutex);
    transmitter->local_microphone = mode_settings[m].local_microphone;
    g_strlcpy(transmitter->microphone_name, mode_settings[m].microphone_name, sizeof(transmitter->microphone_name));
    transmitter->puresignal       = mode_settings[m].puresignal;
    transmitter->use_rx_filter    = mode_settings[m].use_rx_filter;
    g_mutex_unlock(&copy_string_mutex);
#endif
    transmitter->lev_enable       = mode_settings[m].lev_enable;
    transmitter->lev_gain         = mode_settings[m].lev_gain;
    transmitter->phrot_enable     = mode_settings[m].phrot_enable;

    for (int i = 0; i < 11; i++) {
      transmitter->eq_gain[i]  = mode_settings[m].tx_eq_gain[i];
      transmitter->eq_freq[i]  = mode_settings[m].tx_eq_freq[i];
      transmitter->cfc_freq[i] = mode_settings[m].cfc_freq[i];
      transmitter->cfc_lvl[i]  = mode_settings[m].cfc_lvl[i];
      transmitter->cfc_post[i] = mode_settings[m].cfc_post[i];
    }

#if defined (__EQ12__)

    for (int ii = 11; ii < 13; ii++) {
      transmitter->eq_gain[ii]  = mode_settings[m].tx_eq_gain[ii];
      transmitter->eq_freq[ii]  = mode_settings[m].tx_eq_freq[ii];
      transmitter->cfc_freq[ii] = mode_settings[m].cfc_freq[ii];
      transmitter->cfc_lvl[ii]  = mode_settings[m].cfc_lvl[ii];
      transmitter->cfc_post[ii] = mode_settings[m].cfc_post[ii];
    }

#endif
#if defined (__CPYMODE__)
    audio_reload_input();
    tx_ps_onoff(transmitter, transmitter->puresignal);
    t_print("%s: state Pure Signal %d\n", __func__, transmitter->puresignal);
    tx_set_filter(transmitter);
    t_print("%s: state transmitter->use_rx_filter: %d\n", __func__, transmitter->use_rx_filter);
#endif
    tx_set_compressor(transmitter);
    tx_set_dexp(transmitter);
    // tx_set_equalizer(transmitter);
  }

  //
  // defer update_eq() until here since it also applies TX EQ settings
  //
  rx_set_agc(rx);
  update_noise();
  update_eq();
  g_idle_add(ext_vfo_update, NULL);

  if (can_transmit && display_sliders) {
    if (n_input_devices > 0) {
      for (int i = 0; i < n_input_devices; i++) {
        if (strcmp(transmitter->microphone_name, input_devices[i].name) == 0) {
          update_slider_local_mic_input(i);
        }
      }

      update_slider_local_mic_button();
    }
  }

  if (display_sliders) {
    update_slider_agc_btn();
  }
}

void vfo_band_changed(int id, int b) {
  const BAND *band;
  BANDSTACK *bandstack;
  int   oldmode = vfo[id].mode;

  //
  // If the band is not equal to the current band, look at the frequency of the
  // new bandstack entry.
  // Return quickly if the frequency is not compatible with the radio.
  // Note the LO frequency of the *new* band must be subtracted here
  //
  if (b != vfo[id].band) {
    t_print("%s: Band changed ! VFO id: %d, current band: %d, previous band: %d\n", __func__, id, b,
            (int) vfo[id].band);
#if defined (__HAVEATU__)

    if (can_transmit) {
      transmitter->is_tuned = 0;
      transmitter->stored_drive = radio_get_drive();

      if (!transmitter->is_tuned) {
        set_drive(1.0);
      }

      t_print("%s: stored drive level: %.1f\n", __func__, transmitter->stored_drive);
      t_print("%s: current drive level: %.1f\n", __func__, radio_get_drive());
    }

#endif
    band = band_get_band(b);
    bandstack = bandstack_get_bandstack(b);
    long long f = bandstack->entry[bandstack->current_entry].frequency;
    f -= (band->frequencyLO + band->errorLO);

    if (f < radio->frequency_min || f > radio->frequency_max) {
      return;
    }

#if defined (__AUTOG__)

    if (autogain_enabled && (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2)) {
      autogain_is_adjusted = 0;
      t_print("%s: autogain_is_adjusted=%d\n", __func__, autogain_is_adjusted);
    }

#endif
  }

  if (id == 0) {
    vfo_save_bandstack();
  }

  if (b == vfo[id].band) {
    // same band selected - step to the next band stack
    bandstack = bandstack_get_bandstack(b);
    vfo[id].bandstack++;

    if (vfo[id].bandstack >= bandstack->entries) {
      vfo[id].bandstack = 0;
    }

    if (id == 0) { bandstack->current_entry = vfo[id].bandstack; }
  } else {
    // new band - get band stack entry
    bandstack = bandstack_get_bandstack(b);
    vfo[id].bandstack = bandstack->current_entry;
  }

  band = band_get_band(b);
  const BANDSTACK_ENTRY *entry = &bandstack->entry[vfo[id].bandstack];
  vfo[id].band = b;
  vfo[id].frequency = entry->frequency;
  vfo[id].ctun = entry->ctun;
  vfo[id].ctun_frequency = entry->ctun_frequency;
  vfo[id].mode = entry->mode;
  vfo[id].lo = band->frequencyLO + band->errorLO;
  vfo[id].filter = entry->filter;
  vfo[id].deviation = entry->deviation;

  //
  // Paranoia:
  // There should be no out-of-band frequencies in the
  // bandstack. But better check twice.
  //
  if (vfo[id].ctun) {
    vfo_adjust_band(id, vfo[id].ctun_frequency);
  } else {
    vfo_adjust_band(id, vfo[id].frequency);
  }

  if (can_transmit) {
    transmitter->ctcss_enabled = entry->ctcss_enabled;
    transmitter->ctcss         = entry->ctcss;
    tx_set_ctcss(transmitter);
  }

  //
  // In the case of CTUN, the offset is re-calculated
  // during vfo_vfos_changed ==> rx_vfo_changed ==> rx_frequency_changed
  //
  if (id < receivers && oldmode != vfo[id].mode) {
    vfo_apply_mode_settings(receiver[id]);
  }

  vfo_vfos_changed();
}

void vfo_bandstack_changed(int b) {
  int id = active_receiver->id;
  int oldmode = vfo[id].mode;
  BANDSTACK *bandstack = bandstack_get_bandstack(vfo[id].band);

  if (id == 0) {
    vfo_save_bandstack();
    bandstack->current_entry = b;
  }

  vfo[id].bandstack = b;
  const BANDSTACK_ENTRY *entry = &bandstack->entry[b];
  vfo[id].frequency = entry->frequency;
  vfo[id].ctun_frequency = entry->ctun_frequency;
  vfo[id].ctun = entry->ctun;
  vfo[id].mode = entry->mode;
  vfo[id].filter = entry->filter;
  vfo[id].deviation = entry->deviation;

  if (vfo[id].ctun) {
    vfo_adjust_band(id, vfo[id].ctun_frequency);
  } else {
    vfo_adjust_band(id, vfo[id].frequency);
  }

  if (can_transmit) {
    transmitter->ctcss_enabled = entry->ctcss_enabled;
    transmitter->ctcss         = entry->ctcss;
    tx_set_ctcss(transmitter);
  }

  if (id < receivers && oldmode != vfo[id].mode) {
    vfo_apply_mode_settings(receiver[id]);
  }

  vfo_vfos_changed();
}

void vfo_mode_changed(int m) {
  int id = active_receiver->id;
  vfo_id_mode_changed(id, m);
}

void vfo_id_mode_changed(int id, int m) {
  vfo[id].mode = m;

  if (id < receivers) {
    vfo_apply_mode_settings(receiver[id]);
    rx_mode_changed(receiver[id]);
    rx_filter_changed(receiver[id]);
  }

  if (can_transmit) {
    tx_set_mode(transmitter, vfo_get_tx_mode());

    if (display_sliders && (m == modeDIGU || m == modeDIGL)) {
      update_slider_bbcompr_scale(FALSE);
      update_slider_bbcompr_button(FALSE);
      update_slider_lev_scale(FALSE);
      update_slider_lev_button(FALSE);
      update_slider_preamp_button(FALSE);
      // update_slider_snb_button(FALSE);
    } else if (display_sliders) {
      update_slider_bbcompr_scale(TRUE);
      update_slider_bbcompr_button(TRUE);
      update_slider_lev_scale(TRUE);
      update_slider_lev_button(TRUE);
      update_slider_preamp_button(TRUE);
      // update_slider_snb_button(TRUE);
    }
  }

  //
  // changing modes may change BFO frequency
  // and SDR need to be informed about "CW or not CW"
  //
  schedule_high_priority();       // update frequencies
  schedule_transmit_specific();   // update "CW" flag
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_deviation_changed(int dev) {
  int id = active_receiver->id;
  vfo_id_deviation_changed(id, dev);
}

void vfo_id_deviation_changed(int id, int dev) {
  vfo[id].deviation = dev;

  if (id < receivers) {
    rx_filter_changed(receiver[id]);
  }

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_filter_changed(int f) {
  int id = active_receiver->id;
  vfo_id_filter_changed(id, f);
}

void vfo_id_filter_changed(int id, int f) {
  // store changed filter in the mode settings
  if (id == 0) {
    int mode = vfo[id].mode;
    mode_settings[mode].filter = f;
    copy_mode_settings(mode);
  }

  vfo[id].filter = f;

  //
  // If f is either Var1 or Var2, then the changed filter edges
  // should also apply to the other receiver, if it is running.
  // Otherwise the filter and rx settings do not coincide.
  //
  if (f == filterVar1 || f == filterVar2 ) {
    for (int i = 0; i < receivers; i++) {
      rx_filter_changed(receiver[i]);
    }
  } else {
    if (id < receivers) {
      rx_filter_changed(receiver[id]);
    }
  }

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_vfos_changed(void) {
  //
  // Use this when there are large changes in the VFOs.
  // Apply the new data
  //
  rx_vfo_changed(receiver[0]);

  if (receivers == 2) {
    rx_vfo_changed(receiver[1]);
  }

  radio_tx_vfo_changed();
  radio_set_alex_antennas();
  //
  // radio_set_alex_antennas already scheduled a HighPrio and General packet,
  // but if the mode changed to/from CW, we also need a DUCspecific packet
  //
  schedule_transmit_specific();
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_a_to_b(void) {
  int oldmode = vfo[VFO_B].mode;
  vfo[VFO_B] = vfo[VFO_A];

  if (vfo[VFO_B].mode != oldmode && receivers == 2) {
    vfo_apply_mode_settings(receiver[1]);
  }

  vfo_vfos_changed();
}

void vfo_b_to_a(void) {
  int oldmode = vfo[VFO_A].mode;
  vfo[VFO_A] = vfo[VFO_B];

  if (vfo[VFO_A].mode != oldmode) {
    vfo_apply_mode_settings(receiver[0]);
  }

  vfo_vfos_changed();
}

void vfo_a_swap_b(void) {
  struct  _vfo temp = vfo[VFO_A];
  vfo[VFO_A]        = vfo[VFO_B];
  vfo[VFO_B]        = temp;

  if (vfo[VFO_A].mode != vfo[VFO_B].mode) {
    vfo_apply_mode_settings(receiver[0]);

    if (receivers == 2) {
      vfo_apply_mode_settings(receiver[1]);
    }
  }

  vfo_vfos_changed();
}

//
// here we collect various functions to
// get/set the VFO step size
//

int vfo_get_step_from_index(int index) {
  //
  // This function is used for some
  // extended CAT commands
  //
  if (index < 0) { index = 0; }

  if (index >= STEPS) { index = STEPS - 1; }

  return steps[index];
}

int vfo_get_stepindex(int id) {
  //
  // return index of current step size in steps[] array
  //
  int i;
  int step = vfo[id].step;

  for (i = 0; i < STEPS; i++) {
    if (steps[i] == step) { break; }
  }

  //
  // If step size is not found (this should not happen)
  // report some "convenient" index at the small end
  // (here: index 4 corresponding to 100 Hz)
  //
  if (i >= STEPS) { i = 4; }

  return i;
}

void vfo_set_step_from_index(int id, int index) {
  //
  // Set VFO step size to steps[index], with range checking
  //
  if (index < 0) { index = 0; }

  if (index >= STEPS) { index = STEPS - 1; }

  int step = steps[index];
  vfo[id].step = step;

  if (id == 0) {
    int mode = vfo[id].mode;
    mode_settings[mode].step = step;
    copy_mode_settings(mode);
  }
}

void vfo_step(int steps) {
  int id = active_receiver->id;
  vfo_id_step(id, steps);
}

void vfo_id_step(int id, int steps) {
  if (!locked) {
    long long delta;

    if (vfo[id].ctun) {
      // CTUN offset is limited by half the sample rate
      // if "id" does not refer to a running RX, take the active receiver
      const RECEIVER *myrx;

      if (id < receivers) {
        myrx = receiver[id];
      } else {
        myrx = active_receiver;
      }

      long long frequency = vfo[id].frequency;
      long long rx_low = ROUND(vfo[id].ctun_frequency, steps, vfo[id].step) + myrx->filter_low;
      long long rx_high = ROUND(vfo[id].ctun_frequency, steps, vfo[id].step) + myrx->filter_high;
      long long half = (long long)myrx->sample_rate / 2LL;
      long long min_freq = frequency - half;
      long long max_freq = frequency + half;

      if (rx_low <= min_freq) {
        return;
      } else if (rx_high >= max_freq) {
        return;
      }

      delta = vfo[id].ctun_frequency;
      vfo[id].ctun_frequency = ROUND(vfo[id].ctun_frequency, steps, vfo[id].step);
      delta = vfo[id].ctun_frequency - delta;
      vfo_adjust_band(id, vfo[id].ctun_frequency);
    } else {
      delta = vfo[id].frequency;
      vfo[id].frequency = ROUND(vfo[id].frequency, steps, vfo[id].step);
      delta = vfo[id].frequency - delta;
      vfo_adjust_band(id, vfo[id].frequency);
    }

    int sid = 1 - id;

    switch (sat_mode) {
    case SAT_NONE:
      break;

    case SAT_MODE:

      // A and B increment and decrement together
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency += delta;
        vfo_adjust_band(sid, vfo[sid].ctun_frequency);
      } else {
        vfo[sid].frequency      += delta;
        vfo_adjust_band(sid, vfo[sid].frequency);
      }

      if (sid < receivers) {
        rx_frequency_changed(receiver[sid]);
      }

      break;

    case RSAT_MODE:

      // A increments and B decrements or A decrments and B increments
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency -= delta;
        vfo_adjust_band(sid, vfo[sid].ctun_frequency);
      } else {
        vfo[sid].frequency      -= delta;
        vfo_adjust_band(sid, vfo[sid].frequency);
      }

      if (sid < receivers) {
        rx_frequency_changed(receiver[sid]);
      }

      break;
    }

    if (id < receivers) {
      rx_frequency_changed(receiver[id]);
    }

    g_idle_add(ext_vfo_update, NULL);
  }
}

void vfo_set_rit_step(int step) {
  int id = active_receiver->id;
  vfo_id_set_rit_step(id, step);
}

void vfo_id_set_rit_step(int id, int step) {
  vfo[id].rit_step = step;

  if (id == 0) {
    int mode = vfo[id].mode;
    mode_settings[mode].rit_step = step;
    copy_mode_settings(mode);
  }

  g_idle_add(ext_vfo_update, NULL);
}

//
// vfo_move (and vfo_id_move) are exclusively used
// to update the radio while dragging with the
// pointer device in the panadapter area. Therefore,
// the behaviour is different whether we use CTUN or not.
//
// In "normal" (non-CTUN) mode, we "drag the spectrum". This
// means, when dragging to the right the spectrum moves towards
// higher frequencies  this means the RX frequence is *decreased*.
//
// In "CTUN" mode, the spectrum is nailed to the display and we
// move the CTUN frequency. So dragging to the right
// *increases* the RX frequency.
//
void vfo_id_move(int id, long long hz, int round) {
  long long delta;

  if (!locked) {
    if (vfo[id].ctun) {
      // don't let ctun go beyond end of passband
      const RECEIVER *myrx;

      if (id < receivers) {
        myrx = receiver[id];
      } else {
        myrx = active_receiver;
      }

      long long frequency = vfo[id].frequency;
      long long rx_low = vfo[id].ctun_frequency + hz + myrx->filter_low;
      long long rx_high = vfo[id].ctun_frequency + hz + myrx->filter_high;
      long long half = (long long)myrx->sample_rate / 2LL;
      long long min_freq = frequency - half;
      long long max_freq = frequency + half;

      if (rx_low <= min_freq) {
        return;
      } else if (rx_high >= max_freq) {
        return;
      }

      delta = vfo[id].ctun_frequency;
      // *Add* the shift (hz) to the ctun frequency
      vfo[id].ctun_frequency = vfo[id].ctun_frequency + hz;

      if (round && (vfo[id].mode != modeCWL && vfo[id].mode != modeCWU)) {
        vfo[id].ctun_frequency = ROUND(vfo[id].ctun_frequency, 0, vfo[id].step);
      }

      delta = vfo[id].ctun_frequency - delta;
      vfo_adjust_band(id, vfo[id].ctun_frequency);
    } else {
      delta = vfo[id].frequency;
      // *Subtract* the shift (hz) from the VFO frequency
      vfo[id].frequency = vfo[id].frequency - hz;

      if (round && (vfo[id].mode != modeCWL && vfo[id].mode != modeCWU)) {
        vfo[id].frequency = ROUND(vfo[id].frequency, 0, vfo[id].step);
      }

      delta = vfo[id].frequency - delta;
      vfo_adjust_band(id, vfo[id].frequency);
    }

    int sid = 1 - id;

    switch (sat_mode) {
    case SAT_NONE:
      break;

    case SAT_MODE:

      // A and B increment and decrement together
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency += delta;
        vfo_adjust_band(sid, vfo[sid].ctun_frequency);
      } else {
        vfo[sid].frequency      += delta;
        vfo_adjust_band(sid, vfo[sid].frequency);
      }

      if (sid < receivers) {
        rx_frequency_changed(receiver[sid]);
      }

      break;

    case RSAT_MODE:

      // A increments and B decrements or A decrments and B increments
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency -= delta;
        vfo_adjust_band(sid, vfo[sid].ctun_frequency);
      } else {
        vfo[sid].frequency      -= delta;
        vfo_adjust_band(sid, vfo[sid].frequency);
      }

      if (sid < receivers) {
        rx_frequency_changed(receiver[sid]);
      }

      break;
    }

    if (id < receivers) {
      rx_frequency_changed(receiver[id]);
    }

    g_idle_add(ext_vfo_update, NULL);
  }
}

void vfo_move(long long hz, int round) {
  vfo_id_move(active_receiver->id, hz, round);
}

void vfo_move_to(long long hz) {
  int id = active_receiver->id;
  vfo_id_move_to(id, hz);
}

void vfo_id_move_to(int id, long long hz) {
  // hz is the offset from the min displayed frequency
  const RECEIVER *myrx;

  if (id < receivers) {
    myrx = receiver[id];
  } else {
    myrx = active_receiver;
  }

  // long long offset = hz;
  long long half = (long long)(myrx->sample_rate / 2);
  // long long f;
  long long f = (vfo[id].frequency - half) + hz + ((double)myrx->pan * myrx->hz_per_pixel);

  if (vfo[id].mode != modeCWL && vfo[id].mode != modeCWU) {
    // offset = ROUND(hz, 0, vfo[id].step);
    f = ROUND(f, 0, vfo[id].step);
  }

  // f = (vfo[id].frequency - half) + offset + ((double)myrx->pan * myrx->hz_per_pixel);

  if (!locked) {
    long long delta;

    if (vfo[id].ctun) {
      delta = vfo[id].ctun_frequency;
      vfo[id].ctun_frequency = f;

      if (vfo[id].mode == modeCWL) {
        vfo[id].ctun_frequency += cw_keyer_sidetone_frequency;
      } else if (vfo[id].mode == modeCWU) {
        vfo[id].ctun_frequency -= cw_keyer_sidetone_frequency;
      }

      delta = vfo[id].ctun_frequency - delta;
      vfo_adjust_band(id, vfo[id].ctun_frequency);
    } else {
      delta = vfo[id].frequency;
      vfo[id].frequency = f;

      if (vfo[id].mode == modeCWL) {
        vfo[id].frequency += cw_keyer_sidetone_frequency;
      } else if (vfo[id].mode == modeCWU) {
        vfo[id].frequency -= cw_keyer_sidetone_frequency;
      }

      delta = vfo[id].frequency - delta;
      vfo_adjust_band(id, vfo[id].frequency);
    }

    int sid = 1 - id;

    switch (sat_mode) {
    case SAT_NONE:
      break;

    case SAT_MODE:

      // A and B increment and decrement together
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency += delta;
        vfo_adjust_band(sid, vfo[id].ctun_frequency);
      } else {
        vfo[sid].frequency      += delta;
        vfo_adjust_band(sid, vfo[sid].frequency);
      }

      if (sid < receivers) {
        rx_frequency_changed(receiver[sid]);
      }

      break;

    case RSAT_MODE:

      // A increments and B decrements or A decrements and B increments
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency -= delta;
        vfo_adjust_band(sid, vfo[sid].ctun_frequency);
      } else {
        vfo[sid].frequency      -= delta;
        vfo_adjust_band(sid, vfo[sid].frequency);
      }

      if (sid < receivers) {
        rx_frequency_changed(receiver[sid]);
      }

      break;
    }

    if (id < receivers) {
      rx_vfo_changed(receiver[id]);
    }

    g_idle_add(ext_vfo_update, NULL);
  }
}

// cppcheck-suppress constParameterCallback
static gboolean vfo_scroll_event_cb (GtkWidget *widget, GdkEventScroll *event, gpointer data) {
  RECEIVER *rx = active_receiver;

  if (!rx) {
    t_print("%s: no active RX\n", __func__);
    return FALSE;
  }

  // return rx_scroll_event(widget, event, data);
  return rx_scroll_event(widget, event, rx);
}

static gboolean vfo_configure_event_cb (GtkWidget         *widget,
                                        GdkEventConfigure *event,
                                        gpointer           data) {
  if (vfo_surface) {
    cairo_surface_destroy (vfo_surface);
  }

  vfo_surface = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
                CAIRO_CONTENT_COLOR,
                gtk_widget_get_allocated_width (widget),
                gtk_widget_get_allocated_height (widget));
  /* Initialize the surface to black */
  cairo_t *cr;
  cr = cairo_create (vfo_surface);
  cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
  cairo_paint (cr);
  cairo_destroy(cr);
  g_idle_add(ext_vfo_update, NULL);
  return TRUE;
}

static gboolean vfo_draw_cb (GtkWidget *widget,
                             cairo_t   *cr,
                             gpointer   data) {
  cairo_set_source_surface (cr, vfo_surface, 0.0, 0.0);
  cairo_paint (cr);
  return FALSE;
}

//
// This function re-draws the VFO bar.
// Lot of elements are programmed, whose size and position
// is determined by the current vfo_layout
// Elements whose x-coordinate is zero are not drawn
//
void vfo_update(void) {
  char wid[6];

  if (!vfo_surface) { return; }

  int id = active_receiver->id;
  int m = vfo[id].mode;
  //
  // Determine the width of the band filter of the active receiver
  // and convert this to a 3-letter string
  //
  int rxhigh = active_receiver->filter_high;
  int rxlow = active_receiver->filter_low;
  int w = rxhigh - rxlow;

  //
  // CW: renormalize the filter edges to zero
  //
  if (m == modeCWL) {
    rxhigh += cw_keyer_sidetone_frequency;
    rxlow  += cw_keyer_sidetone_frequency;
  } else if (m == modeCWU) {
    rxhigh -= cw_keyer_sidetone_frequency;
    rxlow  -= cw_keyer_sidetone_frequency;
  }

  if (w < 995) {
    w = 10 * ((w + 5) / 10); // between 0 and 990
    snprintf(wid, sizeof(wid), "%3d", w);
  } else if (w < 9950) {
    w = 100 * ((w + 50) / 100); // between 1000 and 9900
    // print "2.7k" for a width of 2700
    snprintf(wid, sizeof(wid), "%d.%dk", w / 1000, (w % 1000) / 100);
  } else {
    // print "11k" for a width of 11000
    w = (w + 500) / 1000;
    snprintf(wid, sizeof(wid), "%2dk", w);
  }

  int f = vfo[id].filter;
  int txvfo = vfo_get_tx_vfo();
  const VFO_BAR_LAYOUT *vfl = &vfo_layout_list[vfo_layout];
  //
  // Filter used in active receiver
  //
  FILTER* band_filters = filters[m];
  const FILTER* band_filter = &band_filters[f];
  char temp_text[32];
  cairo_t *cr;
  cr = cairo_create (vfo_surface);
  cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
  cairo_paint (cr);
  cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

  // -----------------------------------------------------------
  //
  // Draw a picture showing the actual and default filter edges
  //
  // -----------------------------------------------------------
  if (m != modeFMN && vfl->filter_x != 0) {
    int def_low = band_filter->low;
    int def_high = band_filter->high;

    // switch high/low for lower-sideband-modes such
    // that the graphic display refers to audio frequencies.
    if (m == modeLSB || m == modeDIGL || m == modeCWL) {
      int swap;
      swap     = def_low;
      def_low  = def_high;
      def_high = swap;
      swap     = rxlow;
      rxlow    = rxhigh;
      rxhigh   = swap;
    }

    // t_print("H=%d DH=%d L=%d DL=%d\n", rxhigh, def_high, rxlow, def_low);
  }

  // -----------------------------------------------------------
  //
  // Draw a string specifying the mode, the filter width
  // For CW; add CW speed and side tone frequency
  //
  // -----------------------------------------------------------
  if (vfl->mode_x != 0) {
    switch (vfo[id].mode) {
    case modeFMN: {
      if (can_transmit ? transmitter->ctcss_enabled : 0) {
        snprintf(temp_text, 32, "%s %s C=%0.1f", mode_string[vfo[id].mode], wid,
                 ctcss_frequencies[transmitter->ctcss]);
      } else {
        snprintf(temp_text, 32, "%s %s", mode_string[vfo[id].mode], wid);
      }
    }
    break;

    case modeCWL:
    case modeCWU:
      if (vfo[id].cwAudioPeakFilter) {
        snprintf(temp_text, 32, "%s %sP %dwpm %dHz", mode_string[vfo[id].mode],
                 wid,
                 cw_keyer_speed,
                 cw_keyer_sidetone_frequency);
      } else {
        snprintf(temp_text, 32, "%s %s %d wpm %d Hz", mode_string[vfo[id].mode],
                 wid,
                 cw_keyer_speed,
                 cw_keyer_sidetone_frequency);
      }

      break;

    case modeDIGL:
    case modeDIGU:
      if (can_transmit && transmitter->use_rx_filter) {
        snprintf(temp_text, 32, "%s %s [RX=TX]", mode_string[vfo[id].mode], wid);
      } else {
        snprintf(temp_text, 32, "%s %s", mode_string[vfo[id].mode], wid);
      }

      break;

    default:
      snprintf(temp_text, 32, "%s %s", mode_string[vfo[id].mode], wid);
      break;
    }

    cairo_set_font_size(cr, vfl->size1 + 2);
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_move_to(cr, vfl->mode_x, vfl->mode_y);
    cairo_show_text(cr, temp_text);
  }

  // In what follows, we want to display the VFO frequency
  // on which we currently transmit a signal with red colour.
  // If it is out-of-band, we display "Out of band" in red.
  // Frequencies we are not transmitting on are displayed in green
  // (dimmed if the freq. does not belong to the active receiver).
  // Frequencies of VFO A and B
  long long af = vfo[0].ctun ? vfo[0].ctun_frequency : vfo[0].frequency;
  long long bf = vfo[1].ctun ? vfo[1].ctun_frequency : vfo[1].frequency;
#if 0

  //
  // DL1YCF:
  // There is no consensus whether the "VFO display frequency" should move if
  // RIT/XIT values are changed. My Kenwood TS590 does so, but some popular
  // other SDR software does not (which in my personal view is a bug, not a feature).
  //
  // The strongest argument to prefer the "TS590" behaviour is that during TX,
  // the frequency actually used for transmit should be displayed.
  // Then, to preserve symmetry, during RX the effective RX frequency
  // is also displayed.
  //
  // Adjust VFO_A frequency for RIT/XIT
  //
  if (radio_is_transmitting() && txvfo == 0) {
    if (vfo[0].xit_enabled) { af += vfo[0].xit; }
  } else {
    if (vfo[0].rit_enabled) { af += vfo[0].rit; }
  }

  //
  // Adjust VFO_B frequency for RIT/XIT
  //
  if (radio_is_transmitting() && txvfo == 1) {
    if (vfo[1].xit_enabled) { af += vfo[1].xit; }
  } else {
    if (vfo[1].rit_enabled) { bf += vfo[1].rit; }
  }

#endif
  int oob = 0;
  int f_m; // MHz part
  int f_k; // kHz part
  int f_h; // Hz  part

  if (can_transmit) { oob = transmitter->out_of_band; }

  // -----------------------------------------------------------
  //
  // Draw VFO A Dial.
  //
  // -----------------------------------------------------------
  if (vfl->vfo_a_x != 0) {
    cairo_move_to(cr, abs(vfl->vfo_a_x), vfl->vfo_a_y);

    if (txvfo == 0 && (radio_is_transmitting() || oob)) {
      cairo_set_source_rgba(cr, COLOUR_ALARM);
    } else if (vfo[0].entered_frequency[0]) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    } else if (id != 0) {
      cairo_set_source_rgba(cr, COLOUR_OK_WEAK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_OK);
    }

    f_m = af / 1000000LL;
    f_k = (af - 1000000LL * f_m) / 1000;
    f_h = (af - 1000000LL * f_m - 1000 * f_k);
    cairo_set_font_size(cr, vfl->size2);
    cairo_show_text(cr, "A:");
    cairo_set_font_size(cr, vfl->size3);

    if (txvfo == 0 && oob) {
      cairo_show_text(cr, "Out of band");
    } else if (vfo[0].entered_frequency[0]) {
      snprintf(temp_text, sizeof(temp_text), "%s", vfo[0].entered_frequency);
      cairo_show_text(cr, temp_text);
    } else {
      //
      // poor man's right alignment:
      // If the frequency is small, print some zeroes
      // with the background colour
      //
      cairo_save(cr);
      cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);

      if (f_m < 10) {
        cairo_show_text(cr, "0000");
      } else if (f_m < 100) {
        cairo_show_text(cr, "000");
      } else if (f_m < 1000) {
        cairo_show_text(cr, "00");
      } else if (f_m < 10000) {
        cairo_show_text(cr, "0");
      }

      cairo_restore(cr);
      snprintf(temp_text, 32, "%0d.%03d", f_m, f_k);
      cairo_show_text(cr, temp_text);
      cairo_set_font_size(cr, vfl->size2);
      snprintf(temp_text, 32, "%03d", f_h);
      cairo_show_text(cr, temp_text);
    }
  }

  // -----------------------------------------------------------
  //
  // Draw VFO B Dial.
  //
  // -----------------------------------------------------------
  if (vfl->vfo_b_x != 0) {
    cairo_move_to(cr, abs(vfl->vfo_b_x), abs(vfl->vfo_b_y));

    if (txvfo == 1 && (radio_is_transmitting() || oob)) {
      cairo_set_source_rgba(cr, COLOUR_ALARM);
    } else if (vfo[1].entered_frequency[0]) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    } else if (id != 1) {
      cairo_set_source_rgba(cr, COLOUR_OK_WEAK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_OK);
    }

    f_m = bf / 1000000LL;
    f_k = (bf - 1000000LL * f_m) / 1000;
    f_h = (bf - 1000000LL * f_m - 1000 * f_k);
    cairo_set_font_size(cr, vfl->size2);
    cairo_show_text(cr, "B:");
    cairo_set_font_size(cr, vfl->size3);

    if (txvfo == 0 && oob) {
      cairo_show_text(cr, "Out of band");
    } else if (vfo[1].entered_frequency[0]) {
      snprintf(temp_text, sizeof(temp_text), "%s", vfo[1].entered_frequency);
      cairo_show_text(cr, temp_text);
    } else {
      //
      // poor man's right alignment:
      // If the frequency is small, print some zeroes
      // with the background colour
      //
      cairo_save(cr);
      cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);

      if (f_m < 10) {
        cairo_show_text(cr, "0000");
      } else if (f_m < 100) {
        cairo_show_text(cr, "000");
      } else if (f_m < 1000) {
        cairo_show_text(cr, "00");
      } else if (f_m < 10000) {
        cairo_show_text(cr, "0");
      }

      cairo_restore(cr);
      snprintf(temp_text, 32, "%0d.%03d", f_m, f_k);
      cairo_show_text(cr, temp_text);
      cairo_set_font_size(cr, vfl->size2);
      snprintf(temp_text, 32, "%03d", f_h);
      cairo_show_text(cr, temp_text);
    }
  }

  //
  // Everything that follows uses font size 1
  //
  // cairo_set_font_size(cr, vfl->size1);
  cairo_set_font_size(cr, 14.0);

  // -----------------------------------------------------------
  //
  // Draw string indicating Zoom status
  //
  // -----------------------------------------------------------
  if (vfl->zoom_x != 0) {
    cairo_move_to(cr, vfl->zoom_x, vfl->zoom_y);

    if (active_receiver->zoom > 1) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    snprintf(temp_text, 32, "Zoom %d", active_receiver->zoom);
    cairo_show_text(cr, temp_text);
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating PS status
  //
  // -----------------------------------------------------------
  if ((protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) && can_transmit && vfl->ps_x != 0) {
    cairo_move_to(cr, vfl->ps_x, vfl->ps_y);

    if (transmitter->puresignal) {
      cairo_set_source_rgba(cr, COLOUR_OK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, "PS");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating RIT offset
  //
  // -----------------------------------------------------------
  if (vfl->rit_x != 0) {
    if (vfo[id].rit_enabled == 0) {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    } else {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    }

    snprintf(temp_text, 32, "RIT %lldHz", vfo[id].rit);
    cairo_move_to(cr, vfl->rit_x, vfl->rit_y);
    cairo_show_text(cr, temp_text);
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating XIT offset
  //
  // -----------------------------------------------------------
  if (can_transmit && vfl->xit_x != 0) {
    if (vfo[txvfo].xit_enabled == 0) {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    } else {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    }

    snprintf(temp_text, 32, "XIT %lldHz", vfo[txvfo].xit);
    cairo_move_to(cr, vfl->xit_x, vfl->xit_y);
    cairo_show_text(cr, temp_text);
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating NB status
  //
  // -----------------------------------------------------------
  if (vfl->nb_x != 0) {
    cairo_move_to(cr, vfl->nb_x, vfl->nb_y);

    switch (active_receiver->nb) {
    case 1:
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, "NB");
      break;

    case 2:
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, "NB2");
      break;

    default:
      cairo_set_source_rgba(cr, COLOUR_SHADE);
      cairo_show_text(cr, "NB");
      break;
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating NR status
  //
  // -----------------------------------------------------------
  if (vfl->nr_x != 0) {
    cairo_move_to(cr, vfl->nr_x, vfl->nr_y);

    switch (active_receiver->nr) {
    case 1:
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, "NR");
      break;

    case 2:
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, "NR2");
      break;

    case 3:
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, "NR3");
      break;

    case 4:
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, "NR4");
      break;

    default:
      cairo_set_source_rgba(cr, COLOUR_SHADE);
      cairo_show_text(cr, "NR");
      break;
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating ANF status
  //
  // -----------------------------------------------------------
  if (vfl->anf_x != 0) {
    cairo_move_to(cr, vfl->anf_x, vfl->anf_y);

    if (active_receiver->anf) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, "ANF");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating SNB status
  //
  // -----------------------------------------------------------
  if (vfl->snb_x != 0) {
    cairo_move_to(cr, vfl->snb_x, vfl->snb_y);

    if (active_receiver->snb) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, "SNB");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating DEXP status
  //
  // -----------------------------------------------------------
  if (vfl->dexp_x != 0 && can_transmit) {
    cairo_move_to(cr, vfl->dexp_x, vfl->dexp_y);

    if (transmitter->dexp) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, "DEXP");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating AGC status
  //
  // -----------------------------------------------------------
  if (vfl->agc_x != 0) {
    cairo_move_to(cr, vfl->agc_x, vfl->agc_y);

    switch (active_receiver->agc) {
    case AGC_OFF:
      cairo_set_source_rgba(cr, COLOUR_SHADE);
      cairo_show_text(cr, "AGC off");
      break;

    case AGC_LONG:
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, "AGC long");
      break;

    case AGC_SLOW:
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, "AGC slow");
      break;

    case AGC_MEDIUM:
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, "AGC med");
      break;

    case AGC_FAST:
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, "AGC fast");
      break;
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating compressor status
  //
  // -----------------------------------------------------------
  if (can_transmit && vfl->cmpr_x != 0) {
    cairo_move_to(cr, vfl->cmpr_x, vfl->cmpr_y);

    if (transmitter->cfc && transmitter->cfc_eq) {
      snprintf(temp_text, 32, "CFC %+d %+d", (int) transmitter->cfc_lvl[0], (int) transmitter->cfc_post[0]);
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    }

    if (transmitter->cfc && !transmitter->cfc_eq) {
      snprintf(temp_text, 32, "CFC PR %+d", (int) transmitter->cfc_lvl[0]);
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    }

    if (!transmitter->cfc && transmitter->cfc_eq) {
      snprintf(temp_text, 32, "CFC PO %+d", (int) transmitter->cfc_post[0]);
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    }

    if (!transmitter->cfc && !transmitter->cfc_eq) {
      snprintf(temp_text, 32, "CFC");
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, temp_text);
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating equalizer status
  //
  // -----------------------------------------------------------
  if (vfl->eq_x != 0) {
    cairo_move_to(cr, vfl->eq_x + 22, vfl->eq_y);

    if (active_receiver->eq_enable) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, "RxEQ");
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
      cairo_show_text(cr, "RxEQ");
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating DIVERSITY status
  //
  // -----------------------------------------------------------
  if (vfl->div_x != 0) {
    cairo_move_to(cr, vfl->div_x, vfl->div_y);

    if (can_transmit) {
      if (transmitter->compressor) {
        snprintf(temp_text, 32, "PROC %+d", (int) transmitter->compressor_level);
        cairo_set_source_rgba(cr, COLOUR_ATTN);
      }

      if (!transmitter->compressor) {
        snprintf(temp_text, 32, "PROC");
        cairo_set_source_rgba(cr, COLOUR_SHADE);
      }

      cairo_show_text(cr, temp_text);
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating VFO step size
  //
  // -----------------------------------------------------------
  if (vfl->step_x != 0) {
    int s;

    for (s = 0; s < STEPS; s++) {
      if (steps[s] == vfo[id].step) { break; }
    }

    if (s >= STEPS) { s = 0; }

    snprintf(temp_text, 32, "Step %s", step_labels[s]);
    cairo_move_to(cr, vfl->step_x, vfl->step_y);
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_show_text(cr, temp_text);
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating CTUN status
  //
  // -----------------------------------------------------------
  if (vfl->ctun_x != 0) {
    cairo_move_to(cr, vfl->ctun_x + 5, vfl->ctun_y);

    if (vfo[id].ctun) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, "CTUN");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating CAT status
  //
  // -----------------------------------------------------------
  if (vfl->cat_x != 0) {
    cairo_move_to(cr, vfl->cat_x, vfl->cat_y);

    if (cat_control > 0) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, "CAT");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating MUTE status
  //
  // -----------------------------------------------------------

  if (vfl->mute_x != 0) {
    cairo_move_to(cr, vfl->mute_x, vfl->mute_y);

    if (active_receiver->mute_radio) {
      cairo_set_source_rgba(cr, COLOUR_ALARM);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    snprintf(temp_text, 32, "MUTE");
    cairo_show_text(cr, temp_text);
  }

  // TX-EQ & Leveler & Tuning state
  if (can_transmit && vfl->eq_x != 0) {
    cairo_move_to(cr, vfl->base_x + 40, vfl->base_y);

    if (transmitter->eq_enable) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);

      if (transmitter->eq_gain[0] == 0.0) {
        snprintf(temp_text, 32, "TxEQ");
      } else {
        snprintf(temp_text, 32, "TxEQ %+d", (int) transmitter->eq_gain[0]);
      }
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
      snprintf(temp_text, 32, "TxEQ");
    }

    cairo_show_text(cr, temp_text);
    cairo_move_to(cr, vfl->base_x + 110, vfl->base_y);

    if (transmitter->lev_enable) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      snprintf(temp_text, 32, "LEV %+d", (int) transmitter->lev_gain);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
      snprintf(temp_text, 32, "LEV");
    }

    cairo_show_text(cr, temp_text);
    cairo_move_to(cr, vfl->base_x + 180, vfl->base_y);

    if (transmitter->phrot_enable) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      snprintf(temp_text, 32, "PH-ROT %+d", (int) transmitter->phrot_stage);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
      snprintf(temp_text, 32, "PH-ROT");
    }

    cairo_show_text(cr, temp_text);
#if defined (__AUTOG__)

    if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
      cairo_move_to(cr, vfl->base_x + 260, vfl->base_y + 20);

      if (autogain_enabled && autogain_is_adjusted) {
        cairo_set_source_rgba(cr, COLOUR_OK);
      } else if (autogain_enabled) {
        cairo_set_source_rgba(cr, COLOUR_ATTN);
      } else {
        cairo_set_source_rgba(cr, COLOUR_SHADE);
      }

      if (autogain_time_enabled) {
        snprintf(temp_text, 32, "AGT");
      } else {
        snprintf(temp_text, 32, "AG");
      }

      cairo_show_text(cr, temp_text);

      if (!have_radioberry1 &&  !have_radioberry2 && !have_radioberry3) {
        cairo_move_to(cr, vfl->base_x + 260, vfl->base_y + 35);

        if (hl2_cl1_input) {
          cairo_set_source_rgba(cr, COLOUR_OK);
        } else {
          cairo_set_source_rgba(cr, COLOUR_SHADE);
        }

        snprintf(temp_text, 32, "CL1");
        cairo_show_text(cr, temp_text);
      }
    }

#endif
#if defined (__HAVEATU__)
    cairo_move_to(cr, vfl->base_x + 260, vfl->base_y + 50);

    if (active_receiver->panadapter_autoscale_enabled) {
      cairo_set_source_rgba(cr, COLOUR_OK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    snprintf(temp_text, 32, "NFA");
    cairo_show_text(cr, temp_text);

    if (vfl->tuned_x != 0) {
      cairo_move_to(cr, vfl->tuned_x, vfl->tuned_y);

      if (transmitter->is_tuned) {
        cairo_set_source_rgba(cr, COLOUR_OK);
        snprintf(temp_text, 32, "TUNED");
      } else {
        cairo_set_source_rgba(cr, COLOUR_ALARM);
        snprintf(temp_text, 32, "TUNED");
      }

      cairo_show_text(cr, temp_text);
    }

#endif

    if (vfl->preamp_x != 0) {
      cairo_move_to(cr, vfl->preamp_x, vfl->preamp_y);

      if (transmitter->addgain_enable) {
        cairo_set_source_rgba(cr, COLOUR_ATTN);

        if (transmitter->addgain_gain > 0) {
          snprintf(temp_text, 32, "Mic PreAmp +%.0fdb", transmitter->addgain_gain);
        } else {
          snprintf(temp_text, 32, "Mic PreAmp");
        }
      } else {
        cairo_set_source_rgba(cr, COLOUR_SHADE);
        snprintf(temp_text, 32, "Mic PreAmp");
      }

      cairo_show_text(cr, temp_text);
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating VOX status
  //
  // -----------------------------------------------------------
  if (can_transmit && vfl->vox_x != 0) {
    cairo_move_to(cr, vfl->vox_x, vfl->vox_y);

    if (vox_enabled) {
      cairo_set_source_rgba(cr, COLOUR_OK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, "VOX");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating LOCK status
  //
  // -----------------------------------------------------------
  if (vfl->lock_x != 0) {
    cairo_move_to(cr, vfl->lock_x, vfl->lock_y);

    if (locked) {
      cairo_set_source_rgba(cr, COLOUR_ALARM);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, "Locked");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating MICGAIN status
  //
  // -----------------------------------------------------------
  if (can_transmit && vfl->mgain_x != 0) {
    cairo_move_to(cr, vfl->mgain_x, vfl->mgain_y);
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    snprintf(temp_text, 32, "MicG %+d", (int)transmitter->mic_gain);
    cairo_show_text(cr, temp_text);
  }

  if (can_transmit && device == DEVICE_HERMES_LITE2) {
    cairo_move_to(cr, vfl->split_x, 59);

    if (enable_hl2_atu_gateware) {
      cairo_set_source_rgba(cr, COLOUR_OK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, "ATU");
    cairo_move_to(cr, vfl->dexp_x, 59);

    if (pa_enabled) {
      cairo_set_source_rgba(cr, COLOUR_OK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, "PA");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating SPLIT status
  //
  // -----------------------------------------------------------
  if (vfl->split_x != 0) {
    cairo_move_to(cr, vfl->split_x, vfl->split_y);

    if (split) {
      cairo_set_source_rgba(cr, COLOUR_OK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    cairo_show_text(cr, "Split");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating SAT status
  //
  // -----------------------------------------------------------
  if (vfl->sat_x != 0) {
    cairo_move_to(cr, vfl->sat_x, vfl->sat_y);

    if (sat_mode != SAT_NONE) {
      cairo_set_source_rgba(cr, COLOUR_OK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    if (sat_mode == SAT_NONE || sat_mode == SAT_MODE) {
      cairo_show_text(cr, "SAT");
    } else {
      cairo_show_text(cr, "RSAT");
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating DUPLEX and CESSB status
  //
  // -----------------------------------------------------------
  if (can_transmit && vfl->dup_x != 0) {
    if (duplex) {
      cairo_set_source_rgba(cr, COLOUR_OK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    snprintf(temp_text, 32, "DUP");
    cairo_move_to(cr, vfl->dup_x, vfl->dup_y);
    cairo_show_text(cr, temp_text);
  }

  if (can_transmit && vfl->dup_x != 0) {
    if (transmitter->compressor && transmitter->cessb_enable && transmitter->compressor_level > 0) {
      cairo_set_source_rgba(cr, COLOUR_OK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_SHADE);
    }

    snprintf(temp_text, 32, "CESSB");
    cairo_move_to(cr, vfl->dup_x + 38, vfl->dup_y + 15);
    cairo_show_text(cr, temp_text);
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating multifunction encoder status
  //
  // -----------------------------------------------------------
  int multi = GetMultifunctionStatus();

  if (vfl->multifn_x != 0 && multi != 0) {
    if (multi == 1) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
    } else {
      cairo_set_source_rgba(cr, COLOUR_ALARM);
    }

    GetMultifunctionString(temp_text, 32);
    cairo_move_to(cr, vfl->multifn_x, vfl->multifn_y);
    cairo_show_text(cr, temp_text);
  }

  cairo_destroy (cr);
  gtk_widget_queue_draw (vfo_panel);
}

// cppcheck-suppress constParameterCallback
static gboolean vfo_press_event_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  int v;

  switch (event->button) {
  case GDK_BUTTON_PRIMARY:
    v = VFO_A;

    if (event->x >= abs(vfo_layout_list[vfo_layout].vfo_b_x)) { v = VFO_B; }

    g_idle_add(ext_start_vfo, GINT_TO_POINTER(v));
    break;

  case GDK_BUTTON_SECONDARY:
    // do not discriminate between A and B
    g_idle_add(ext_start_band, NULL);
    break;
  }

  return TRUE;
}

GtkWidget* vfo_init(int width, int height) {
  my_width = width;
  my_height = height;
  vfo_panel = gtk_drawing_area_new ();
  gtk_widget_set_size_request (vfo_panel, width, height);
  g_signal_connect (vfo_panel, "configure-event",
                    G_CALLBACK (vfo_configure_event_cb), NULL);
  g_signal_connect (vfo_panel, "draw",
                    G_CALLBACK (vfo_draw_cb), NULL);
  /* Event signals */
  g_signal_connect (vfo_panel, "button-press-event",
                    G_CALLBACK (vfo_press_event_cb), NULL);
  g_signal_connect(vfo_panel, "scroll_event",
                   G_CALLBACK(vfo_scroll_event_cb), NULL);
  gtk_widget_set_events (vfo_panel, gtk_widget_get_events (vfo_panel)
                         | GDK_BUTTON_PRESS_MASK
                         | GDK_SCROLL_MASK);
  return vfo_panel;
}

//
// Some utility functions to get characteristics of the current
// transmitter. These functions can be used even if there is no
// transmitter
//

int vfo_get_tx_vfo(void) {
  int txvfo = active_receiver->id;

  if (split) { txvfo = 1 - txvfo; }

  return txvfo;
}

int vfo_get_tx_mode(void) {
  int txvfo = active_receiver->id;

  if (split) { txvfo = 1 - txvfo; }

  return vfo[txvfo].mode;
}

long long vfo_get_tx_freq(void) {
  int txvfo = active_receiver->id;

  if (split) { txvfo = 1 - txvfo; }

  if (vfo[txvfo].ctun) {
    return  vfo[txvfo].ctun_frequency;
  } else {
    return vfo[txvfo].frequency;
  }
}

void vfo_xit_value(long long value ) {
  int id = vfo_get_tx_vfo();
  vfo[id].xit = value;
  vfo[id].xit_enabled = value ? 1 : 0;
  schedule_high_priority();
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_xit_toggle(void) {
  int id = vfo_get_tx_vfo();
  TOGGLE(vfo[id].xit_enabled);
  schedule_high_priority();
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_rit_toggle(int id) {
  TOGGLE(vfo[id].rit_enabled);

  if (id < receivers) {
    rx_frequency_changed(receiver[id]);
  }

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_rit_value(int id, long long value) {
  vfo[id].rit = value;
  vfo[id].rit_enabled = value ? 1 : 0;

  if (id < receivers) {
    rx_frequency_changed(receiver[id]);
  }

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_rit_onoff(int id, int enable) {
  vfo[id].rit_enabled = SET(enable);

  if (id < receivers) {
    rx_frequency_changed(receiver[id]);
  }

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_xit_onoff(int enable) {
  int id = vfo_get_tx_vfo();
  vfo[id].xit_enabled = SET(enable);
  schedule_high_priority();
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_xit_incr(int incr) {
  int id = vfo_get_tx_vfo();
  long long value = vfo[id].xit + incr;

  if (value < -9999) {
    value = -9999;
  } else if (value > 9999) {
    value = 9999;
  }

  vfo[id].xit = value;
  vfo[id].xit_enabled = (value != 0);
  schedule_high_priority();
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_rit_incr(int id, int incr) {
  long long value = vfo[id].rit + incr;

  if (value < -9999) {
    value = -9999;
  } else if (value > 9999) {
    value = 9999;
  }

  vfo[id].rit = value;
  vfo[id].rit_enabled = (value != 0);

  if (id < receivers) {
    rx_frequency_changed(receiver[id]);
  }

  g_idle_add(ext_vfo_update, NULL);
}

//
// Interface to set the frequency, including
// "long jumps", for which we may have to
// change the band. This is solely used for
//
// - FREQ MENU
// - MIDI or GPIO NumPad
// - CAT "set frequency" command
//
void vfo_set_frequency(int v, long long f) {
  //
  // Here we used to have call vfo_band_changed() for
  // frequency jumps from one band to another.
  // However, this involved saving/restoring
  // bandstacks, including setting the new mode
  // the new bandstack.
  // This is proably an un-intended side effect
  // if the frequency is changed, for example, by
  // a digi mode program via a CAT command.
  // Therefore, now e call vfo_adjust_band() which
  // sets the band but does save to or restore from
  // bandstacks.
  //
  vfo_adjust_band(v, f);

  if (v == VFO_A) { rx_set_frequency(receiver[0], f); }

  if (v == VFO_B) {
    //
    // If there is only one receiver, there is no RX running that
    // is controlled by VFO_B, so just update the frequency of the
    // VFO without telling WDSP about it.
    // If VFO_B controls a (running) receiver, do the "full job".
    //
    if (receivers == 2) {
      rx_set_frequency(receiver[1], f);
    } else {
      vfo[v].frequency = f;

      if (vfo[v].ctun) {
        vfo[v].ctun = 0;
        vfo[v].offset = 0;
        vfo[v].ctun_frequency = vfo[v].frequency;
      }
    }
  }

  g_idle_add(ext_vfo_update, NULL);
}

//
// Set CTUN state of a VFO
//
void vfo_ctun_update(int id, int state) {
  //
  // Note: if this VFO does not control a (running) receiver,
  //       rx_set_frequency is *not* called therefore
  //       we should update ctun_frequency and offset
  //
  if (vfo[id].ctun == state) { return; }  // no-op if no change

  vfo[id].ctun = state;

  if (vfo[id].ctun) {
    // CTUN turned OFF->ON
    vfo[id].ctun_frequency = vfo[id].frequency;
    vfo[id].offset = 0;
    vfo_adjust_band(id, vfo[id].ctun_frequency);

    if (id < receivers) {
      rx_set_frequency(receiver[id], vfo[id].ctun_frequency);
    }
  } else {
    // CTUN turned ON->OFF: keep frequency
    vfo[id].frequency = vfo[id].ctun_frequency;
    vfo[id].offset = 0;
    vfo_adjust_band(id, vfo[id].frequency);

    if (id < receivers) {
      rx_set_frequency(receiver[id], vfo[id].ctun_frequency);
    }
  }
}
