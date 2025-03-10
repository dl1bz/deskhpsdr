/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT
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

#include "radio.h"
#include "transmitter.h"
#include "vox.h"
#include "vfo.h"
#include "ext.h"

static guint vox_timeout = 0;

static double peak = 0.0;

static int vox_timeout_cb(gpointer data) {
  //
  // First set vox_timeout to zero (via vox_cancel())
  // indicating no "hanging" timeout
  // then, remove VOX and update display
  //
  vox_cancel();
  g_idle_add(ext_set_vox, GINT_TO_POINTER(0));
  g_idle_add(ext_vfo_update, NULL);
  return FALSE;
}

double vox_get_peak() {
  double result = peak;
  return result;
}

void clear_vox() {
  peak = 0.0;
}

void update_vox(TRANSMITTER *tx) {
  // calculate peak microphone input
  // assumes it is interleaved left and right channel with length samples
  peak = 0.0;

  for (int i = 0; i < tx->buffer_size; i++) {
    double sample = tx->mic_input_buffer[i * 2];

    if (sample < 0.0) {
      sample = -sample;
    }

    if (sample > peak) {
      peak = sample;
    }
  }

  if (vox_enabled && !mox && !tune && !TxInhibit) {
    if (peak > vox_threshold) {
      // we use the value of vox_timeout to determine whether
      // the time-out is "hanging". We cannot use the value of vox
      // since this may be set with a delay, and we MUST NOT miss
      // a "hanging" timeout. Note that if a time-out fires, vox_timeout
      // is set to zero.
      if (vox_timeout > 0) {
        g_source_remove(vox_timeout);
        vox_timeout = 0;
      } else {
        //
        // no hanging time-out, assume that we just fired VOX
        //
        g_idle_add(ext_set_vox, GINT_TO_POINTER(1));
        g_idle_add(ext_vfo_update, NULL);
      }

      // re-init "vox hang" time
      vox_timeout = g_timeout_add((int)vox_hang, vox_timeout_cb, NULL);
    }

    // if peak is not above threshold, do nothing (this shall be done later in the timeout event
  }
}

//
// If no vox time-out is hanging, this function is a no-op
//
void vox_cancel() {
  if (vox_timeout) {
    g_source_remove(vox_timeout);
    vox_timeout = 0;
  }
}
