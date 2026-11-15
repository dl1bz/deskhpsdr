/* Per-thread flush-to-zero for WDSP DSP worker threads. */
#ifndef WDSP_FTZ_H
#define WDSP_FTZ_H

#if defined(__x86_64__) || (defined(__i386__) && defined(__SSE__))
#include <xmmintrin.h>
static inline void wdsp_ftz_enable(void) {
  _mm_setcsr(_mm_getcsr() | _MM_FLUSH_ZERO_ON);
}
#elif defined(__aarch64__)
#include <stdint.h>
static inline void wdsp_ftz_enable(void) {
  uint64_t fpcr;
  __asm__ __volatile__("mrs %0, fpcr" : "=r" (fpcr));
  __asm__ __volatile__("msr fpcr, %0" : : "r" (fpcr | (1ull << 24)));
}
#else
static inline void wdsp_ftz_enable(void) { }
#endif

#endif
