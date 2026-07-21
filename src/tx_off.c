/* Copyright (C)
* 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
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
*/

#include <gtk/gtk.h>

#include <math.h>
#include <stdint.h>

#include "discovered.h"
#include "mode.h"
#include "message.h"
#include "transmitter.h"
#include "tx_off.h"

extern int protocol;
extern int vfo_get_tx_mode(void);
extern uint64_t old_protocol_tx_fence_begin(void);
extern int old_protocol_tx_fence_complete(uint64_t fence);
extern uint64_t new_protocol_tx_fence_begin(void);
extern int new_protocol_tx_fence_complete(uint64_t fence);

#define TX_OFF_TIMER_INTERVAL_MS       5
#define TX_OFF_FM_CTCSS_DRAIN_US       350000
#define TX_OFF_FM_CTCSS_CFC_DRAIN_US   800000
#define TX_OFF_DRAIN_MAX_US            2000000
#define TX_OFF_FENCE_TIMEOUT_US        1000000
#define TX_OFF_TOTAL_TIMEOUT_US        4000000
#define TX_OFF_DRAIN_QUIET_BLOCKS      2
#define TX_OFF_DRAIN_QUIET_LEVEL       1.0e-6
#define TX_OFF_P1_FIFO_GUARD_US        40000
#define TX_OFF_P2_FIFO_GUARD_US        15000

enum tx_off_state {
  TX_OFF_STATE_IDLE = 0,
  TX_OFF_STATE_DRAIN,
  TX_OFF_STATE_FENCE_ARMING,
  TX_OFF_STATE_FENCE,
  TX_OFF_STATE_GUARD,
  TX_OFF_STATE_COMPLETING
};

static GMutex tx_off_mutex;
static GCond tx_off_cond;
static guint tx_off_timeout = 0;
static enum tx_off_state tx_off_state = TX_OFF_STATE_IDLE;
static TX_OFF_TARGET tx_off_target = TX_OFF_TARGET_NONE;
static TX_OFF_COMPLETE_FUNC tx_off_complete = NULL;
static gint64 tx_off_requested_us = 0;
static gint64 tx_off_drain_started_us = 0;
static gint64 tx_off_fence_started_us = 0;
static gint64 tx_off_guard_deadline_us = 0;
static uint64_t tx_off_fence = 0;
static int tx_off_quiet_blocks = 0;

/*
 * Hot-path snapshots. The TX audio producer reads these without taking
 * tx_off_mutex for every 48 kHz microphone sample.
 */
static volatile gint tx_off_target_atomic = TX_OFF_TARGET_NONE;
static volatile gint tx_off_output_blocked = 0;

static const char *tx_off_target_name(TX_OFF_TARGET target) {
  switch (target) {
  case TX_OFF_TARGET_VOX:
    return "VOX";
  case TX_OFF_TARGET_MOX:
    return "MOX/PTT";
  default:
    return "unknown";
  }
}

static uint64_t tx_off_protocol_fence_begin(void) {
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    return old_protocol_tx_fence_begin();
  case NEW_PROTOCOL:
    return new_protocol_tx_fence_begin();
  default:
    return 0;
  }
}

static int tx_off_protocol_fence_complete(uint64_t fence) {
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    return old_protocol_tx_fence_complete(fence);
  case NEW_PROTOCOL:
    return new_protocol_tx_fence_complete(fence);
  default:
    return 1;
  }
}

static gint64 tx_off_protocol_fifo_guard_us(void) {
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    return TX_OFF_P1_FIFO_GUARD_US;
  case NEW_PROTOCOL:
    return TX_OFF_P2_FIFO_GUARD_US;
  default:
    return 0;
  }
}

static void tx_off_reset_locked(void) {
  tx_off_state = TX_OFF_STATE_IDLE;
  tx_off_target = TX_OFF_TARGET_NONE;
  tx_off_complete = NULL;
  tx_off_requested_us = 0;
  tx_off_drain_started_us = 0;
  tx_off_fence_started_us = 0;
  tx_off_guard_deadline_us = 0;
  tx_off_fence = 0;
  tx_off_quiet_blocks = 0;
  g_atomic_int_set(&tx_off_target_atomic, TX_OFF_TARGET_NONE);
  g_atomic_int_set(&tx_off_output_blocked, 0);
}

static gboolean tx_off_timeout_cb(gpointer data) {
  (void) data;
  gint64 now = g_get_monotonic_time();
  TX_OFF_COMPLETE_FUNC complete = NULL;
  TX_OFF_TARGET completed_target = TX_OFF_TARGET_NONE;
  g_mutex_lock(&tx_off_mutex);
  if (tx_off_state == TX_OFF_STATE_IDLE) {
    tx_off_timeout = 0;
    g_mutex_unlock(&tx_off_mutex);
    return G_SOURCE_REMOVE;
  }
  switch (tx_off_state) {
  case TX_OFF_STATE_DRAIN:
  case TX_OFF_STATE_FENCE_ARMING:
    if (now - tx_off_requested_us >= TX_OFF_TOTAL_TIMEOUT_US) {
      // Last-resort safety path if the TX producer no longer advances.
      // Do not wait forever for a WDSP block or a fence that cannot be armed.
      t_print("TX OFF (%s): total drain timeout\n",
              tx_off_target_name(tx_off_target));
      tx_off_state = TX_OFF_STATE_GUARD;
      tx_off_guard_deadline_us = now + tx_off_protocol_fifo_guard_us();
      g_atomic_int_set(&tx_off_output_blocked, 1);
    }
    break;
  case TX_OFF_STATE_FENCE:
    if (tx_off_protocol_fence_complete(tx_off_fence)) {
      tx_off_state = TX_OFF_STATE_GUARD;
      tx_off_guard_deadline_us = now + tx_off_protocol_fifo_guard_us();
    } else if (now - tx_off_fence_started_us >= TX_OFF_FENCE_TIMEOUT_US) {
      /*
       * A protocol or network failure must never leave the transmitter
       * keyed indefinitely. The following FIFO guard still preserves a
       * clean final zero packet whenever the transport is merely delayed.
       */
      t_print("TX OFF (%s): protocol fence timeout\n",
              tx_off_target_name(tx_off_target));
      tx_off_state = TX_OFF_STATE_GUARD;
      tx_off_guard_deadline_us = now + tx_off_protocol_fifo_guard_us();
    }
    break;
  case TX_OFF_STATE_GUARD:
    if (now >= tx_off_guard_deadline_us) {
      complete = tx_off_complete;
      completed_target = tx_off_target;
      tx_off_state = TX_OFF_STATE_COMPLETING;
      tx_off_timeout = 0;
    }
    break;
  case TX_OFF_STATE_IDLE:
  case TX_OFF_STATE_COMPLETING:
    break;
  }
  if (complete == NULL) {
    g_mutex_unlock(&tx_off_mutex);
    return G_SOURCE_CONTINUE;
  }
  /*
   * Do not hold tx_off_mutex while rxtx() performs the WDSP/channel
   * transition. Input and protocol output remain blocked until the
   * completion callback has actually removed MOX/VOX.
   */
  g_mutex_unlock(&tx_off_mutex);
  complete();
  g_mutex_lock(&tx_off_mutex);
  if (tx_off_state == TX_OFF_STATE_COMPLETING &&
      tx_off_target == completed_target) {
    tx_off_reset_locked();
    g_cond_broadcast(&tx_off_cond);
  }
  g_mutex_unlock(&tx_off_mutex);
  return G_SOURCE_REMOVE;
}

int tx_off_request(TX_OFF_TARGET target, TX_OFF_COMPLETE_FUNC complete) {
  if (target == TX_OFF_TARGET_NONE || complete == NULL) {
    return 0;
  }
  g_mutex_lock(&tx_off_mutex);
  if (tx_off_state != TX_OFF_STATE_IDLE) {
    int same_request = tx_off_target == target;
    g_mutex_unlock(&tx_off_mutex);
    return same_request;
  }
  tx_off_state = TX_OFF_STATE_DRAIN;
  tx_off_target = target;
  tx_off_complete = complete;
  tx_off_requested_us = g_get_monotonic_time();
  tx_off_drain_started_us = 0;
  tx_off_fence_started_us = 0;
  tx_off_guard_deadline_us = 0;
  tx_off_fence = 0;
  tx_off_quiet_blocks = 0;
  g_atomic_int_set(&tx_off_target_atomic, target);
  g_atomic_int_set(&tx_off_output_blocked, 0);
  tx_off_timeout = g_timeout_add(TX_OFF_TIMER_INTERVAL_MS,
                                 tx_off_timeout_cb,
                                 NULL);
  g_mutex_unlock(&tx_off_mutex);
  t_print("TX OFF (%s): graceful drain started\n",
          tx_off_target_name(target));
  return 1;
}

void tx_off_cancel_target(TX_OFF_TARGET target) {
  g_mutex_lock(&tx_off_mutex);
  /*
   * Once the completion callback has started, let it finish before the new
   * owner continues. Otherwise the caller could observe the old logical TX
   * state and miss the required re-key after the callback removes TX.
   */
  while (tx_off_state == TX_OFF_STATE_COMPLETING &&
         tx_off_target == target) {
    g_cond_wait(&tx_off_cond, &tx_off_mutex);
  }
  if (tx_off_state != TX_OFF_STATE_IDLE && tx_off_target == target) {
    if (tx_off_timeout != 0) {
      /*
       * Remove the source while holding the mutex. This prevents a newly
       * allocated source from reusing the old ID before g_source_remove().
       */
      g_source_remove(tx_off_timeout);
      tx_off_timeout = 0;
    }
    t_print("TX OFF (%s): graceful drain cancelled\n",
            tx_off_target_name(target));
    tx_off_reset_locked();
  }
  g_mutex_unlock(&tx_off_mutex);
}

void tx_off_cancel(void) {
  g_mutex_lock(&tx_off_mutex);
  while (tx_off_state == TX_OFF_STATE_COMPLETING) {
    g_cond_wait(&tx_off_cond, &tx_off_mutex);
  }
  if (tx_off_state != TX_OFF_STATE_IDLE) {
    if (tx_off_timeout != 0) {
      g_source_remove(tx_off_timeout);
      tx_off_timeout = 0;
    }
    t_print("TX OFF (%s): forced immediate cancellation\n",
            tx_off_target_name(tx_off_target));
    tx_off_reset_locked();
  }
  g_mutex_unlock(&tx_off_mutex);
}

int tx_off_pending_target(TX_OFF_TARGET target) {
  return (TX_OFF_TARGET) g_atomic_int_get(&tx_off_target_atomic) == target;
}

int tx_off_zero_input_samples(void) {
  /*
   * For MOX/PTT OFF, preserve samples already collected in the current
   * WDSP input block and replace only samples arriving after PTT release.
   */
  return g_atomic_int_get(&tx_off_target_atomic) == TX_OFF_TARGET_MOX;
}

int tx_off_zero_input_block(void) {
  /*
   * VOX must inspect the unmodified block first so renewed speech can
   * cancel the pending OFF without losing the first resumed word.
   */
  return g_atomic_int_get(&tx_off_target_atomic) == TX_OFF_TARGET_VOX;
}

int tx_off_output_enabled(void) {
  return !g_atomic_int_get(&tx_off_output_blocked);
}

void tx_off_output_block(const TRANSMITTER *tx) {
  gint64 drain_started;
  TX_OFF_TARGET target;
  g_mutex_lock(&tx_off_mutex);
  if (tx_off_state != TX_OFF_STATE_DRAIN) {
    g_mutex_unlock(&tx_off_mutex);
    return;
  }
  if (tx_off_drain_started_us == 0) {
    tx_off_drain_started_us = g_get_monotonic_time();
  }
  drain_started = tx_off_drain_started_us;
  target = tx_off_target;
  g_mutex_unlock(&tx_off_mutex);
  /*
   * Detect modulation rather than absolute RF level. Removing the complex
   * block mean makes the test work for AM/FM carriers as well as SSB/DIGI.
   */
  double mean_i = 0.0;
  double mean_q = 0.0;
  for (int i = 0; i < tx->output_samples; i++) {
    mean_i += tx->iq_output_buffer[2 * i];
    mean_q += tx->iq_output_buffer[2 * i + 1];
  }
  if (tx->output_samples > 0) {
    mean_i /= tx->output_samples;
    mean_q /= tx->output_samples;
  }
  double activity = 0.0;
  for (int i = 0; i < tx->output_samples; i++) {
    double di = fabs(tx->iq_output_buffer[2 * i] - mean_i);
    double dq = fabs(tx->iq_output_buffer[2 * i + 1] - mean_q);
    if (di > activity) { activity = di; }
    if (dq > activity) { activity = dq; }
  }
  gint64 now = g_get_monotonic_time();
  gint64 elapsed = now - drain_started;
  int txmode = vfo_get_tx_mode();
  int fixed_tail_mode = txmode == modeFMN && tx->ctcss_enabled;
  gint64 fixed_tail_us = tx->cfc ? TX_OFF_FM_CTCSS_CFC_DRAIN_US
                         : TX_OFF_FM_CTCSS_DRAIN_US;
  int arm_fence = 0;
  g_mutex_lock(&tx_off_mutex);
  if (tx_off_state == TX_OFF_STATE_DRAIN &&
      tx_off_target == target &&
      tx_off_drain_started_us == drain_started) {
    if (activity <= TX_OFF_DRAIN_QUIET_LEVEL) {
      tx_off_quiet_blocks++;
    } else {
      tx_off_quiet_blocks = 0;
    }
    if (fixed_tail_mode) {
      /*
       * CTCSS intentionally keeps FM IQ active. Use the measured fixed
       * WDSP tail instead of waiting for output silence that cannot occur.
       */
      arm_fence = elapsed >= fixed_tail_us;
    } else {
      arm_fence = tx_off_quiet_blocks >= TX_OFF_DRAIN_QUIET_BLOCKS ||
                  elapsed >= TX_OFF_DRAIN_MAX_US;
    }
    if (arm_fence) {
      tx_off_state = TX_OFF_STATE_FENCE_ARMING;
      g_atomic_int_set(&tx_off_output_blocked, 1);
    }
  }
  g_mutex_unlock(&tx_off_mutex);
  if (!arm_fence) {
    return;
  }
  /*
   * This is executed in the sole TX-IQ producer thread. Close the current
   * protocol block, append one complete zero block and remember its fence.
   * Every speech sample is therefore ahead of that fence.
   */
  uint64_t fence = tx_off_protocol_fence_begin();
  g_mutex_lock(&tx_off_mutex);
  if (tx_off_state == TX_OFF_STATE_FENCE_ARMING &&
      tx_off_target == target) {
    if (fence != 0) {
      tx_off_fence = fence;
      tx_off_fence_started_us = g_get_monotonic_time();
      tx_off_state = TX_OFF_STATE_FENCE;
      t_print("TX OFF (%s): WDSP tail %.1f ms, fence=%llu\n",
              tx_off_target_name(target),
              0.001 * (double) elapsed,
              (unsigned long long) fence);
    } else {
      /*
       * The protocol ring was temporarily full. Resume zero-block feeding
       * and retry after the producer/transport has recovered.
       */
      tx_off_state = TX_OFF_STATE_DRAIN;
      tx_off_quiet_blocks = 0;
      g_atomic_int_set(&tx_off_output_blocked, 0);
    }
  }
  g_mutex_unlock(&tx_off_mutex);
}
