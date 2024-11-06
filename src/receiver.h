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
#ifndef _RECEIVER_H
#define _RECEIVER_H

#include <gtk/gtk.h>
#ifdef PORTAUDIO
  #include <portaudio.h>
#endif
#ifdef ALSA
  #include <alsa/asoundlib.h>
#endif
#ifdef PULSEAUDIO
  #include <pulse/pulseaudio.h>
  #include <pulse/simple.h>
#endif

enum _audio_channel_enum {
  STEREO = 0,
  LEFT,
  RIGHT
};

typedef struct _receiver {
  int id;
  GMutex mutex;
  GMutex display_mutex;

  int adc;

  double volume;  // in dB

  int dsp_size;
  int buffer_size;
  int fft_size;
  int low_latency;

  int agc;
  double agc_gain;
  double agc_slope;
  double agc_hang_threshold;
  double agc_hang;
  double agc_thresh;
  int fps;
  int displaying;
  int audio_channel; // STEREO or LEFT or RIGHT
  int sample_rate;
  int pixels;
  int samples;
  int output_samples;
  double *iq_input_buffer;
  double *audio_output_buffer;
  int audio_index;
  float *pixel_samples;
  int display_panadapter;
  int display_waterfall;
  guint update_timer_id;
  int    smetermode;
  double meter;

  double hz_per_pixel;

  int dither;
  int random;
  int preamp;

  //
  // Encodings for "QRM fighters"
  //
  // nr = 0:         No noise reduction
  // nr = 1:         Variable-Leak LMS Algorithm, "NR", "ANR"
  // nr = 2:         Spectral Noise Reduction, "NR2", "AEMNR"
  // nr = 3:         non-standard extension to WDSP
  // nr = 4:         non-standard extension to WDSP
  //
  // nb = 0:         No noise blanker
  // nb = 1:         Preemptive Wideband Blanker, "NB", "ANB"
  // nb = 2:         Interpolating Wideband Blanker, "NB2", "NOB"
  //
  // anf= 0/1:       Automatic notch filter off/on
  // snb= 0/1:       Spectral noise blanker off/on
  //
  int nb;
  int nr;
  int anf;
  int snb;

  //
  // NR/NR2/ANF: position
  // 0: execute NR/NR2/ANF before AGC
  // 1: execute NR/NR2/ANF after AGC
  //
  int nr_agc;

  //
  // Noise reduction parameters for "NR2"
  // To be modified in the DSP menu.
  //
  //  Gain method: 0=GaussianSpeechLin, 1=GaussianSpeechLog, 2=GammaSpeech
  //  NPE  method: 0=OSMS, 1=MMSE
  //  AE         : Artifact elimination filter on(1)/off(0)
  //
  int nr2_gain_method;
  int nr2_npe_method;
  int nr2_ae;
  double nr2_trained_threshold;
  double nr2_trained_t2;

  //
  // Noise blanker parameters. These parameters have
  // similar meanings for NB/NB2 so we only take one set.
  // To be modified in the DSP menu. Note the "times"
  // are stored internally in seconds, while in the DSP menu they
  // are specified in milli-seconds.
  // The "NB value" nb_thresh, as obtained from the DSP menu,
  // is multiplied with 0.165 merely since this is done in other
  // SDR programs as well.
  // The comments indicate the names of the parameters in the Thetis menu
  // as well as the internal name used in Thetis.
  //
  double nb_tau;       // "Slew",                         NBTransision
  double nb_advtime;   // "Lead",                         NBLead
  double nb_hang;      // "Lag",                          NBLag
  double nb_thresh;    // "Threshold",                    NBThreshold
  int    nb2_mode;     // NB mode, only NB2
  //
  // nb2_mode = 0:  zero mode
  // nb2_mode = 1:  sample-hold
  // nb2_mode = 2:  mean-hold
  // nb2_mode = 3:  hold-sample
  // nb2_mode = 4:  interpolate

#ifdef EXTNR
  //
  // NR4 parameters. Only used if compiled with EXTNR
  //
  double nr4_reduction_amount;
  double nr4_smoothing_factor;
  double nr4_whitening_factor;
  double nr4_noise_rescale;
  double nr4_post_filter_threshold;
#endif

  int alex_antenna;
  int alex_attenuation;

  int filter_low;
  int filter_high;

  int width;
  int height;

  GtkWidget *panel;
  GtkWidget *panadapter;
  GtkWidget *waterfall;

  int panadapter_low;
  int panadapter_high;
  int panadapter_step;

  int waterfall_low;
  int waterfall_high;
  int waterfall_automatic;
  cairo_surface_t *panadapter_surface;
  GdkPixbuf *pixbuf;
  int local_audio;
  int mute_when_not_active;
  int audio_device;
  gchar audio_name[128];

#if defined(PORTAUDIO) && defined(PULSEAUDIO) && defined(ALSA)
  // this is only possible for "cppcheck" runs
  // declare all data without conflicts
  void *playstream;
  int local_audio_buffer_inpt;
  int local_audio_buffer_outpt;
  int local_audio_buffer_offset;
  void *local_audio_buffer;
  snd_pcm_t *playback_handle;
  snd_pcm_format_t local_audio_format;
#endif
#if defined(PORTAUDIO) && !defined(PULSEAUDIO) && !defined(ALSA)
  PaStream *playstream;
  volatile int local_audio_buffer_inpt;    // pointer in audio ring-buffer
  volatile int local_audio_buffer_outpt;   // pointer in audio ring-buffer
  float *local_audio_buffer;
#endif
#if !defined(PORTAUDIO) && !defined(PULSEAUDIO) && defined(ALSA)
  snd_pcm_t *playback_handle;
  snd_pcm_format_t local_audio_format;
  void *local_audio_buffer;        // different formats possible, so void*
  int local_audio_buffer_offset;
#endif
#if !defined(PORTAUDIO) && defined(PULSEAUDIO) && !defined(ALSA)
  pa_simple *playstream;
  float *local_audio_buffer;
  int local_audio_buffer_offset;
#endif

  GMutex local_audio_mutex;

  int squelch_enable;
  double squelch;

  int binaural;

  int deviation;

  long long waterfall_frequency;
  int waterfall_sample_rate;
  int waterfall_pan;
  int waterfall_zoom;

  int mute_radio;

  double *buffer;
  void *resampler;
  double *resample_buffer;
  int resample_buffer_size;

  int zoom;
  int pan;

  int x;
  int y;

  // two variables that implement the new
  // "mute first RX IQ samples after TX/RX transition"
  // feature that is relevant for HermesLite-II and STEMlab
  // (and possibly some other radios)
  //
  int txrxcount;
  int txrxmax;

  int display_gradient;
  int display_filled;
  int display_detector_mode;
  int display_average_mode;
  double display_average_time;

  //
  // Equalizer data
  //
  int  eq_enable;
  double eq_freq[11];
  double eq_gain[11];

} RECEIVER;

extern RECEIVER *rx_create_pure_signal_receiver(int id, int sample_rate, int pixels, int fps);
extern RECEIVER *rx_create_receiver(int id, int pixels, int width, int height);

extern gboolean rx_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer data);
extern gboolean rx_button_release_event(GtkWidget *widget, GdkEventButton *event, gpointer data);
extern gboolean rx_motion_notify_event(GtkWidget *widget, GdkEventMotion *event, gpointer data);
extern gboolean rx_scroll_event(GtkWidget *widget, const GdkEventScroll *event, gpointer data);

extern void   rx_add_iq_samples(RECEIVER *rx, double i_sample, double q_sample);
extern void   rx_add_div_iq_samples(RECEIVER *rx, double i0, double q0, double i1, double q1);

extern void   rx_change_sample_rate(RECEIVER *rx, int sample_rate);
extern void   rx_change_adc(const RECEIVER *rx);
extern void   rx_close(const RECEIVER *rx);
extern void   rx_create_analyzer(const RECEIVER *rx);
extern void   rx_filter_changed(RECEIVER *rx);
extern int    rx_get_pixels(RECEIVER *rx);
extern double rx_get_smeter(const RECEIVER *rx);
extern void   rx_frequency_changed(RECEIVER *rx);
extern void   rx_mode_changed(RECEIVER *rx);
extern void   rx_off(const RECEIVER *rx);
extern void   rx_on(const RECEIVER *rx);
extern void   rx_reconfigure(RECEIVER *rx, int height);
extern void   rx_restore_state(RECEIVER *rx);
extern void   rx_save_state(const RECEIVER *rx);

extern void   rx_set_active(RECEIVER *rx);
extern void   rx_set_af_binaural(const RECEIVER *rx);
extern void   rx_set_af_gain(const RECEIVER *rx);
extern void   rx_set_agc(RECEIVER *rx);
extern void   rx_set_analyzer(const RECEIVER *rx);
extern void   rx_set_average(const RECEIVER *rx);
extern void   rx_set_bandpass(const RECEIVER *rx);
extern void   rx_set_cw_peak(const RECEIVER *rx, int state, double freq);
extern void   rx_set_detector(const RECEIVER *rx);
extern void   rx_set_deviation(const RECEIVER *rx);
extern void   rx_set_displaying(RECEIVER *rx);
extern void   rx_set_equalizer(RECEIVER *rx);
extern void   rx_set_fft_latency(const RECEIVER *rx);
extern void   rx_set_fft_size(const RECEIVER *rx);
extern void   rx_set_filter(RECEIVER *rx);
extern void   rx_set_framerate(RECEIVER *rx);
extern void   rx_set_frequency(RECEIVER *rx, long long frequency);
extern void   rx_set_mode(const RECEIVER* rx);
extern void   rx_set_noise(const RECEIVER *rx);
extern void   rx_set_offset(const RECEIVER *rx, long long offset);
extern void   rx_set_squelch(const RECEIVER *rx);

extern void   rx_vfo_changed(RECEIVER *rx);
extern void   rx_update_zoom(RECEIVER *rx);

#ifdef CLIENT_SERVER
  extern void rx_create_remote(RECEIVER *rx);
  extern void rx_remote_update_display(RECEIVER *rx);
#endif

#endif
