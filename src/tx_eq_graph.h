/* Copyright (C)
 * 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TX_EQ_GRAPH_H
#define TX_EQ_GRAPH_H

#include <gtk/gtk.h>

#include "transmitter.h"

GtkWidget *tx_eq_graph_create(TRANSMITTER *tx);
void tx_eq_graph_bind_control(int index, GtkWidget *freq_spin, GtkWidget *gain_spin);
void tx_eq_graph_refresh(void);

#endif
