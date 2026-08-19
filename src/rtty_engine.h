#ifndef _RTTY_ENGINE_H
#define _RTTY_ENGINE_H

#include <stddef.h>

typedef enum {
  RTTY_FILL_MARK = 0,
  RTTY_FILL_LTRS = 1,
  RTTY_FILL_SPACE = 2
} RTTY_FILL_MODE;

void rtty_engine_init(void);
void rtty_engine_set_baud(double baud);
double rtty_engine_get_baud(void);
void rtty_engine_set_shift(int shift_hz);
int rtty_engine_get_shift(void);
void rtty_engine_set_reverse(int reverse);
int rtty_engine_get_reverse(void);
void rtty_engine_set_stop_bits(double stop_bits);
double rtty_engine_get_stop_bits(void);
void rtty_engine_set_uos(int enabled);
int rtty_engine_get_uos(void);
void rtty_engine_set_fill(RTTY_FILL_MODE fill);
RTTY_FILL_MODE rtty_engine_get_fill(void);
void rtty_engine_set_start_crlf(int enabled);
int rtty_engine_get_start_crlf(void);
void rtty_engine_set_idle_timeout(int seconds);
int rtty_engine_get_idle_timeout(void);
int rtty_engine_start(void);
int rtty_engine_queue_text(const char *text);
void rtty_engine_drain(void);
void rtty_engine_stop(void);
void rtty_engine_abort(void);
int rtty_engine_is_active(void);
void rtty_engine_render_iq(double *iq, int frames, int sample_rate, int txmode);

#endif
