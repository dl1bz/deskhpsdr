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
extern void cw_engine_set_terminal(int enabled);
extern int cw_engine_get_terminal(void);
extern void cw_engine_set_start_delay(int delay_ms);
extern int cw_engine_get_start_delay(void);
extern void cw_engine_set_empty_callback(void (*callback)(void));
extern int cw_engine_buffer_used(void);
extern int cw_engine_queue_position(void);
extern int cw_engine_replace_queued_range(int start_pos, int end_pos, const char *replacement, int *new_end_pos);
extern int cw_engine_queue_char(char c);
extern int cw_engine_queue_text(const char *text);

#endif // CW_ENGINE_H
