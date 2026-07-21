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

#include <math.h>

#include "ext.h"
#include "radio.h"
#include "transmitter.h"
#include "tx_off.h"
#include "vox.h"

#define VOX_TIMER_INTERVAL_MS 5
#define VOX_PTT_DELAY_MS      50

enum vox_state {
  VOX_STATE_IDLE = 0,
  VOX_STATE_HANG,
  VOX_STATE_PTT_DELAY,
  VOX_STATE_OFF_REQUESTING
};

static GMutex vox_mutex;
static guint vox_timeout = 0;
static guint trx_scheduled = 0;
static enum vox_state vox_state = VOX_STATE_IDLE;
static gint64 vox_deadline_us = 0;

static double peak = 0.0;

static void vox_graceful_off_complete(void) {
  ext_set_vox(GINT_TO_POINTER(0));
  ext_vfo_update(NULL);
}

static gboolean vox_trx_scheduled_cb(gpointer data) {
  (void) data;
  int start_tx_off = 0;
  g_mutex_lock(&vox_mutex);
  trx_scheduled = 0;
  if (vox_state == VOX_STATE_PTT_DELAY) {
    vox_state = VOX_STATE_OFF_REQUESTING;
    start_tx_off = 1;
  }
  g_mutex_unlock(&vox_mutex);
  if (!start_tx_off) {
    return G_SOURCE_REMOVE;
  }
  /*
   * A microphone block can arrive between releasing vox_mutex and creating
   * the TX OFF request. VOX_STATE_OFF_REQUESTING detects that race: renewed
   * speech changes the state back to HANG and the just-created drain is then
   * cancelled before it can remove TX.
   */
  int requested = 0;
  if (vox && !mox && !tune) {
    requested = tx_off_request(TX_OFF_TARGET_VOX,
                               vox_graceful_off_complete);
  }
  int cancel_request = 0;
  g_mutex_lock(&vox_mutex);
  if (vox_state == VOX_STATE_OFF_REQUESTING) {
    vox_state = VOX_STATE_IDLE;
  } else if (requested) {
    cancel_request = 1;
  }
  g_mutex_unlock(&vox_mutex);
  if (cancel_request) {
    tx_off_cancel_target(TX_OFF_TARGET_VOX);
  }
  return G_SOURCE_REMOVE;
}

static gboolean vox_timeout_cb(gpointer data) {
  (void) data;
  gint64 now = g_get_monotonic_time();
  g_mutex_lock(&vox_mutex);
  if (vox_state != VOX_STATE_HANG) {
    vox_timeout = 0;
    g_mutex_unlock(&vox_mutex);
    return G_SOURCE_REMOVE;
  }
  /*
   * Voice Keyer replay owns TX. Start a complete VOX hang interval after
   * replay has finished instead of dropping TX in the old timer cycle.
   */
  if (capture_state == CAP_XMIT) {
    vox_deadline_us = now + (gint64) vox_hang * 1000;
    g_mutex_unlock(&vox_mutex);
    return G_SOURCE_CONTINUE;
  }
  if (now < vox_deadline_us) {
    g_mutex_unlock(&vox_mutex);
    return G_SOURCE_CONTINUE;
  }
  /*
   * vox_hang and the final PTT delay are separate, independently cancellable
   * phases. Renewed speech during either phase keeps the transmitter keyed.
   */
  vox_timeout = 0;
  vox_state = VOX_STATE_PTT_DELAY;
  trx_scheduled = g_timeout_add(VOX_PTT_DELAY_MS,
                                vox_trx_scheduled_cb,
                                NULL);
  g_mutex_unlock(&vox_mutex);
  return G_SOURCE_REMOVE;
}

double vox_get_peak(void) {
  return peak;
}

void clear_vox(void) {
  peak = 0.0;
}

void update_vox(double level) {
  /*
   * DEXP supplies only the detector level. VOX deliberately keeps its own
   * threshold and timing, so DEXP attack/hold/release do not become hidden
   * additional VOX controls.
   */
  peak = level;
  /* Voice Keyer Replay is handled by the periodic timer above. */
  if (capture_state == CAP_XMIT) {
    return;
  }
  if (vox_enabled && !mox && !tune && !TxInhibit && peak > vox_threshold) {
    gint64 now = g_get_monotonic_time();
    int start_vox;
    /*
     * A new speech block cancels every pending VOX-off phase: hang timer,
     * PTT-delay timer and an already started WDSP/protocol drain. The current
     * DEXP-processed block then reaches WDSP without a TX/RX transition.
     */
    tx_off_cancel_target(TX_OFF_TARGET_VOX);
    g_mutex_lock(&vox_mutex);
    if (trx_scheduled != 0) {
      g_source_remove(trx_scheduled);
      trx_scheduled = 0;
    }
    start_vox = vox_state == VOX_STATE_IDLE && !vox;
    vox_state = VOX_STATE_HANG;
    vox_deadline_us = now + (gint64) vox_hang * 1000;
    if (vox_timeout == 0) {
      vox_timeout = g_timeout_add(VOX_TIMER_INTERVAL_MS,
                                  vox_timeout_cb,
                                  NULL);
    }
    g_mutex_unlock(&vox_mutex);
    if (start_vox) {
      g_idle_add(ext_set_vox, GINT_TO_POINTER(1));
      g_idle_add(ext_vfo_update, NULL);
    }
  }
}

/*
 * If no VOX hang or VOX-owned TX OFF operation is pending, this is a no-op.
 */
void vox_cancel(void) {
  tx_off_cancel_target(TX_OFF_TARGET_VOX);
  g_mutex_lock(&vox_mutex);
  if (trx_scheduled != 0) {
    g_source_remove(trx_scheduled);
    trx_scheduled = 0;
  }
  if (vox_timeout != 0) {
    /*
     * Remove the old source before releasing the lock. Otherwise a new VOX
     * event could allocate a reused source ID which a delayed remove would
     * then delete by mistake.
     */
    g_source_remove(vox_timeout);
    vox_timeout = 0;
  }
  vox_state = VOX_STATE_IDLE;
  vox_deadline_us = 0;
  g_mutex_unlock(&vox_mutex);
}
