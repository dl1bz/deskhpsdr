/* Copyright (C)
* 2019 - John Melton, G0ORX/N6LYT
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include "discovered.h"
#include "soapy_discovery.h"
#include "message.h"
#include "mystring.h"

static int rtlsdr_count = 0;
static int sdrplay_count = 0;

static void get_info(char *driver) {
  size_t rx_rates_length, tx_rates_length, rx_gains_length, tx_gains_length, ranges_length, rx_antennas_length,
         tx_antennas_length, rx_bandwidth_length, tx_bandwidth_length;
  SoapySDRKwargs args = {};
  int software_version = 0;
  const char *address = NULL;
  int rtlsdr_val = 0;
  int sdrplay_val = 0;
  char fw_version[16];
  char gw_version[16];
  char hw_version[16];
  char p_version[16];
  char** tx_antennas;
  char** tx_gains;
  t_print("soapy_discovery: get_info: %s\n", driver);
  STRLCPY(fw_version, "", 16);
  STRLCPY(gw_version, "", 16);
  STRLCPY(hw_version, "", 16);
  STRLCPY(p_version, "", 16);
  SoapySDRKwargs_set(&args, "driver", driver);

  if (strcmp(driver, "rtlsdr") == 0) {
    char count[16];
    snprintf(count, 16, "%d", rtlsdr_count);
    SoapySDRKwargs_set(&args, "rtl", count);
    rtlsdr_val = rtlsdr_count;
    rtlsdr_count++;
  } else if (strcmp(driver, "sdrplay") == 0) {
    char label[16];
    snprintf(label, 16, "SDRplay Dev%d", sdrplay_count);
    SoapySDRKwargs_set(&args, "label", label);
    sdrplay_val = sdrplay_count;
    sdrplay_count++;
  }

  SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
  SoapySDRKwargs_clear(&args);
  software_version = 0;
  char *driverkey = SoapySDRDevice_getDriverKey(sdr);
  t_print("DriverKey=%s\n", driverkey);
  char *hardwarekey = SoapySDRDevice_getHardwareKey(sdr);
  t_print("HardwareKey=%s\n", hardwarekey);

  if (strcmp(driver, "sdrplay") == 0) {
    address = hardwarekey;
  }

  SoapySDRKwargs info = SoapySDRDevice_getHardwareInfo(sdr);

  for (size_t i = 0; i < info.size; i++) {
    t_print("soapy_discovery: hardware info key=%s val=%s\n", info.keys[i], info.vals[i]);

    if (strcmp(info.keys[i], "firmwareVersion") == 0) {
      STRLCPY(fw_version, info.vals[i], 16);
    }

    if (strcmp(info.keys[i], "gatewareVersion") == 0) {
      STRLCPY(gw_version, info.vals[i], 16);
      software_version = (int)(atof(info.vals[i]) * 100.0);
    }

    if (strcmp(info.keys[i], "hardwareVersion") == 0) {
      STRLCPY(hw_version, info.vals[i], 16);
    }

    if (strcmp(info.keys[i], "protocolVersion") == 0) {
      STRLCPY(p_version, info.vals[i], 16);
    }
  }

  size_t rx_channels = SoapySDRDevice_getNumChannels(sdr, SOAPY_SDR_RX);
  t_print("Rx channels: %ld\n", (long) rx_channels);

  for (size_t i = 0; i < rx_channels; i++) {
    t_print("Rx channel full duplex: channel=%d fullduplex=%d\n", i, SoapySDRDevice_getFullDuplex(sdr, SOAPY_SDR_RX, i));
  }

  size_t tx_channels = SoapySDRDevice_getNumChannels(sdr, SOAPY_SDR_TX);
  t_print("Tx channels: %ld\n", (long) tx_channels);

  for (size_t i = 0; i < tx_channels; i++) {
    t_print("Tx channel full duplex: channel=%d fullduplex=%d\n", i, SoapySDRDevice_getFullDuplex(sdr, SOAPY_SDR_TX, i));
  }

  int sample_rate = 768000;
  SoapySDRRange *rx_rates = SoapySDRDevice_getSampleRateRange(sdr, SOAPY_SDR_RX, 0, &rx_rates_length);

  for (size_t i = 0; i < rx_rates_length; i++) {
    t_print("RX sample rate available: %20.6f -> %20.6f (%10.6f)\n", rx_rates[i].minimum, rx_rates[i].maximum,
            rx_rates[i].minimum / 48000.0);
  }

  if (strcmp(driver, "rtlsdr") == 0) {
    sample_rate = 1536000;
  } else if (strcmp(driver, "radioberry") == 0) {
    sample_rate = 48000;
  }

  free(rx_rates);
  t_print("sample_rate selected %d\n", sample_rate);

  if (tx_channels > 0) {
    SoapySDRRange *tx_rates = SoapySDRDevice_getSampleRateRange(sdr, SOAPY_SDR_TX, 0, &tx_rates_length);

    for (size_t i = 0; i < tx_rates_length; i++) {
      t_print("TX sample rate available: %20.6f -> %20.6f (%10.6f)\n", tx_rates[i].minimum, tx_rates[i].maximum,
              tx_rates[i].minimum / 48000.0);
    }

    free(tx_rates);
  }

  double *bandwidths = SoapySDRDevice_listBandwidths(sdr, SOAPY_SDR_RX, 0, &rx_bandwidth_length);

  for (size_t i = 0; i < rx_bandwidth_length; i++) {
    t_print("RX bandwidth available: %20.6f\n", bandwidths[i]);
  }

  free(bandwidths);

  if (tx_channels > 0) {
    bandwidths = SoapySDRDevice_listBandwidths(sdr, SOAPY_SDR_TX, 0, &tx_bandwidth_length);

    for (size_t i = 0; i < tx_bandwidth_length; i++) {
      t_print("TX bandwidth available: %20.6f\n", bandwidths[i]);
    }

    free(bandwidths);
  }

  double bandwidth = SoapySDRDevice_getBandwidth(sdr, SOAPY_SDR_RX, 0);
  t_print("RX1: bandwidth selected: %f\n", bandwidth);

  if (tx_channels > 0) {
    bandwidth = SoapySDRDevice_getBandwidth(sdr, SOAPY_SDR_TX, 0);
    t_print("TX0: bandwidth selected: %f\n", bandwidth);
  }

  SoapySDRRange *ranges = SoapySDRDevice_getFrequencyRange(sdr, SOAPY_SDR_RX, 0, &ranges_length);

  for (size_t i = 0; i < ranges_length; i++) t_print("RX freq range [%f MHz -> %f MHz step=%f]\n",
      ranges[i].minimum * 1E-6, ranges[i].maximum * 1E-6, ranges[i].step);

  char** rx_antennas = SoapySDRDevice_listAntennas(sdr, SOAPY_SDR_RX, 0, &rx_antennas_length);

  for (size_t i = 0; i < rx_antennas_length; i++) { t_print( "RX antenna: %s\n", rx_antennas[i]); }

  if (tx_channels > 0) {
    tx_antennas = SoapySDRDevice_listAntennas(sdr, SOAPY_SDR_TX, 0, &tx_antennas_length);

    for (size_t i = 0; i < tx_antennas_length; i++) { t_print( "TX antenna: %s\n", tx_antennas[i]); }
  }

  char **rx_gains = SoapySDRDevice_listGains(sdr, SOAPY_SDR_RX, 0, &rx_gains_length);
  gboolean has_automatic_gain = SoapySDRDevice_hasGainMode(sdr, SOAPY_SDR_RX, 0);
  t_print("has_automaic_gain=%d\n", has_automatic_gain);
  gboolean has_automatic_dc_offset_correction = SoapySDRDevice_hasDCOffsetMode(sdr, SOAPY_SDR_RX, 0);
  t_print("has_automaic_dc_offset_correction=%d\n", has_automatic_dc_offset_correction);

  if (tx_channels > 0) {
    tx_gains = SoapySDRDevice_listGains(sdr, SOAPY_SDR_TX, 0, &tx_gains_length);
  }

  size_t formats_length;
  char **formats = SoapySDRDevice_getStreamFormats(sdr, SOAPY_SDR_RX, 0, &formats_length);

  for (size_t i = 0; i < formats_length; i++) { t_print( "RX format available: %s\n", formats[i]); }

  size_t sensors;
  char **sensor = SoapySDRDevice_listSensors(sdr, &sensors);
  gboolean has_temp = FALSE;

  for (size_t i = 0; i < sensors; i++) {
    char *value = SoapySDRDevice_readSensor(sdr, sensor[i]);
    t_print( "Sensor:   %s=%s\n", sensor[i], value);

    if ((strstr(sensor[i], "temp")) != NULL) {
      has_temp = TRUE;
    }
  }

  if (devices < MAX_DEVICES) {
    discovered[devices].device = SOAPYSDR_USB_DEVICE;
    discovered[devices].protocol = SOAPYSDR_PROTOCOL;
    STRLCPY(discovered[devices].name, driver, sizeof(discovered[devices].name));
    discovered[devices].supported_receivers = rx_channels;
    discovered[devices].supported_transmitters = tx_channels;
    discovered[devices].adcs = rx_channels;
    discovered[devices].dacs = tx_channels;
    discovered[devices].status = STATE_AVAILABLE;
    discovered[devices].software_version = software_version;
    discovered[devices].frequency_min = ranges[0].minimum;
    discovered[devices].frequency_max = ranges[0].maximum;
    STRLCPY(discovered[devices].info.soapy.driver_key, driverkey, sizeof(discovered[devices].info.soapy.driver_key));
    STRLCPY(discovered[devices].info.soapy.hardware_key, hardwarekey, sizeof(discovered[devices].info.soapy.hardware_key));
    discovered[devices].info.soapy.sample_rate = sample_rate;

    if (strcmp(driver, "rtlsdr") == 0) {
      discovered[devices].info.soapy.rtlsdr_count = rtlsdr_val;
      discovered[devices].info.soapy.sdrplay_count = 0;
    } else if (strcmp(driver, "sdrplay") == 0) {
      discovered[devices].info.soapy.rtlsdr_count = 0;
      discovered[devices].info.soapy.sdrplay_count = sdrplay_val;
    } else {
      discovered[devices].info.soapy.rtlsdr_count = 0;
      discovered[devices].info.soapy.sdrplay_count = 0;
    }

    if (strcmp(driver, "lime") == 0) {
      snprintf(discovered[devices].info.soapy.version, sizeof(discovered[devices].info.soapy.version),
               "fw=%s gw=%s hw=%s p=%s", fw_version, gw_version, hw_version,
               p_version);
    } else if (strcmp(driver, "radioberry") == 0) {
      snprintf(discovered[devices].info.soapy.version, sizeof(discovered[devices].info.soapy.version),
               "fw=%s gw=%s", fw_version, gw_version);
    } else {
      STRLCPY(discovered[devices].info.soapy.version, "", sizeof(discovered[devices].info.soapy.version));
    }

    discovered[devices].info.soapy.rx_channels = rx_channels;
    discovered[devices].info.soapy.rx_gains = rx_gains_length;
    discovered[devices].info.soapy.rx_gain = rx_gains;
    discovered[devices].info.soapy.rx_range = malloc(rx_gains_length * sizeof(SoapySDRRange));

    for (size_t i = 0; i < rx_gains_length; i++) {
      t_print("RX gain %s\n", rx_gains[i]);
      SoapySDRRange rx_range = SoapySDRDevice_getGainElementRange(sdr, SOAPY_SDR_RX, 0, rx_gains[i]);
      t_print("RX gain available: %s, %f -> %f step=%f\n", rx_gains[i], rx_range.minimum, rx_range.maximum, rx_range.step);
      discovered[devices].info.soapy.rx_range[i] = rx_range;
    }

    discovered[devices].info.soapy.rx_has_automatic_gain = has_automatic_gain;
    discovered[devices].info.soapy.rx_has_automatic_dc_offset_correction = has_automatic_dc_offset_correction;
    discovered[devices].info.soapy.rx_antennas = rx_antennas_length;
    discovered[devices].info.soapy.rx_antenna = rx_antennas;
    discovered[devices].info.soapy.tx_channels = tx_channels;

    if (tx_channels > 0) {
      discovered[devices].info.soapy.tx_gains = tx_gains_length;
      discovered[devices].info.soapy.tx_gain = tx_gains;
      discovered[devices].info.soapy.tx_range = malloc(tx_gains_length * sizeof(SoapySDRRange));

      for (size_t i = 0; i < tx_gains_length; i++) {
        t_print("%s ", tx_gains[i]);
        SoapySDRRange tx_range = SoapySDRDevice_getGainElementRange(sdr, SOAPY_SDR_TX, 0, tx_gains[i]);
        t_print("TX gain %s, %f -> %f step=%f\n", tx_gains[i], tx_range.minimum, tx_range.maximum, tx_range.step);
        discovered[devices].info.soapy.tx_range[i] = tx_range;
      }

      discovered[devices].info.soapy.tx_antennas = tx_antennas_length;
      discovered[devices].info.soapy.tx_antenna = tx_antennas;
    }

    discovered[devices].info.soapy.sensors = sensors;
    discovered[devices].info.soapy.sensor = sensor;
    discovered[devices].info.soapy.has_temp = has_temp;

    if (address != NULL) {
      STRLCPY(discovered[devices].info.soapy.address, address, sizeof(discovered[devices].info.soapy.address));
    } else {
      STRLCPY(discovered[devices].info.soapy.address, "USB", sizeof(discovered[devices].info.soapy.address));
    }

    t_print("soapy_discovery: name=%s min=%0.3f MHz max=%0.3f MHz\n", discovered[devices].name,
            discovered[devices].frequency_min * 1E-6,
            discovered[devices].frequency_max * 1E-6);
    devices++;
  }

  SoapySDRDevice_unmake(sdr);
  free(ranges);
}

void soapy_discovery() {
  size_t length;
  SoapySDRKwargs input_args = {};
  t_print("%s\n", __FUNCTION__);
  rtlsdr_count = 0;
  SoapySDRKwargs_set(&input_args, "hostname", "pluto.local");
  SoapySDRKwargs *results = SoapySDRDevice_enumerate(&input_args, &length);
  t_print("%s: length=%d\n", __FUNCTION__, (int)length);

  for (size_t i = 0; i < length; i++) {
    for (size_t j = 0; j < results[i].size; j++) {
      if (strcmp(results[i].keys[j], "driver") == 0 && strcmp(results[i].vals[j], "audio") != 0) {
        get_info(results[i].vals[j]);
      }
    }
  }

  SoapySDRKwargsList_clear(results, length);
}
