/* waterfall3dss.h - 3D OpenGL waterfall display (Yaesu 3DSS style)
 * Adapted from piHPSDR for deskHPSDR
 * Copyright (C) 2015-2026 John Melton, G0ORX/N6LYT
 * Copyright (C) 2024-2025 Heiko Amft, DL1BZ (Project deskHPSDR)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef _WATERFALL3DSS_H
#define _WATERFALL3DSS_H

extern void waterfall3dss_update(RECEIVER *rx);
extern void waterfall3dss_init(RECEIVER *rx, int width, int height);

#include <gtk/gtk.h>

void waterfall3dss_gl_realize(GtkGLArea *area, gpointer data);
void waterfall3dss_gl_unrealize(GtkGLArea *area, gpointer data);
gboolean waterfall3dss_gl_render(GtkGLArea *area, GdkGLContext *context, gpointer data);

#endif
