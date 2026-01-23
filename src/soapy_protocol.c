/* Copyright (C)
* 2019 - John Melton, G0ORX/N6LYT
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>

#include <wdsp.h>   // only needed for the resampler

#include <SoapySDR/Constants.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Version.h>
#include <SoapySDR/Logger.h>

#include "band.h"
#include "channel.h"
#include "discovered.h"
#include "mode.h"
#include "filter.h"
#include "receiver.h"
#include "transmitter.h"
#include "radio.h"
#include "main.h"
#include "soapy_protocol.h"
#include "audio.h"
#include "vfo.h"
#include "ext.h"
#include "message.h"

#define MAX_CHANNELS 2
static SoapySDRStream *rx_stream[MAX_CHANNELS];
static SoapySDRStream *tx_stream;
static SoapySDRDevice *soapy_device;
static int max_samples;

static double bandwidth = 2000000.0;
const double rx_bw_lime = 12000000.0;
// const double rx_bw_sdrplay = 6000000.0;

/* Drop a few RX blocks after an RF LO retune (Lime) to suppress transients */
static volatile int soapy_rx_drop_blocks = 0;

static GThread *receive_thread_id = NULL;
static gpointer receive_thread(gpointer data);

/* ---------------- RX DC blocker (IIR) ----------------
 * Removes DC/near-DC spur (center "tune" line) in RX IQ.
 * Enabled only for Lime via have_lime trigger.
 */
typedef struct {
  double r;
  double x1_i, y1_i;
  double x1_q, y1_q;
  int inited;
} rx_dc_blocker_t;

static rx_dc_blocker_t rx_dc[2]; /* support up to 2 RX channels */

static inline void rx_dc_init(rx_dc_blocker_t *s, double r) {
  s->r = r;
  s->x1_i = s->y1_i = 0.0;
  s->x1_q = s->y1_q = 0.0;
  s->inited = 1;
}

static inline void rx_dc_block_doubles(rx_dc_blocker_t *s, double *iq, int n_complex) {
  /* iq: interleaved I/Q as double */
  const double r = s->r;
  double x1_i = s->x1_i, y1_i = s->y1_i;
  double x1_q = s->x1_q, y1_q = s->y1_q;

  for (int k = 0; k < n_complex; k++) {
    const double x_i = iq[2 * k + 0];
    const double x_q = iq[2 * k + 1];
    const double y_i = (x_i - x1_i) + r * y1_i;
    const double y_q = (x_q - x1_q) + r * y1_q;
    iq[2 * k + 0] = y_i;
    iq[2 * k + 1] = y_q;
    x1_i = x_i;
    y1_i = y_i;
    x1_q = x_q;
    y1_q = y_q;
  }

  s->x1_i = x1_i;
  s->y1_i = y1_i;
  s->x1_q = x1_q;
  s->y1_q = y1_q;
}

static inline __attribute__((unused))
void rx_dc_block_floats(rx_dc_blocker_t *s, float *iq, int n_complex) {
  /* iq: interleaved I/Q as float */
  const double r = s->r;
  double x1_i = s->x1_i, y1_i = s->y1_i;
  double x1_q = s->x1_q, y1_q = s->y1_q;

  for (int k = 0; k < n_complex; k++) {
    const double x_i = (double)iq[2 * k + 0];
    const double x_q = (double)iq[2 * k + 1];
    const double y_i = (x_i - x1_i) + r * y1_i;
    const double y_q = (x_q - x1_q) + r * y1_q;
    iq[2 * k + 0] = (float)y_i;
    iq[2 * k + 1] = (float)y_q;
    x1_i = x_i;
    y1_i = y_i;
    x1_q = x_q;
    y1_q = y_q;
  }

  s->x1_i = x1_i;
  s->y1_i = y1_i;
  s->x1_q = x1_q;
  s->y1_q = y1_q;
}

/*
 * LIME integration policy:
 * - Use Soapy's PPM correction for LimeSDR (avoid app-side scaling).
 * - Keep TX gain at 0 dB while receiving to minimize LO leakage.
 * - Kick TX auto-calibration by setting TX gain to ~30 dB before TX stream activation.
 * - Best-effort enable oversampling/decimation via OVERSAMPLING=32.
 */

static void soapy_lime_tx_gain_set(const size_t channel, const double gain_db) {
  if (!have_lime) {
    return;
  }

  if (soapy_device == NULL) {
    return;
  }

  (void)SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_TX, channel, gain_db);
}

static int soapy_lime_set_lo_bb(const int direction, const size_t channel, const double target_hz) {
  /*
   * LimeSDR tuning policy (sample-rate aware): keep RF LO fixed when possible,
   * move BB/NCO offset instead.
   *
   * With low hardware sample rates (e.g. 768 kS/s), the BB offset is limited
   * to approximately +/-Fs/2. Therefore the classical 1..5 MHz window cannot
   * be used; we derive a practical window from the current sample rate.
   *
   * Returns 0 on success; non-zero SoapySDR error code on failure.
   */
  if (soapy_device == NULL) {
    return SOAPY_SDR_TIMEOUT;
  }

  const double fs = SoapySDRDevice_getSampleRate(soapy_device, direction, channel);

  if (!(fs > 0.0)) {
    return SOAPY_SDR_TIMEOUT;
  }

  /* Keep away from DC and away from Nyquist edge */
  const double lo_window_min = fmax(20000.0, 0.06 * fs); /* >=20 kHz or 6% of Fs */
  const double lo_window_max = 0.40 * fs;               /* 40% of Fs */
  const double preferred_off = 0.26 * fs;               /* ~200 kHz @ 768 kS/s */
  const double max_abs_off = 0.45 * fs;                 /* hard guard (< Nyquist) */
  int did_set_rf = 0;
  int rc;
  double lo_freq = SoapySDRDevice_getFrequencyComponent(soapy_device, direction, channel, "RF");
  const int rf_valid = (lo_freq > 0.0);

  if (!rf_valid) {
    lo_freq = target_hz - preferred_off;
  }

  double off = target_hz - lo_freq;
  double off_abs = fabs(off);

  /* If BB offset would violate Nyquist, force a new LO immediately */
  if (off_abs > max_abs_off) {
    lo_freq = target_hz - ((off >= 0.0) ? preferred_off : -preferred_off);
    rc = SoapySDRDevice_setFrequencyComponent(soapy_device, direction, channel, "RF", lo_freq, NULL);

    if (rc != 0) {
      return rc;
    }

    return SoapySDRDevice_setFrequencyComponent(soapy_device, direction, channel, "BB", target_hz - lo_freq, NULL);
  }

  const int lo_ok = (off_abs >= lo_window_min && off_abs <= lo_window_max);

  /* If RF is not valid OR offset is outside window, choose and set a new LO */
  if (!rf_valid || !lo_ok) {
    lo_freq = target_hz - ((off >= 0.0) ? preferred_off : -preferred_off);
    rc = SoapySDRDevice_setFrequencyComponent(soapy_device, direction, channel, "RF", lo_freq, NULL);

    if (rc != 0) {
      return rc;
    }

    did_set_rf = 1;
  }

  /* Set BB so that RF + BB == target */
  rc = SoapySDRDevice_setFrequencyComponent(soapy_device, direction, channel, "BB", target_hz - lo_freq, NULL);

  /* If we changed RF LO on RX, drop a few subsequent RX blocks to avoid audible zzz */
  if (did_set_rf && direction == SOAPY_SDR_RX) {
    soapy_rx_drop_blocks = 4; /* start value: 4 blocks; tune 3..6 if needed */
  }

  if (did_set_rf) {
    t_print("LIME TUNE: RF+BB  dir=%s ch=%zu RF=%0.0f BB=%0.0f (fs=%0.0f)\n",
            direction == SOAPY_SDR_RX ? "RX" : "TX",
            channel, lo_freq, target_hz - lo_freq, fs);
  } else {
    t_print("LIME TUNE: BB-only dir=%s ch=%zu RF=%0.0f BB=%0.0f (fs=%0.0f)\n",
            direction == SOAPY_SDR_RX ? "RX" : "TX",
            channel, lo_freq, target_hz - lo_freq, fs);
  }

  return rc;
}

static gboolean running;

static int mic_samples = 0;
static int mic_sample_divisor = 1;

static int max_tx_samples;
static float *tx_output_buffer = NULL;
static int tx_output_buffer_index;

// cppcheck-suppress unusedFunction
SoapySDRDevice *get_soapy_device() {
  return soapy_device;
}

void soapy_protocol_set_mic_sample_rate(int rate) {
  mic_sample_divisor = rate / 48000;

  if (mic_sample_divisor < 1) {
    mic_sample_divisor = 1;
  }
}

void soapy_protocol_change_sample_rate(RECEIVER *rx) {
  //
  // rx->mutex already locked, so we can call this  only
  // if the radio is stopped -- we cannot change the resampler
  // while the receive thread is stuck in rx_add_iq_samples()
  //
  // We stick to the hardware sample rate and use the WDSP resampler
  //
  if (rx->sample_rate == soapy_radio_sample_rate) {
    if (rx->resample_buffer != NULL) {
      g_free(rx->resample_buffer);
      rx->resample_buffer = NULL;
      rx->resample_buffer_size = 0;
    }

    if (rx->resampler != NULL) {
      destroy_resample(rx->resampler);
      rx->resampler = NULL;
    }
  } else {
    if (rx->resample_buffer != NULL) {
      g_free(rx->resample_buffer);
      rx->resample_buffer = NULL;
    }

    if (rx->resampler != NULL) {
      destroy_resample(rx->resampler);
      rx->resampler = NULL;
    }

    rx->resample_buffer_size = 2 * max_samples / (soapy_radio_sample_rate / rx->sample_rate);
    rx->resample_buffer = g_new(double, rx->resample_buffer_size);
    rx->resampler = create_resample (1, max_samples, rx->buffer, rx->resample_buffer, soapy_radio_sample_rate,
                                     rx->sample_rate,
                                     0.0, 0, 1.0);
  }
}

void soapy_protocol_create_receiver(RECEIVER *rx) {
  int rc;
  mic_sample_divisor = rx->sample_rate / 48000;

  if (have_lime) {
    bandwidth = rx_bw_lime;
  }

  // if (have_sdrplay) {
  //   bandwidth = rx_bw_sdrplay;
  // }
  /*
   * LIME: for oversampling/decimation setups, request a wide RF bandwidth
   * (pre-decimation). Best effort - fall back to the configured bandwidth.
   */
  const double bw_req = have_lime || have_sdrplay ? bandwidth : (double)soapy_radio_sample_rate;
  t_print("%s: device=%p adc=%d setting bandwidth=%f\n", __FUNCTION__, soapy_device, rx->adc, bw_req);
  rc = SoapySDRDevice_setBandwidth(soapy_device, SOAPY_SDR_RX, rx->adc, bw_req);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setBandwidth(%f) failed: %s\n", __FUNCTION__, (double)bw_req, SoapySDR_errToStr(rc));
  }

  t_print("%s: setting samplerate=%f device=%p adc=%d mic_sample_divisor=%d\n", __FUNCTION__,
          (double)soapy_radio_sample_rate,
          soapy_device, rx->adc, mic_sample_divisor);
  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_RX, rx->adc, (double)soapy_radio_sample_rate);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setSampleRate(%f) failed: %s\n", __FUNCTION__, (double)soapy_radio_sample_rate,
            SoapySDR_errToStr(rc));
  }

  if (have_lime) {
    /* Best effort: enable oversampling/decimation in the Lime driver. */
    rc = SoapySDRDevice_writeSetting(soapy_device, "OVERSAMPLING", "32");

    if (rc != 0) {
      t_print("%s: setting OVERSAMPLING=32 failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }

    /* While receiving: keep TX muted to reduce LO leakage into RX. */
    soapy_lime_tx_gain_set((size_t)rx->adc, 0.0);
  }

  size_t channel = rx->adc;
#if defined(SOAPY_SDR_API_VERSION) && (SOAPY_SDR_API_VERSION < 0x00080000)
  t_print("%s: SoapySDRDevice_setupStream(version<0x00080000): channel=%ld\n", __FUNCTION__, channel);
  rc = SoapySDRDevice_setupStream(soapy_device, &rx_stream[channel], SOAPY_SDR_RX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setupStream (RX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "Soapy Setup RX Stream Failed");
  }

#else
  t_print("%s: SoapySDRDevice_setupStream(version>=0x00080000): channel=%ld\n", __FUNCTION__, channel);
  rx_stream[channel] = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_RX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rx_stream[channel] == NULL) {
    t_print("%s: SoapySDRDevice_setupStream (RX) failed (rx_stream is NULL)\n", __FUNCTION__);
    g_idle_add(fatal_error, "Soapy Setup RX Stream Failed");
  }

#endif
  t_print("%s: id=%d soapy_device=%p rx_stream=%p\n", __FUNCTION__, rx->id, soapy_device, rx_stream);
  max_samples = SoapySDRDevice_getStreamMTU(soapy_device, rx_stream[channel]);
  t_print("%s: max_samples=%d\n", __FUNCTION__, max_samples);

  if (max_samples > (2 * rx->fft_size)) {
    max_samples = 2 * rx->fft_size;
  }

  rx->buffer = g_new(double, max_samples * 2);

  if (rx->sample_rate == soapy_radio_sample_rate) {
    rx->resample_buffer = NULL;
    rx->resampler = NULL;
    rx->resample_buffer_size = 0;
  } else {
    rx->resample_buffer_size = 2 * max_samples / (soapy_radio_sample_rate / rx->sample_rate);
    rx->resample_buffer = g_new(double, rx->resample_buffer_size);
    rx->resampler = create_resample (1, max_samples, rx->buffer, rx->resample_buffer, soapy_radio_sample_rate,
                                     rx->sample_rate,
                                     0.0, 0, 1.0);
  }

  t_print("%s: max_samples=%d buffer=%p\n", __FUNCTION__, max_samples, rx->buffer);
}

void soapy_protocol_start_receiver(RECEIVER *rx) {
  int rc;
  t_print("%s: id=%d soapy_device=%p rx_stream=%p\n", __FUNCTION__, rx->id, soapy_device, rx_stream);
  size_t channel = rx->adc;

  if (have_lime) {
    /* While receiving: keep TX muted to reduce LO leakage. */
    soapy_lime_tx_gain_set(channel, 0.0);
  }

  double rate = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_RX, rx->adc);
  t_print("%s: rate=%f\n", __FUNCTION__, rate);
  t_print("%s: activate Stream\n", __FUNCTION__);
  rc = SoapySDRDevice_activateStream(soapy_device, rx_stream[channel], 0, 0LL, 0);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_activateStream failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "Soapy Start RX Stream failed");
  }

  t_print("%s: create receiver_thread\n", __FUNCTION__);
  receive_thread_id = g_thread_new( "soapy_rx", receive_thread, rx);
  t_print("%s: receiver_thread_id=%p\n", __FUNCTION__, receive_thread_id);
}

void soapy_protocol_create_transmitter(TRANSMITTER *tx) {
  int rc;
  t_print("%s: setting samplerate=%f\n", __FUNCTION__, (double)tx->iq_output_rate);
  rc = SoapySDRDevice_setSampleRate(soapy_device, SOAPY_SDR_TX, tx->dac, (double)tx->iq_output_rate);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setSampleRate(%f) failed: %s\n", __FUNCTION__, (double)tx->iq_output_rate,
            SoapySDR_errToStr(rc));
  }

  if (have_lime) {
    soapy_lime_tx_gain_set((size_t)tx->dac, 0.0);
  }

  size_t channel = tx->dac;
  t_print("%s: SoapySDRDevice_setupStream: channel=%ld\n", __FUNCTION__, channel);
#if defined(SOAPY_SDR_API_VERSION) && (SOAPY_SDR_API_VERSION < 0x00080000)
  rc = SoapySDRDevice_setupStream(soapy_device, &tx_stream, SOAPY_SDR_TX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setupStream (TX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "Soapy Setup TX Stream Failed");
  }

#else
  tx_stream = SoapySDRDevice_setupStream(soapy_device, SOAPY_SDR_TX, SOAPY_SDR_CF32, &channel, 1, NULL);

  if (tx_stream == NULL) {
    t_print("%s: SoapySDRDevice_setupStream (TX) failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "Soapy Setup TX Stream Failed");
  }

#endif
  max_tx_samples = SoapySDRDevice_getStreamMTU(soapy_device, tx_stream);

  if (max_tx_samples > (2 * tx->fft_size)) {
    max_tx_samples = 2 * tx->fft_size;
  }

  if (tx_output_buffer) {
    g_free(tx_output_buffer);
  }

  t_print("%s: max_tx_samples=%d\n", __FUNCTION__, max_tx_samples);
  // tx_output_buffer = (float *)malloc(max_tx_samples * sizeof(float) * 2);
  tx_output_buffer = g_new(float, 2 * max_tx_samples);
}

void soapy_protocol_start_transmitter(TRANSMITTER *tx) {
  int rc;

  if (have_lime) {
    /*
     * LIME: kick LMS auto-calibration at TX start.
     * Use a moderate drive level before activating the TX stream.
     */
    soapy_lime_tx_gain_set((size_t)tx->dac, 30.0);
  }

  double rate = SoapySDRDevice_getSampleRate(soapy_device, SOAPY_SDR_TX, tx->dac);
  t_print("soapy_protocol_start_transmitter: activateStream rate=%f\n", rate);
  rc = SoapySDRDevice_activateStream(soapy_device, tx_stream, 0, 0LL, 0);

  if (rc != 0) {
    t_print("soapy_protocol_start_transmitter: SoapySDRDevice_activateStream failed: %s\n", SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "Soapy Start TX Stream Failed");
    return;
  }

  if (have_lime) {
    /* After autocal kick, force nominal drive (old_gain may have been 0 due to RX muting). */
    soapy_lime_tx_gain_set((size_t)tx->dac, (double)tx->drive);
  }
}

void soapy_protocol_stop_receiver(const RECEIVER *rx) {
  // argument rx unused
  running = FALSE;

  if (receive_thread_id) {
    g_thread_join(receive_thread_id);
    receive_thread_id = NULL;
  }
}

// cppcheck-suppress unusedFunction
void soapy_protocol_stop_transmitter(TRANSMITTER *tx) {
  int rc;
  t_print("soapy_protocol_stop_transmitter: deactivateStream\n");
  // argument tx unused
  rc = SoapySDRDevice_deactivateStream(soapy_device, tx_stream, 0, 0LL);

  if (rc != 0) {
    t_print("soapy_protocol_stop_transmitter: SoapySDRDevice_deactivateStream failed: %s\n", SoapySDR_errToStr(rc));
    g_idle_add(fatal_error, "Soapy Stop TX Stream Failed");
  }

  if (have_lime) {
    /* Back to RX: keep TX muted to reduce LO leakage. */
    soapy_lime_tx_gain_set((size_t)tx->dac, 0.0);
  }
}

void soapy_protocol_init(gboolean hf) {
  SoapySDRKwargs args = {};
  char temp[32];
  SoapySDR_setLogLevel(SOAPY_SDR_TRACE);
  t_print("%s: hf=%d driver=%s\n", __FUNCTION__, hf, radio->name);
  // initialize the radio
  SoapySDRKwargs_set(&args, "driver", radio->name);

  if (strcmp(radio->name, "rtlsdr") == 0) {
    snprintf(temp, sizeof(temp), "%d", radio->info.soapy.rtlsdr_count);
    SoapySDRKwargs_set(&args, "rtl", temp);

    if (hf) {
      SoapySDRKwargs_set(&args, "direct_samp", "2");
    } else {
      SoapySDRKwargs_set(&args, "direct_samp", "0");
    }
  } else if (strcmp(radio->name, "sdrplay") == 0) {
    snprintf(temp, sizeof(temp), "SDRplay Dev%d", radio->info.soapy.sdrplay_count);
    t_print("%s: label=%s\n", __FUNCTION__, temp);
    SoapySDRKwargs_set(&args, "label", temp);
  }

  soapy_device = SoapySDRDevice_make(&args);

  if (soapy_device == NULL) {
    t_print("%s: SoapySDRDevice_make failed: %s\n", __FUNCTION__, SoapySDRDevice_lastError());
    g_idle_add(fatal_error, "Soapy Make Device Failed");
  }

  SoapySDRKwargs_clear(&args);
  t_print("%s: soapy_device=%p\n", __FUNCTION__, soapy_device);

  if (can_transmit) {
    if (transmitter->local_microphone) {
      if (audio_open_input() != 0) {
        t_print("%s: audio_open_input failed\n", __FUNCTION__);
        transmitter->local_microphone = 0;
      }
    }
  }
}

static void *receive_thread(void *arg) {
  //
  //  Since no mic samples arrive in SOAPY, we must use
  //  the incoming RX samples as a "heart beat" for the
  //  transmitter.
  //
  double isample;
  double qsample;
  int flags = 0;
  long long timeNs = 0;
  long timeoutUs = 100000L;
  int i;
  RECEIVER *rx = (RECEIVER *)arg;
  float *buffer = g_new(float, max_samples * 2);
  void *buffs[] = {buffer};
  float fsample;
  running = TRUE;
  t_print("soapy_protocol: receive_thread\n");
  size_t channel = rx->adc;
  const int is_rx1 = (channel == 0);

  if (have_lime) {
    const int idx = (channel < 2) ? (int)channel : 0;

    if (!rx_dc[idx].inited) {
      /* For Fs=768k: r=0.998 is a good start (strong DC removal, minimal impact) */
      rx_dc_init(&rx_dc[idx], 0.998);
    }
  }

  while (running) {
    int elements = SoapySDRDevice_readStream(soapy_device, rx_stream[channel], buffs, max_samples, &flags, &timeNs,
                   timeoutUs);

    //t_print("soapy_protocol_receive_thread: SoapySDRDevice_readStream failed: max_samples=%d read=%d\n",max_samples,elements);
    if (elements < 0) {
      continue;
    }

    for (i = 0; i < elements; i++) {
      rx->buffer[i * 2] = (double)buffer[i * 2];
      rx->buffer[(i * 2) + 1] = (double)buffer[(i * 2) + 1];
    }

    if (rx->resampler != NULL) {
      int samples = xresample(rx->resampler);

      /* Lime RX DC blocker on resampler output (double interleaved) */
      if (have_lime) {
        const int idx = (channel < 2) ? (int)channel : 0;
        rx_dc_block_doubles(&rx_dc[idx], rx->resample_buffer, samples);
      }

      /* If we are dropping due to a recent LO retune: keep TX heartbeat, but do NOT feed WDSP */
      if (have_lime && soapy_rx_drop_blocks > 0) {
        soapy_rx_drop_blocks--;

        if (can_transmit) {
          mic_samples += samples;

          while (mic_samples >= mic_sample_divisor) {
            tx_add_mic_sample(transmitter, 0);
            mic_samples -= mic_sample_divisor;
          }
        }

        continue;
      }

      for (i = 0; i < samples; i++) {
        isample = rx->resample_buffer[i * 2];
        qsample = rx->resample_buffer[(i * 2) + 1];

        if (soapy_iqswap) {
          rx_add_iq_samples(rx, qsample, isample);
        } else {
          rx_add_iq_samples(rx, isample, qsample);
        }

        if (is_rx1) {
          mic_samples++;

          if (mic_samples >= mic_sample_divisor) { // reduce to 48000
            mic_samples = 0;

            if (can_transmit && transmitter != NULL) {
              fsample = transmitter->local_microphone ? audio_get_next_mic_sample() : 0.0F;
              tx_add_mic_sample(transmitter, fsample);
            }
          }
        }
      }
    } else {
      /* Lime RX DC blocker on direct stream buffer (float interleaved) */
      if (have_lime) {
        const int idx = (channel < 2) ? (int)channel : 0;
        rx_dc_block_doubles(&rx_dc[idx], rx->buffer, elements);
      }

      /* Drop path without resampler: keep TX heartbeat, skip WDSP */
      if (have_lime && soapy_rx_drop_blocks > 0) {
        soapy_rx_drop_blocks--;

        if (can_transmit) {
          mic_samples += elements;

          while (mic_samples >= mic_sample_divisor) {
            tx_add_mic_sample(transmitter, 0);
            mic_samples -= mic_sample_divisor;
          }
        }

        continue;
      }

      for (i = 0; i < elements; i++) {
        isample = rx->buffer[i * 2];
        qsample = rx->buffer[(i * 2) + 1];

        if (soapy_iqswap) {
          rx_add_iq_samples(rx, qsample, isample);
        } else {
          rx_add_iq_samples(rx, isample, qsample);
        }

        if (is_rx1) {
          mic_samples++;

          if (mic_samples >= mic_sample_divisor) { // reduce to 48000
            mic_samples = 0;

            if (can_transmit && transmitter != NULL) {
              fsample = transmitter->local_microphone ? audio_get_next_mic_sample() : 0.0F;
              tx_add_mic_sample(transmitter, fsample);
            }
          }
        }
      }
    }
  }

  t_print("soapy_protocol: receive_thread: SoapySDRDevice_deactivateStream\n");
  SoapySDRDevice_deactivateStream(soapy_device, rx_stream[channel], 0, 0LL);
  /*
  t_print("soapy_protocol: receive_thread: SoapySDRDevice_closeStream\n");
  SoapySDRDevice_closeStream(soapy_device,rx_stream[channel]);
  t_print("soapy_protocol: receive_thread: SoapySDRDevice_unmake\n");
  SoapySDRDevice_unmake(soapy_device);
  */
  g_free(buffer);
  return NULL;
}

void soapy_protocol_iq_samples(float isample, float qsample) {
  int flags = 0;

  if (radio_is_transmitting()) {
    //
    // The "soapy_iqswap" logic has now been removed  from transmitter.c
    // and moved here, because this is where it is also handled
    // upon RX.
    //
    if (soapy_iqswap) {
      tx_output_buffer[(tx_output_buffer_index * 2)] = qsample;
      tx_output_buffer[(tx_output_buffer_index * 2) + 1] = isample;
    } else {
      tx_output_buffer[(tx_output_buffer_index * 2)] = isample;
      tx_output_buffer[(tx_output_buffer_index * 2) + 1] = qsample;
    }

    tx_output_buffer_index++;

    if (tx_output_buffer_index >= max_tx_samples) {
      const void *tx_buffs[] = {tx_output_buffer};
      long long timeNs = 0;
      long timeoutUs = 100000L;
      int elements = SoapySDRDevice_writeStream(soapy_device, tx_stream, tx_buffs, max_tx_samples, &flags, timeNs, timeoutUs);

      if (elements != max_tx_samples) {
        t_print("soapy_protocol_iq_samples: writeStream returned %d for %d elements\n", elements, max_tx_samples);
      }

      tx_output_buffer_index = 0;
    }
  }
}

// cppcheck-suppress unusedFunction
void soapy_protocol_stop() {
  t_print("soapy_protocol_stop\n");
  running = FALSE;
}

int soapy_protocol_get_rx_frequency_mhz(RECEIVER *rx) {
  double freq_hz = SoapySDRDevice_getFrequency(soapy_device, SOAPY_SDR_RX, rx->adc);
  return (int)((freq_hz / 1e6) + 0.5);
}

void soapy_protocol_set_rx_frequency(RECEIVER *rx, int v) {
  if (v < 0) {
    v = VFO_A;
  }

  if (soapy_device != NULL) {
    long long f = vfo[v].frequency;

    if (vfo[v].mode == modeCWU) {
      f -= (long long)cw_keyer_sidetone_frequency;
    } else if (vfo[v].mode == modeCWL) {
      f += (long long)cw_keyer_sidetone_frequency;
    }

    // f += frequency_calibration - vfo[v].lo;
    f = (f - vfo[v].lo);

    /*
     * PPM correction:
     * - LimeSDR: apply app-side scaling (robust, like other app).
     * - Others : keep existing app-side scaling.
     */
    if (have_lime) {
      const long long cal_0p1ppm = llround(ppm_factor * 10.0);
      f = (long long)((__int128)f * (10000000LL + cal_0p1ppm) / 10000000LL);
    } else {
      f = (long long)((double)f * (1.0 + ppm_factor / 1e6));
    }

    int rc;

    if (have_lime) {
      // LimeSDR: keep LO fixed when possible and move the BB offset.
      rc = soapy_lime_set_lo_bb(SOAPY_SDR_RX, (size_t)rx->adc, (double)f);

      if (rc != 0) {
        // Fallback: classic tuning.
        rc = SoapySDRDevice_setFrequency(soapy_device, SOAPY_SDR_RX, rx->adc, (double)f, NULL);
        soapy_rx_drop_blocks = 4;
      }
    } else {
      rc = SoapySDRDevice_setFrequency(soapy_device, SOAPY_SDR_RX, rx->adc, (double)f, NULL);
    }

    if (rc != 0) {
      t_print("soapy_protocol: SoapySDRDevice_setFrequency(RX) failed: %s\n", SoapySDR_errToStr(rc));
    }
  }
}

void soapy_protocol_set_tx_frequency(TRANSMITTER *tx) {
  if (can_transmit && soapy_device != NULL) {
    int v = vfo_get_tx_vfo();
    long long f;
    f = vfo[v].ctun ? vfo[v].ctun_frequency : vfo[v].frequency;

    if (vfo[v].xit_enabled) {
      f += vfo[v].xit;
    }

    // f += frequency_calibration - vfo[v].lo;
    f = (f - vfo[v].lo);

    /* PPM correction: Lime uses app-side scaling; others keep existing scaling. */
    if (have_lime) {
      const long long cal_0p1ppm = llround(ppm_factor * 10.0);
      f = (long long)((__int128)f * (10000000LL + cal_0p1ppm) / 10000000LL);
    } else {
      f = (long long)((double)f * (1.0 + ppm_factor / 1e6));
    }

    //t_print("soapy_protocol_set_tx_frequency: %f\n",f);
    int rc;

    if (have_lime) {
      rc = soapy_lime_set_lo_bb(SOAPY_SDR_TX, (size_t)tx->dac, (double)f);

      if (rc != 0) {
        rc = SoapySDRDevice_setFrequency(soapy_device, SOAPY_SDR_TX, tx->dac, (double) f, NULL);
      }
    } else {
      rc = SoapySDRDevice_setFrequency(soapy_device, SOAPY_SDR_TX, tx->dac, (double) f, NULL);
    }

    if (rc != 0) {
      t_print("soapy_protocol: SoapySDRDevice_setFrequency(TX) failed: %s\n", SoapySDR_errToStr(rc));
    }
  }
}

void soapy_protocol_set_rx_antenna(RECEIVER *rx, int ant) {
  if (soapy_device != NULL) {
    if (ant >= (int) radio->info.soapy.rx_antennas) { ant = (int) radio->info.soapy.rx_antennas - 1; }

    t_print("soapy_protocol: set_rx_antenna: %s\n", radio->info.soapy.rx_antenna[ant]);
    int rc = SoapySDRDevice_setAntenna(soapy_device, SOAPY_SDR_RX, rx->adc, radio->info.soapy.rx_antenna[ant]);

    if (rc != 0) {
      t_print("soapy_protocol: SoapySDRDevice_setAntenna RX failed: %s\n", SoapySDR_errToStr(rc));
    }
  }
}

void soapy_protocol_set_tx_antenna(TRANSMITTER *tx, int ant) {
  if (soapy_device != NULL) {
    if (ant >= (int) radio->info.soapy.tx_antennas) { ant = (int) radio->info.soapy.tx_antennas - 1; }

    t_print("soapy_protocol: set_tx_antenna: %s\n", radio->info.soapy.tx_antenna[ant]);
    int rc = SoapySDRDevice_setAntenna(soapy_device, SOAPY_SDR_TX, tx->dac, radio->info.soapy.tx_antenna[ant]);

    if (rc != 0) {
      t_print("soapy_protocol: SoapySDRDevice_setAntenna TX failed: %s\n", SoapySDR_errToStr(rc));
    }
  }
}

/*
void soapy_protocol_set_tx_antenna_lime(int ant) {
  if (soapy_device != NULL) {
    if (!have_lime) { return; }

    if (ant >= (int) radio->soapy.tx.antennas) { ant = (int) radio->soapy.tx.antennas - 1; }

    t_print("%s: set_tx_antenna: %s\n", __FUNCTION__, radio->soapy.tx.antenna[ant]);
    int rc = SoapySDRDevice_setAntenna(soapy_device, SOAPY_SDR_TX, 0, radio->soapy.tx.antenna[ant]);

    if (rc != 0) {
      t_print("%s: SetAntenna failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
    }
  }
}
*/

void soapy_protocol_set_gain(RECEIVER *rx) {
  int rc;
  t_print("soapy_protocol_set_gain: adc=%d gain=%f\n", rx->adc, (double)adc[rx->adc].gain);
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_RX, rx->adc, (double)adc[rx->adc].gain);

  if (rc != 0) {
    t_print("soapy_protocol: SoapySDRDevice_setGain failed: %s\n", SoapySDR_errToStr(rc));
  }

  t_print("%s: soapy_protocol_get_gain = %f\n", __FUNCTION__, SoapySDRDevice_getGain(soapy_device, SOAPY_SDR_RX,
          rx->adc));
}

int soapy_protocol_get_rx_gain(RECEIVER *rx) {
  double gain = SoapySDRDevice_getGain(soapy_device, SOAPY_SDR_RX, rx->adc);

  if (gain < 0) { return -1; }

  return (int)gain;
}

void soapy_protocol_get_settings_info(RECEIVER *rx) {
  size_t count = 0;
  const SoapySDRArgInfo *infos_const = SoapySDRDevice_getSettingInfo(soapy_device, &count);
  SoapySDRArgInfo *infos = (SoapySDRArgInfo *)infos_const;

  for (size_t i = 0; i < count; i++) {
    t_print("%s: Setting: %s (%s)\n", __FUNCTION__, infos[i].key, infos[i].name);
  }

  SoapySDRArgInfoList_clear(infos, count);
}

void soapy_protocol_set_rx_gain(int id) {
  int rc;
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_RX, id, adc[id].gain);

  if (rc != 0) {
    t_print("%s: SetGain failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_rx_attenuate(int id) {
  //
  // Make this receiver temporarily "deaf". This may be useful while TXing
  //
  int rc;
  //t_print("%s: id=%d gain=%f\n", __FUNCTION__, id, adc[id].min_gain);
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_RX, id, adc[id].min_gain);

  if (rc != 0) {
    t_print("%s: SetGain failed: %s\n", __FUNCTION__, SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_rx_unattenuate(int id) {
  //
  // Restore nominal RF gain to recover from having "deaf-ened" it
  // This must not do any harm if receivers have not been "deaf-ened" before
  // (this is the case in DUPLEX).
  //
  soapy_protocol_set_rx_gain(id);
}

void soapy_protocol_set_rx_gain_element(int id, char *name, double gain) {
  int rc;
  t_print("%s: id=%d %s=%f\n", __FUNCTION__, id, name, gain);
  rc = SoapySDRDevice_setGainElement(soapy_device, SOAPY_SDR_RX, id, name, gain);

  if (rc != 0) {
    t_print("%s: SetGainElement %s failed: %s\n", __FUNCTION__, name, SoapySDR_errToStr(rc));
  }

  //
  // The overall gain has now changed. So we need to query it and set the gain
  //
  adc[id].gain = SoapySDRDevice_getGain(soapy_device, SOAPY_SDR_RX, id);
}

void soapy_protocol_set_gain_element(const RECEIVER *rx, char *name, int gain) {
  int rc;
  t_print("%s: adc=%d %s=%d\n", __FUNCTION__, rx->adc, name, gain);
  rc = SoapySDRDevice_setGainElement(soapy_device, SOAPY_SDR_RX, rx->adc, name, (double)gain);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_setGainElement %s failed: %s\n", __FUNCTION__, name, SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_set_tx_gain(TRANSMITTER *tx, int gain) {
  int rc;
  rc = SoapySDRDevice_setGain(soapy_device, SOAPY_SDR_TX, tx->dac, (double)gain);

  if (rc != 0) {
    t_print("soapy_protocol: SoapySDRDevice_setGain failed: %s\n", SoapySDR_errToStr(rc));
  }
}

void soapy_protocol_set_tx_gain_element(TRANSMITTER *tx, char *name, int gain) {
  int rc;
  rc = SoapySDRDevice_setGainElement(soapy_device, SOAPY_SDR_TX, tx->dac, name, (double)gain);

  if (rc != 0) {
    t_print("soapy_protocol: SoapySDRDevice_setGainElement %s failed: %s\n", name, SoapySDR_errToStr(rc));
  }
}

int soapy_protocol_get_gain_element(RECEIVER *rx, char *name) {
  double gain;
  gain = SoapySDRDevice_getGainElement(soapy_device, SOAPY_SDR_RX, rx->adc, name);
  return (int)gain;
}

int soapy_protocol_get_tx_gain_element(TRANSMITTER *tx, char *name) {
  double gain;
  gain = SoapySDRDevice_getGainElement(soapy_device, SOAPY_SDR_TX, tx->dac, name);
  return (int)gain;
}

// cppcheck-suppress unusedFunction
gboolean soapy_protocol_get_automatic_gain(RECEIVER *rx) {
  gboolean mode = SoapySDRDevice_getGainMode(soapy_device, SOAPY_SDR_RX, rx->adc);
  return mode;
}

void soapy_protocol_get_driver(RECEIVER *rx) {
  char buf[32];
  const char *driver = SoapySDRDevice_getDriverKey(soapy_device);
  snprintf(buf, sizeof(buf), "%s", driver);
  t_print("%s: driver = %s\n", __FUNCTION__, buf);
  return;
}

void soapy_protocol_set_bias_t(gboolean enable) {
  const char *key = (radio && strcmp(radio->name, "sdrplay") == 0) ? "biasT_ctrl" : "biastee";
  const char *value = enable ? "true" : "false";
  int rc = SoapySDRDevice_writeSetting(soapy_device, key, value);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_writeSetting(%s=%s) failed: %s\n",
            __FUNCTION__, key, value, SoapySDR_errToStr(rc));
  }
}

gboolean soapy_protocol_get_bias_t(RECEIVER *rx) {
  const char *key = (radio && strcmp(radio->name, "sdrplay") == 0) ? "biasT_ctrl" : "biastee";
  const char *val = SoapySDRDevice_readSetting(soapy_device, key);

  if (val == NULL) { return -1; }

  // return (strcmp(val, "true") == 0) ? 1 : 0;
  return (strcmp(val, "true") == 0) ? TRUE : FALSE;
}

int soapy_protocol_get_rfgain_sel(RECEIVER *rx) {
  const char *val = SoapySDRDevice_readSetting(soapy_device, "rfgain_sel");
  t_print("%s: rfgain_sel raw = '%s'\n", __FUNCTION__, val ? val : "NULL");

  if (val == NULL) { return -1; }

  return atoi(val);
}

void soapy_protocol_set_rfgain_sel(RECEIVER *rx, int value) {
  char buf[8];

  if (value < 0 || value > 8) { return; }  // Wertebereich absichern

  snprintf(buf, sizeof(buf), "%d", value);
  int rc = SoapySDRDevice_writeSetting(soapy_device, "rfgain_sel", buf);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_writeSetting(rfgain_sel=%s) failed: %s\n",
            __FUNCTION__, buf, SoapySDR_errToStr(rc));
  }
}

const char *soapy_protocol_get_if_mode(RECEIVER *rx) {
  const char *val = SoapySDRDevice_readSetting(soapy_device, "if_mode");

  if (val == NULL) { return NULL; }

  static char buf[16];
  snprintf(buf, sizeof(buf), "%s", val);
  return buf;
}

int soapy_protocol_get_agc_setpoint(RECEIVER *rx) {
  const char *val = SoapySDRDevice_readSetting(soapy_device, "agc_setpoint");
  // t_print("%s: agc_setpoint raw = '%s'\n", __FUNCTION__, val ? val : "NULL");

  if (val == NULL) { return -1; }

  return atoi(val);
}

void soapy_protocol_set_agc_setpoint(RECEIVER *rx, int setpoint) {
  char buf[8];
  int rc;

  if (setpoint < -60 || setpoint > 0) { return; }

  snprintf(buf, sizeof(buf), "%d", setpoint);
  rc = SoapySDRDevice_writeSetting(soapy_device, "agc_setpoint", buf);

  if (rc != 0) {
    t_print("%s: SoapySDRDevice_writeSetting(agc_setpoint=%s) failed: %s\n",
            __FUNCTION__, buf, SoapySDR_errToStr(rc));
  }

  t_print("%s: agc_setpoint = %s\n", __FUNCTION__, SoapySDRDevice_readSetting(soapy_device, "agc_setpoint"));
}

void soapy_protocol_set_automatic_gain(RECEIVER *rx, gboolean mode) {
  int rc;
  rc = SoapySDRDevice_setGainMode(soapy_device, SOAPY_SDR_RX, rx->adc, mode);

  if (rc != 0) {
    t_print("soapy_protocol: SoapySDRDevice_getGainMode failed: %s\n", SoapySDR_errToStr(rc));
  }

  t_print("%s: GainMode = %d\n", __FUNCTION__, SoapySDRDevice_getGainMode(soapy_device, SOAPY_SDR_RX, rx->adc));
}

gboolean soapy_protocol_check_sdrplay_mod(RECEIVER *rx) {
  gboolean driver_flag = FALSE;

  if (radio->info.soapy.rx_has_automatic_gain && radio->info.soapy.rx_gains > 1
      && (strcmp(radio->name, "sdrplay") == 0)) {
    for (size_t i = 0; i < radio->info.soapy.rx_gains; i++) {
      if (strcmp(radio->info.soapy.rx_gain[i], "CURRENT") == 0) {
        driver_flag = TRUE;
        t_print("%s: Required RX Gain \"%s\" in SDRPlay_Soapy_module at index %d\n", __FUNCTION__, radio->info.soapy.rx_gain[i],
                i);
      }
    }
  }

  if (!driver_flag) {
    t_print("%s: WRONG SDRPlay_Soapy_module !\n", __FUNCTION__);
  }

  return driver_flag;
}

void soapy_protocol_rxtx(TRANSMITTER *tx) {
  //
  // Do everything that needs be done for a RX->TX transition
  //
  // This is called from rxtx() after the WDSP receivers are slewed down
  // (unless using DUPLEX) and the WDSP transmitter is slewed up.
  //
  // This routine is *never* called if there is no transmitter!
  //
  if (have_lime) {
    //
    // LIME:
    // - "mute" receivers if not running duplex,
    // - execute TRX relay via GPIO,
    // - (re-)connect TX antenna (since it was disconnected upon TXRX)
    // - (re-)set nominal TX drive (since it was set to zero upon TXRX)
    //
    if (!duplex) {
      soapy_protocol_rx_attenuate(active_receiver->id);
    }

    const char *bank = "MAIN";
    t_print("%s: Setting LIME GPIO to 1\n", __FUNCTION__);
    SoapySDRDevice_writeGPIODir(soapy_device, bank, 0xFF);
    SoapySDRDevice_writeGPIO(soapy_device, bank, 0x01);
    usleep(30000);
    soapy_protocol_set_tx_antenna(tx, dac[0].antenna);
    soapy_protocol_set_tx_gain(tx, tx->drive);
  }

  soapy_protocol_set_tx_frequency(tx);
}

void soapy_protocol_txrx(RECEIVER *rx) {
  //
  // Do everything that needs be done for a TX->RX transition
  //
  // This is called from rxtx() after the WDSP transmitter is slewed down
  // and the WDSP receivers are slewed up (in non-DUPLEX case)
  //
  // This routine is *never* called if there is no transmitter!
  //
  //
  if (have_lime) {
    //
    // LIME: DO NOT STOP the transmitter, but
    // - set TX gain to zero,
    // - disconnect antenna,
    // - execute TRX relay,
    // - set RX gains to nominal value
    //
    soapy_protocol_set_tx_gain(transmitter, 0); // TX drive is 0
    soapy_protocol_set_tx_antenna(transmitter, 0); // 0 is NONE
    const char *bank = "MAIN"; //set GPIO to signal the relay to RX
    t_print("%s: Setting LIME GPIO to 0\n", __FUNCTION__);
    SoapySDRDevice_writeGPIODir(soapy_device, bank, 0xFF);
    SoapySDRDevice_writeGPIO(soapy_device, bank, 0x00);
    soapy_protocol_rx_unattenuate(active_receiver->id);
  }

  soapy_protocol_set_rx_frequency(active_receiver, active_receiver->id);
}
