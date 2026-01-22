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

#ifndef _SOAPY_PROTOCOL_H
#define _SOAPY_PROTOCOL_H

SoapySDRDevice *get_soapy_device(void);

void soapy_protocol_create_receiver(RECEIVER *rx);
void soapy_protocol_start_receiver(RECEIVER *rx);
void soapy_lime_set_freq_corr_ppm(const int direction, const size_t channel, const double ppm);

void soapy_protocol_init(gboolean hf);
void soapy_protocol_stop(void);
void soapy_protocol_stop_receiver(const RECEIVER *rx);
int soapy_protocol_get_rx_frequency_mhz(RECEIVER *rx);
void soapy_protocol_set_rx_frequency(RECEIVER *rx, int v);
void soapy_protocol_set_rx_antenna(RECEIVER *rx, int ant);
void soapy_protocol_set_lna_gain(RECEIVER *rx, int gain);
void soapy_protocol_set_gain(RECEIVER *rx);
int soapy_protocol_get_rx_gain(RECEIVER *rx);
void soapy_protocol_get_driver(RECEIVER *rx);
void soapy_protocol_get_settings_info(RECEIVER *rx);
void soapy_protocol_set_gain_element(const RECEIVER *rx, char *name, int gain);
int soapy_protocol_get_gain_element(RECEIVER *rx, char *name);
void soapy_protocol_change_sample_rate(RECEIVER *rx);
gboolean soapy_protocol_get_automatic_gain(RECEIVER *rx);
void soapy_protocol_set_automatic_gain(RECEIVER *rx, gboolean mode);
void soapy_protocol_create_transmitter(TRANSMITTER *tx);
void soapy_protocol_start_transmitter(TRANSMITTER *tx);
void soapy_protocol_stop_transmitter(TRANSMITTER *tx);
void soapy_protocol_set_tx_frequency(TRANSMITTER *tx);
void soapy_protocol_set_tx_antenna(TRANSMITTER *tx, int ant);
// void soapy_protocol_set_tx_antenna_lime(int ant);
void soapy_protocol_set_tx_gain(TRANSMITTER *tx, int gain);
void soapy_protocol_set_tx_gain_element(TRANSMITTER *tx, char *name, int gain);
int soapy_protocol_get_tx_gain_element(TRANSMITTER *tx, char *name);
void soapy_protocol_iq_samples(float isample, float qsample);
void soapy_protocol_set_mic_sample_rate(int rate);
void soapy_protocol_set_bias_t(gboolean enable);
gboolean soapy_protocol_get_bias_t(RECEIVER *rx);
int soapy_protocol_get_rfgain_sel(RECEIVER *rx);
const char *soapy_protocol_get_if_mode(RECEIVER *rx);
int soapy_protocol_get_agc_setpoint(RECEIVER *rx);
void soapy_protocol_set_agc_setpoint(RECEIVER *rx, int setpoint);
void soapy_protocol_set_rfgain_sel(RECEIVER *rx, int value);
gboolean soapy_protocol_check_sdrplay_mod(RECEIVER *rx);
void soapy_protocol_set_rx_gain(int id);
void soapy_protocol_set_rx_gain_element(int id, char *name, double gain);
void soapy_protocol_rx_attenuate(int id);
void soapy_protocol_rx_unattenuate(int id);
void soapy_protocol_rxtx(TRANSMITTER *tx);
void soapy_protocol_txrx(RECEIVER *rx);

#endif
