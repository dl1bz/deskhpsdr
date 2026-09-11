/* Copyright (C)
 * 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CFC_GRAPH_H
#define CFC_GRAPH_H

#include <gtk/gtk.h>
#include "transmitter.h"

GtkWidget *cfc_graph_create(TRANSMITTER *tx);
void cfc_graph_bind_control(int index, GtkWidget *freq_spin, GtkWidget *comp_spin, GtkWidget *post_spin);
void cfc_graph_refresh(void);

#endif
