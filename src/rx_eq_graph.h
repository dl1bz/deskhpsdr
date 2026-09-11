/* Copyright (C)
 * 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef RX_EQ_GRAPH_H
#define RX_EQ_GRAPH_H

#include <gtk/gtk.h>

#include "receiver.h"

GtkWidget *rx_eq_graph_create(RECEIVER *rx);
void rx_eq_graph_bind_control(int rx_id, int index, GtkWidget *freq_spin, GtkWidget *gain_spin);
void rx_eq_graph_refresh(int rx_id);

#endif
