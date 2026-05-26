/* Copyright (C)
* 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*/

#ifndef CW_ENGINE_H
#define CW_ENGINE_H

#include <glib.h>

#define CW_ENGINE_BUF_SIZE 512

extern void cw_engine_start_thread(void);
extern void cw_engine_clear(void);
extern int cw_engine_buffer_used(void);
extern int cw_engine_queue_char(char c);
extern int cw_engine_queue_text(const char *text);

#endif // CW_ENGINE_H
