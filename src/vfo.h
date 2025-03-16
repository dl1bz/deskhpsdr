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

#ifndef _VFO_H
#define _VFO_H

#include "mode.h"

enum _vfo_enum {
  VFO_A = 0,
  VFO_B,
  MAX_VFOS
};

struct _vfo {
  //
  // Band data
  //
  int band;
  int bandstack;
  //
  // Frequency data,including CTUN, RIT, XIT
  //
  long long frequency;
  int ctun;
  long long ctun_frequency;

  int rit_enabled;
  int rit_step;
  long long rit;
  int xit_enabled;
  long long xit;

  long long lo;
  long long offset;

  //
  // Mode data and step size
  //
  int mode;
  int step;

  //
  // Filter data
  //
  int filter;
  int  deviation;         // only for FMN
  int cwAudioPeakFilter;  // only for CWU/CWL

  //
  // temporary storage for direct "NumPad" frequency entry
  //
  char entered_frequency[16];  // need 13, but rounded up to next multiple of 4

};

extern struct _vfo vfo[MAX_VFOS];

//
// Store settings on a per-mode basis for settings that
// are likely to be "bound" to a mode rather than to a band
// If a mode changes, these settings are restored to what
// has been effective the last time the "new" mode has
// been used.
//
//
struct _mode_settings {
  //
  // VFO setting
  //
  int    filter;                    // actual filter used
  int    cwPeak;                    // CW peak filter on/off
  int    rit_step;                  // RIT step size
  long long step;                   // VFO step size
  //
  // RX (noise, EQ, AGC mode) settings
  //
  int    nb;                        // Noise blanker (0..2)
  double nb_tau;                    // NB parameters
  double nb_hang;
  double nb_advtime;
  double nb_thresh;
  int    nb2_mode;
  int    nr;                        // Noise reduction (0..2 or 0..4)
  int    nr_agc;                    // NR parameters
  int    nr2_gain_method;
  int    nr2_npe_method;
  int    nr2_ae;
  double nr2_trained_threshold;
  double nr2_trained_t2;
#ifdef EXTNR
  double nr4_reduction_amount;      // NR4 parameters, only used if compiled with EXTNR
  double nr4_smoothing_factor;
  double nr4_whitening_factor;
  double nr4_noise_rescale;
  double nr4_post_filter_threshold;
#endif
  int anf;                          // Automatic notch filter
  int snb;                          // Spectral noise blanker
  int agc;                          // AGC characteristics (slow/medium/fast etc.)
  int en_rxeq;                      // RX equalizer on/off
  double rx_eq_freq[11];            // RX equalizer settings
  double rx_eq_gain[11];
  //
  // TX (EQ, CMPR, DEXP, CRC) settings
  //
  int en_txeq;                      // TX equalizer on/off
  double tx_eq_freq[11];            // TX equalizer settings
  double tx_eq_gain[11];
  int compressor;                   // TX compressor on/off
  double compressor_level;          // TX compressor level
#if defined (__LDESK__) && defined (__USELESS__)
  double mic_gain;                  // TX mic gain
#endif
  int dexp;                         // Downward Expander (DEXP) on/off
  int dexp_trigger;                 // DEXP trigger level (dB)
  double dexp_tau;                  // DEXP averaging time constant
  double dexp_attack;               // DEXP OpenGate width
  double dexp_release;              // DEXP CloseGate width
  double dexp_hold;                 // DEXP "gate open with no signal" time
  int dexp_exp;                     // DEXP expansion ration (dB)
  double dexp_hyst;                 // DEXP hysteresis ratio
  int dexp_filter;                  // DEXP side channel filter on/off
  int dexp_filter_low;              // DEXP side channel filter low-cut
  int dexp_filter_high;             // DEXP side channel filter high-cut
  int cfc;                          // Continuous Frequency Compressor (CFC) on/off
  int cfc_eq;                       // CFC post-equalizer on/off
  double lev_gain;                  // Leveler Gain
  int    lev_enable;                // Leveler on/off
  int    phrot_enable;              // Phase Rotator on/off
  double cfc_freq[11];              // CFC corner frequencies
  double cfc_lvl[11];               // CFC compression at corner frequency
  double cfc_post[11];              // CFC post-EQ gain at corner frequency
#if defined (__LDESK__) && defined (__CPYMODE__)
  int local_microphone;
  char microphone_name[128];
  int puresignal;
  int use_rx_filter;
#endif
};

extern struct _mode_settings mode_settings[];
extern void copy_mode_settings(int mode);
#if defined (__LDESK__) && defined (__CPYMODE__)
  extern GMutex copy_string_mutex;
#endif

#define STEPS 17
extern char *step_labels[];

//
// Global functions declared in vfo.h start with "vfo_"
//
extern GtkWidget* vfo_init(int width, int height);
extern int        vfo_get_stepindex(int id);
extern void       vfo_set_step_from_index(int id, int index);
extern int        vfo_get_step_from_index(int index);
extern void       vfo_step(int steps);
extern void       vfo_id_step(int id, int steps);
extern void       vfo_set_rit_step(int step);
extern void       vfo_id_set_rit_step(int id, int step);
extern void       vfo_move(long long hz, int round);
extern void       vfo_id_move(int id, long long hz, int round);
extern void       vfo_id_move_to(int id, long long hz);
extern void       vfo_move_to(long long hz);
extern void       vfo_update(void);
extern void       vfo_save_state(void);
extern void       vfo_restore_state(void);
extern void       vfo_band_changed(int id, int b);
extern void       vfo_bandstack_changed(int b);
extern void       vfo_id_mode_changed(int id, int m);
extern void       vfo_mode_changed(int m);
extern void       vfo_id_filter_changed(int id, int f);
extern void       vfo_filter_changed(int f);
extern void       vfo_id_deviation_changed(int id, int dev);
extern void       vfo_deviation_changed(int dev);
extern void       vfo_a_to_b(void);
extern void       vfo_b_to_a(void);
extern void       vfo_a_swap_b(void);
extern int        vfo_get_tx_vfo(void);
extern int        vfo_get_tx_mode(void);
extern long long  vfo_get_tx_freq(void);
extern void       vfo_xvtr_changed(void);
extern void       vfo_xit_toggle(void);
extern void       vfo_xit_incr(int incr);
extern void       vfo_xit_onoff(int enable);
extern void       vfo_xit_value(long long value);
extern void       vfo_rit_toggle(int rx);
extern void       vfo_rit_onoff(int rx, int enable);
extern void       vfo_rit_incr(int rx, int incr);
extern void       vfo_rit_value(int rx, long long value);
extern void       vfo_set_frequency(int vfo, long long f);
extern void       vfo_ctun_update(int id, int state);
extern void       vfo_apply_mode_settings(RECEIVER *rx);
extern void       vfo_vfos_changed(void);
extern void       vfo_num_pad(int val, int vfo);
extern const char* getModeName(int mode);

#endif
