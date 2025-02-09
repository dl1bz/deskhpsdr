/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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

#ifndef HPSDR_SERVER_H
#define HPSDR_SERVER_H

#include <stdint.h>

#ifndef __APPLE__
  #define htonll htobe64
  #define ntohll be64toh
#endif

//
// Conversion of host(double) to/from network(uint32) with 1E-6 resolution
// Assume that double data is between -2000 and 2000,
// convert to uint32 via 1000000.0*(double+2000.0) (result in the range 0 to 4E9)
//
#define htond(X) htonl((uint32_t) ((X+2000.0)*1000000.0) )
#define ntohd(X) 0.000001*ntohl(X)-2000.0
#define mydouble uint32_t

typedef enum {
  RECEIVER_DETACHED,
  RECEIVER_ATTACHED
} CLIENT_STATE;

enum _header_type_enum {
  INFO_RADIO,
  INFO_ADC,
  INFO_RECEIVER,
  INFO_TRANSMITTER,
  INFO_VFO,
  INFO_SPECTRUM,
  INFO_AUDIO,
  CMD_RESP_SPECTRUM,
  CMD_RESP_AUDIO,
  CMD_RESP_SAMPLE_RATE,
  CMD_RESP_LOCK,
  CMD_RESP_CTUN,
  CMD_RESP_SPLIT,
  CMD_RESP_SAT,
  CMD_RESP_DUP,
  CMD_RESP_STEP,
  CMD_RESP_RECEIVERS,
  CMD_RESP_RX_FREQ,
  CMD_RESP_RX_STEP,
  CMD_RESP_RX_MOVE,
  CMD_RESP_RX_MOVETO,
  CMD_RESP_RX_BAND,
  CMD_RESP_RX_MODE,
  CMD_RESP_RX_FILTER,
  CMD_RESP_RX_AGC,
  CMD_RESP_RX_NOISE,
  CMD_RESP_RX_ZOOM,
  CMD_RESP_RX_PAN,
  CMD_RESP_RX_VOLUME,
  CMD_RESP_RX_AGC_GAIN,
  CMD_RESP_RX_ATTENUATION,
  CMD_RESP_RX_GAIN,
  CMD_RESP_RX_SQUELCH,
  CMD_RESP_RX_FPS,
  CMD_RESP_RX_SELECT,
  CMD_RESP_VFO,
  CMD_RESP_RIT_TOGGLE,
  CMD_RESP_RIT_CLEAR,
  CMD_RESP_RIT,
  CMD_RESP_XIT_TOGGLE,
  CMD_RESP_XIT_CLEAR,
  CMD_RESP_XIT,
  CMD_RESP_RIT_INCREMENT,
  CMD_RESP_FILTER_BOARD,
  CMD_RESP_SWAP_IQ,
  CMD_RESP_REGION,
  CMD_RESP_MUTE_RX,
};

enum _vfo_action_enum {
  VFO_A_TO_B,
  VFO_B_TO_A,
  VFO_A_SWAP_B,
};

#define CLIENT_SERVER_VERSION 0LL

#define SPECTRUM_DATA_SIZE 1024
#define AUDIO_DATA_SIZE 1024

#define REMOTE_SYNC (uint16_t)0xFAFA

typedef struct _remote_rx {
  int receiver;
  gboolean send_audio;
  int audio_format;
  int audio_port;
  struct sockaddr_in audio_address;
  gboolean send_spectrum;
  int spectrum_fps;
  int spectrum_port;
  struct sockaddr_in spectrum_address;
} REMOTE_RX;

typedef struct _remote_client {
  gboolean running;
  int socket;
  socklen_t address_length;
  struct sockaddr_in address;
  GThread *thread_id;
  CLIENT_STATE state;
  int receivers;
  guint spectrum_update_timer_id;
  REMOTE_RX receiver[8];
  void *next;
} REMOTE_CLIENT;

typedef struct __attribute__((__packed__)) _header {
  uint16_t sync;
  uint16_t data_type;
  uint64_t version;
  union {
    uint64_t i;
    REMOTE_CLIENT *client;
  } context;
} HEADER;

typedef struct __attribute__((__packed__)) _radio_data {
  HEADER header;
  char name[32];
  uint16_t protocol;
  uint16_t device;
  uint64_t sample_rate;
  uint64_t frequency_min;
  uint64_t frequency_max;
  uint8_t locked;
  uint16_t supported_receivers;
  uint16_t receivers;
  uint8_t can_transmit;
  uint8_t split;
  uint8_t sat_mode;
  uint8_t duplex;
  uint8_t have_rx_gain;
  uint16_t rx_gain_calibration;
  uint16_t filter_board;
} RADIO_DATA;

typedef struct __attribute__((__packed__)) _adc_data {
  HEADER header;
  uint8_t adc;
  uint16_t filters;
  uint16_t hpf;
  uint16_t lpf;
  uint16_t antenna;
  uint8_t dither;
  uint8_t random;
  uint8_t preamp;
  uint16_t attenuation;
  mydouble gain;
  mydouble min_gain;
  mydouble max_gain;
} ADC_DATA;

typedef struct __attribute__((__packed__)) _receiver_data {
  HEADER header;
  uint8_t rx;
  uint8_t adc;
  mydouble volume;
  uint32_t fft_size;
  uint8_t  agc;
  mydouble agc_gain;
  uint64_t sample_rate;
  uint8_t displaying;
  uint8_t display_panadapter;
  uint8_t display_waterfall;
  uint16_t fps;
  mydouble agc_hang;
  mydouble agc_thresh;
  mydouble agc_hang_thresh;
  uint8_t nb;
  uint8_t nb2_mode;
  uint8_t nr;
  uint8_t nr_agc;
  uint8_t nr2_ae;
  uint8_t anf;
  uint8_t snb;
  mydouble nr2_trained_threshold;
  mydouble nb_tau;
  mydouble nb_hang;
  mydouble nb_advtime;
  mydouble nb_thresh;
  mydouble nr4_reduction_amount;
  mydouble nr4_smoothing_factor;
  mydouble nr4_whitening_factor;
  mydouble nr4_noise_rescale;
  mydouble nr4_post_filter_threshold;
  uint8_t display_gradient;
  uint8_t display_filled;
  uint8_t display_detector_mode;
  uint8_t display_average_mode;
  uint16_t display_average_time;
  uint16_t filter_low;
  uint16_t filter_high;
  uint16_t panadapter_low;
  uint16_t panadapter_high;
  uint16_t panadapter_step;
  uint16_t waterfall_low;
  uint16_t waterfall_high;
  uint8_t waterfall_automatic;
  uint16_t pixels;
  uint16_t zoom;
  uint16_t pan;
  uint16_t width;
  uint16_t height;
  uint16_t x;
  uint16_t y;
} RECEIVER_DATA;

typedef struct __attribute__((__packed__)) _vfo_data {
  HEADER header;
  uint8_t vfo;
  uint16_t band;
  uint16_t bandstack;
  uint64_t frequency;
  uint16_t mode;
  uint16_t filter;
  uint8_t ctun;
  uint64_t ctun_frequency;
  uint8_t rit_enabled;
  uint64_t rit;
  uint64_t lo;
  uint64_t offset;
  uint64_t step;
} VFO_DATA;

//
// TODO: make the spectrum variable-size since displays
//       now can change size
//
typedef struct __attribute__((__packed__)) _spectrum_data {
  HEADER header;
  uint8_t rx;
  uint64_t vfo_a_freq;
  uint64_t vfo_b_freq;
  uint64_t vfo_a_ctun_freq;
  uint64_t vfo_b_ctun_freq;
  uint64_t vfo_a_offset;
  uint64_t vfo_b_offset;
  mydouble meter;
  uint16_t samples;
  uint16_t sample[SPECTRUM_DATA_SIZE];
} SPECTRUM_DATA;

typedef struct __attribute__((__packed__)) _audio_data {
  HEADER header;
  uint8_t rx;
  uint16_t samples;
  uint16_t sample[AUDIO_DATA_SIZE * 2];
} AUDIO_DATA;

typedef struct __attribute__((__packed__)) _spectrum_command {
  HEADER header;
  uint8_t id;
  uint8_t start_stop;
} SPECTRUM_COMMAND;

typedef struct __attribute__((__packed__)) _freq_command {
  HEADER header;
  uint8_t id;
  uint64_t hz;
} FREQ_COMMAND;

typedef struct __attribute__((__packed__)) _step_command {
  HEADER header;
  uint8_t id;
  uint16_t steps;
} STEP_COMMAND;

typedef struct __attribute__((__packed__)) _sample_rate_command {
  HEADER header;
  int8_t id;
  uint64_t sample_rate;
} SAMPLE_RATE_COMMAND;

typedef struct __attribute__((__packed__)) _receivers_command {
  HEADER header;
  uint8_t receivers;
} RECEIVERS_COMMAND;

typedef struct __attribute__((__packed__)) _move_command {
  HEADER header;
  uint8_t id;
  uint64_t hz;
  uint8_t round;
} MOVE_COMMAND;

typedef struct __attribute__((__packed__)) _move_to_command {
  HEADER header;
  uint8_t id;
  uint64_t hz;
} MOVE_TO_COMMAND;

typedef struct __attribute__((__packed__)) _zoom_command {
  HEADER header;
  uint8_t id;
  uint16_t zoom;
} ZOOM_COMMAND;

typedef struct __attribute__((__packed__)) _pan_command {
  HEADER header;
  uint8_t id;
  uint16_t pan;
} PAN_COMMAND;

typedef struct __attribute__((__packed__)) _volume_command {
  HEADER header;
  uint8_t id;
  mydouble volume;
} VOLUME_COMMAND;

typedef struct __attribute__((__packed__)) _band_command {
  HEADER header;
  uint8_t id;
  uint16_t band;
} BAND_COMMAND;

typedef struct __attribute__((__packed__)) _mode_command {
  HEADER header;
  uint8_t id;
  uint16_t mode;
} MODE_COMMAND;

typedef struct __attribute__((__packed__)) _filter_command {
  HEADER header;
  uint8_t id;
  uint16_t filter;
  uint16_t filter_low;
  uint16_t filter_high;
} FILTER_COMMAND;

typedef struct __attribute__((__packed__)) _agc_command {
  HEADER header;
  uint8_t id;
  uint16_t agc;
} AGC_COMMAND;

typedef struct __attribute__((__packed__)) _agc_gain_command {
  HEADER header;
  uint8_t id;
  mydouble gain;
  mydouble hang;
  mydouble thresh;
  mydouble hang_thresh;
} AGC_GAIN_COMMAND;

typedef struct __attribute__((__packed__)) _attenuation_command {
  HEADER header;
  uint8_t id;
  uint16_t attenuation;
} ATTENUATION_COMMAND;

typedef struct __attribute__((__packed__)) _rfgain_command {
  HEADER header;
  uint8_t id;
  mydouble gain;
} RFGAIN_COMMAND;

typedef struct __attribute__((__packed__)) _squelch_command {
  HEADER header;
  uint8_t id;
  uint8_t enable;
  uint16_t squelch;
} SQUELCH_COMMAND;

typedef struct __attribute__((__packed__)) _noise_command {
  HEADER header;
  uint8_t  id;
  uint8_t  nb;
  uint8_t  nr;
  uint8_t  anf;
  uint8_t  snb;
  uint8_t  nb2_mode;
  uint8_t  nr_agc;
  uint8_t  nr2_gain_method;
  uint8_t  nr2_npe_method;
  uint8_t  nr2_ae;
  mydouble nb_tau;
  mydouble nb_hang;
  mydouble nb_advtime;
  mydouble nb_thresh;
  mydouble nr2_trained_threshold;
  mydouble nr4_reduction_amount;
  mydouble nr4_smoothing_factor;
  mydouble nr4_whitening_factor;
  mydouble nr4_noise_rescale;
  mydouble nr4_post_filter_threshold;
} NOISE_COMMAND;

typedef struct __attribute__((__packed__)) _split_command {
  HEADER header;
  uint8_t split;
} SPLIT_COMMAND;

typedef struct __attribute__((__packed__)) _sat_command {
  HEADER header;
  uint8_t sat;
} SAT_COMMAND;

typedef struct __attribute__((__packed__)) _dup_command {
  HEADER header;
  uint8_t dup;
} DUP_COMMAND;

typedef struct __attribute__((__packed__)) _fps_command {
  HEADER header;
  uint8_t id;
  uint8_t fps;
} FPS_COMMAND;

typedef struct __attribute__((__packed__)) _ctun_command {
  HEADER header;
  uint8_t id;
  uint8_t ctun;
} CTUN_COMMAND;

typedef struct __attribute__((__packed__)) _rx_select_command {
  HEADER header;
  uint8_t id;
} RX_SELECT_COMMAND;

typedef struct __attribute__((__packed__)) _vfo_command {
  HEADER header;
  uint8_t id;
} VFO_COMMAND;

typedef struct __attribute__((__packed__)) _lock_command {
  HEADER header;
  uint8_t lock;
} LOCK_COMMAND;

typedef struct __attribute__((__packed__)) _rit_toggle_command {
  HEADER header;
  uint8_t id;
} RIT_TOGGLE_COMMAND;

typedef struct __attribute__((__packed__)) _rit_clear_command {
  HEADER header;
  uint8_t id;
} RIT_CLEAR_COMMAND;

typedef struct __attribute__((__packed__)) _rit_command {
  HEADER header;
  uint8_t id;
  uint16_t rit;
} RIT_COMMAND;

typedef struct __attribute__((__packed__)) _xit_toggle_command {
  HEADER header;
} XIT_TOGGLE_COMMAND;

typedef struct __attribute__((__packed__)) _xit_clear_command {
  HEADER header;
} XIT_CLEAR_COMMAND;

typedef struct __attribute__((__packed__)) _xit_command {
  HEADER header;
  uint16_t xit;
} XIT_COMMAND;

typedef struct __attribute__((__packed__)) _rit_increment {
  HEADER header;
  uint16_t increment;
} RIT_INCREMENT_COMMAND;

typedef struct __attribute__((__packed__)) _filter_board {
  HEADER header;
  uint8_t filter_board;
} FILTER_BOARD_COMMAND;

typedef struct __attribute__((__packed__)) _swap_iq {
  HEADER header;
  uint8_t iqswap;
} SWAP_IQ_COMMAND;

typedef struct __attribute__((__packed__)) _region {
  HEADER header;
  uint8_t region;
} REGION_COMMAND;

typedef struct __attribute__((__packed__)) _mute_rx {
  HEADER header;
  uint8_t mute;
} MUTE_RX_COMMAND;

extern gboolean hpsdr_server;
extern gboolean hpsdr_server;
extern int client_socket;
extern int start_spectrum(void *data);
extern void start_vfo_timer(void);
extern gboolean remote_started;

extern REMOTE_CLIENT *remoteclients;

extern int listen_port;

extern int create_hpsdr_server(void);
extern int destroy_hpsdr_server(void);

extern int radio_connect_remote(char *host, int port);

extern void send_radio_data(const REMOTE_CLIENT *client);
extern void send_adc_data(const REMOTE_CLIENT *client, int i);
extern void send_receiver_data(const REMOTE_CLIENT *client, int rx);
extern void send_vfo_data(const REMOTE_CLIENT *client, int v);

extern void send_start_spectrum(int s, int rx);
extern void send_vfo_frequency(int s, int rx, long long hz);
extern void send_vfo_move_to(int s, int rx, long long hz);
extern void send_vfo_move(int s, int rx, long long hz, int round);
extern void update_vfo_move(int rx, long long hz, int round);
extern void send_vfo_step(int s, int rx, int steps);
extern void update_vfo_step(int rx, int steps);
extern void send_zoom(int s, int rx, int zoom);
extern void send_pan(int s, int rx, int pan);
extern void send_volume(int s, int rx, double volume);
extern void send_agc(int s, int rx, int agc);
extern void send_agc_gain(int s, int rx, double gain, double hang, double thresh, double hang_thresh);
extern void send_attenuation(int s, int rx, int attenuation);
extern void send_rfgain(int s, int rx, double gain);
extern void send_squelch(int s, int rx, int enable, int squelch);
extern void send_noise(int s, const RECEIVER *rx);
extern void send_band(int s, int rx, int band);
extern void send_mode(int s, int rx, int mode);
extern void send_filter(int s, int rx, int filter);
extern void send_split(int s, int split);
extern void send_sat(int s, int sat);
extern void send_dup(int s, int dup);
extern void send_ctun(int s, int vfo, int ctun);
extern void send_fps(int s, int rx, int fps);
extern void send_rx_select(int s, int rx);
extern void send_vfo(int s, int action);
extern void send_lock(int s, int lock);
extern void send_rit_toggle(int s, int rx);
extern void send_rit_clear(int s, int rx);
extern void send_rit(int s, int rx, int rit);
extern void send_xit_toggle(int s);
extern void send_xit_clear(int s);
extern void send_xit(int s, int xit);
extern void send_sample_rate(int s, int rx, int sample_rate);
extern void send_receivers(int s, int receivers);
extern void send_rit_step(int s, int step);
extern void send_filter_board(int s, int filter_board);
extern void send_swap_iq(int s, int swap_iq);
extern void send_region(int s, int region);
extern void send_mute_rx(int s, int mute);

extern void remote_audio(const RECEIVER *rx, short left_sample, short right_sample);

#endif
