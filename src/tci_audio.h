/*  Copyright (C)
*   2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
*   TCI server based on libwebsockets is a complete rebuild for deskHPSDR
*   exclusivly by Heiko Amft, DL1BZ
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

//
// TCI server based on libwebsockets
// complete rebuild for deskHPSDR by DL1BZ
//

#ifndef _TCI_AUDIO_H
#define _TCI_AUDIO_H

#include <glib.h>
#include <stdint.h>
#include <stddef.h>

typedef struct _receiver RECEIVER;

typedef void (*TCI_AUDIO_WAKEUP_CALLBACK) (void);
typedef void (*TCI_AUDIO_TX_CHRONO_CALLBACK) (void);

#define TCI_RX_AUDIO_MAX_RECEIVERS 2
#define TCI_RX_AUDIO_RING_FRAMES 48000
#define TCI_RX_AUDIO_FRAME_FRAMES 512
#define TCI_AUDIO_SAMPLE_RATE 48000
#define TCI_AUDIO_SAMPLE_RATE_24K 24000
#define TCI_AUDIO_CHANNELS 2
#define TCI_AUDIO_FORMAT_FLOAT32 3
#define TCI_STREAM_RX_AUDIO 1
#define TCI_STREAM_TX_AUDIO 2
#define TCI_STREAM_TX_CHRONO 3
#define TCI_TX_AUDIO_FRAME_FRAMES 512
#define TCI_TX_AUDIO_24K_FRAME_FRAMES TCI_TX_AUDIO_FRAME_FRAMES
#define TCI_TX_AUDIO_INTERNAL_FRAME_FRAMES (TCI_TX_AUDIO_FRAME_FRAMES * 2)
#define TCI_TX_AUDIO_CHRONO_LENGTH (TCI_TX_AUDIO_FRAME_FRAMES * 2)
#define TCI_AUDIO_MONITOR_RING_FRAMES (48000 * 4)
#define TCI_TX_AUDIO_RING_FRAMES (48000 * 4)

void tci_audio_set_active (int active);
int tci_audio_is_active (void);
void tci_audio_set_wakeup_callback (TCI_AUDIO_WAKEUP_CALLBACK callback);
void tci_audio_set_tx_chrono_callback (TCI_AUDIO_TX_CHRONO_CALLBACK callback);
int tci_audio_normalize_sample_rate (int sample_rate);
guint tci_audio_get_stream_frames (void);
void tci_audio_destroy_rx_resamplers (void **resampler_l, void **resampler_r);
void tci_audio_destroy_tx_resampler (void **resampler);

typedef struct _tci_stream_header {
  uint32_t receiver;
  uint32_t sample_rate;
  uint32_t format;
  uint32_t codec;
  uint32_t crc;
  uint32_t length;
  uint32_t type;
  uint32_t channels;
  uint32_t reserv[8];
} TCI_STREAM_HEADER;

#define TCI_AUDIO_RX_FRAME_MAX_BYTES \
  (sizeof(TCI_STREAM_HEADER) + (TCI_RX_AUDIO_FRAME_FRAMES * TCI_AUDIO_CHANNELS * sizeof(float)))

void tci_audio_rx_sample (RECEIVER *rx, float left, float right);
void tci_audio_rx_block (RECEIVER *rx, const float *samples, guint frames);
guint64 tci_audio_get_write_count (int receiver_id);
guint tci_audio_get_frame (int receiver_id, guint64 *read_count, unsigned char *frame, size_t frame_size,
                           size_t *frame_len, int sample_rate, void **resampler_l, void **resampler_r);
void tci_audio_handle_tx_frame (const unsigned char *data, size_t len, int client_sample_rate,
                                void **resampler_24_to_48);

void tci_audio_tx_reset (void);
void tci_audio_tx_set_active (int active);
int tci_audio_tx_is_active (void);
int tci_audio_tx_enabled (void);
void tci_get_next_mic_sample (double *micsample);
guint tci_audio_tx_read (float *out, guint frames);
guint64 tci_audio_tx_available (void);
void tci_audio_tx_debug_snapshot (guint64 *write_count, guint64 *read_count,
                                  guint *dropped, guint64 *available, int *enabled);

void tci_audio_monitor_set_active (int active);
int tci_audio_monitor_is_active (void);
guint tci_audio_monitor_read (float *out, guint frames);

#endif
