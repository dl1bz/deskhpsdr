/*  channel.c

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013 Warren Pratt, NR0V

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at

warren@wpratt.com

*/

#include "comm.h"
#include <limits.h>
#include <stdint.h>

struct _ch ch[MAX_CHANNELS];

static int valid_channel_id(int channel) {
  return channel >= 0 && channel < MAX_CHANNELS;
}

static int valid_channel_type(int type) {
  return type == 0 || type == 1 || type == 31;
}

/* Buffer sizes must describe whole samples and fit the I/O ring indices.
 * Reject unsupported fractional rate ratios rather than silently truncating
 * integer divisions (or creating zero-length buffers).
 */
static int scaled_size(int size, int from_rate, int to_rate, int *result) {
  int64_t value;
  if (size <= 0 || from_rate <= 0 || to_rate <= 0) { return 0; }
  value = (int64_t)size * to_rate;
  if (value % from_rate != 0) { return 0; }
  value /= from_rate;
  if (value <= 0 || value > INT_MAX / DSP_MULT) { return 0; }
  *result = (int)value;
  return 1;
}

static int valid_channel_sizes_rates(int in_size, int dsp_size, int in_rate, int dsp_rate, int out_rate) {
  int dsp_insize, dsp_outsize, out_size;
  int r1_size, r2_size;
  if (!scaled_size(dsp_size, dsp_rate, in_rate, &dsp_insize) ||
      !scaled_size(dsp_size, dsp_rate, out_rate, &dsp_outsize) ||
      !scaled_size(in_size, in_rate, out_rate, &out_size) ||
      in_size > INT_MAX / DSP_MULT || dsp_size > INT_MAX / DSP_MULT) { return 0; }
  /* iobuffs.c wraps ring indices only on exact equality. Both producer
   * and consumer block sizes must divide the ring capacity; otherwise
   * an index eventually crosses the allocation boundary. */
  r1_size = in_size > dsp_insize ? in_size : dsp_insize;
  r2_size = out_size > dsp_outsize ? out_size : dsp_outsize;
  if ((int64_t)DSP_MULT * r1_size % in_size != 0 ||
      (int64_t)DSP_MULT * r1_size % dsp_insize != 0 ||
      (int64_t)DSP_MULT * r2_size % out_size != 0 ||
      (int64_t)DSP_MULT * r2_size % dsp_outsize != 0) { return 0; }
  /* Sem_OutReady is created with a maximum count of 1000. */
  if ((int64_t)(DSP_MULT - 1) * r2_size / out_size > 1000) { return 0; }
  return 1;
}

/* Slew/delay durations are converted to signed sample counters in
 * create_slews() and the channel setters. Reject NaN, infinity and values
 * outside the representable range before performing those casts.
 * Keep one sample of headroom for the +1 slew-table allocation.
 */
static int valid_sample_time(double time, int rate) {
  return rate > 0 && time >= 0.0 &&
         time * (double)rate < (double)INT_MAX;
}

static int channel_is_open(int channel) {
  return valid_channel_id(channel) && InterlockedAnd(&ch[channel].open, 1);
}


void start_thread(int channel) {
  HANDLE handle;
  InterlockedBitTestAndSet(&ch[channel].thread_active, 0);
  handle = (HANDLE) _beginthread(wdspmain, 0, (void *)(uintptr_t)channel);
  if ((uintptr_t)handle == (uintptr_t) -1) {
    InterlockedBitTestAndReset(&ch[channel].run, 0);
    InterlockedBitTestAndReset(&ch[channel].thread_active, 0);
  }
  //SetThreadPriority(handle, THREAD_PRIORITY_HIGHEST);
}

void pre_main_build(int channel) {
  /* All three conversions were validated before building the channel. */
  scaled_size(ch[channel].dsp_size, ch[channel].dsp_rate, ch[channel].in_rate, &ch[channel].dsp_insize);
  scaled_size(ch[channel].dsp_size, ch[channel].dsp_rate, ch[channel].out_rate, &ch[channel].dsp_outsize);
  scaled_size(ch[channel].in_size, ch[channel].in_rate, ch[channel].out_rate, &ch[channel].out_size);
  InitializeCriticalSectionAndSpinCount(&ch[channel].csDSP, 2500);
  InitializeCriticalSectionAndSpinCount(&ch[channel].csEXCH,  2500);
  InterlockedBitTestAndReset(&ch[channel].flushflag, 0);
  create_iobuffs(channel);
}

void post_main_build(int channel) {
  InterlockedBitTestAndSet(&ch[channel].run, 0);
  start_thread(channel);
  if (ch[channel].state == 1) {
    InterlockedBitTestAndSet(&ch[channel].exchange, 0);
  }
}

void build_channel(int channel) {
  pre_main_build(channel);
  create_main(channel);
  post_main_build(channel);
}

PORT
void OpenChannel(int channel, int in_size, int dsp_size, int input_samplerate, int dsp_rate, int output_samplerate,
                 int type, int state, double tdelayup, double tslewup, double tdelaydown, double tslewdown, int bfo) {
  if (!valid_channel_id(channel) || channel_is_open(channel) ||
      !valid_channel_sizes_rates(in_size, dsp_size, input_samplerate, dsp_rate, output_samplerate) ||
      !valid_channel_type(type) || (state != 0 && state != 1) ||
      !valid_sample_time(tdelayup, input_samplerate) ||
      !valid_sample_time(tslewup, input_samplerate) ||
      !valid_sample_time(tdelaydown, output_samplerate) ||
      !valid_sample_time(tslewdown, output_samplerate)) {
    return;
  }
  ch[channel].in_size = in_size;
  ch[channel].dsp_size = dsp_size;
  ch[channel].in_rate = input_samplerate;
  ch[channel].dsp_rate = dsp_rate;
  ch[channel].out_rate = output_samplerate;
  ch[channel].type = type;
  ch[channel].state = state;
  ch[channel].tdelayup = tdelayup;
  ch[channel].tslewup = tslewup;
  ch[channel].tdelaydown = tdelaydown;
  ch[channel].tslewdown = tslewdown;
  ch[channel].bfo = bfo;
  InterlockedBitTestAndReset(&ch[channel].exchange, 0);
  build_channel(channel);
  InterlockedBitTestAndSet(&ch[channel].open, 0);
  if (ch[channel].state) {
    InterlockedBitTestAndSet(&ch[channel].iob.pc->slew.upflag, 0);
    InterlockedBitTestAndSet(&ch[channel].iob.ch_upslew, 0);
    InterlockedBitTestAndReset(&ch[channel].iob.pc->exec_bypass, 0);
    InterlockedBitTestAndSet(&ch[channel].exchange, 0);
  }
#ifdef _WIN32
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif
}

void pre_main_destroy(int channel) {
  IOB a = ch[channel].iob.pc;
  InterlockedBitTestAndReset(&ch[channel].exchange, 0);
  InterlockedBitTestAndReset(&ch[channel].run, 0);
  InterlockedBitTestAndSet(&ch[channel].iob.pc->exec_bypass, 0);
  ReleaseSemaphore(a->Sem_BuffReady, 1, 0);
  while (InterlockedAnd(&ch[channel].thread_active, 1)) {
    Sleep(1);
  }
}

void post_main_destroy(int channel) {
  destroy_iobuffs(channel);
  DeleteCriticalSection(&ch[channel].csEXCH);
  DeleteCriticalSection(&ch[channel].csDSP);
}

PORT
void CloseChannel(int channel) {
  if (!channel_is_open(channel)) { return; }
  pre_main_destroy(channel);
  destroy_main(channel);
  post_main_destroy(channel);
  InterlockedBitTestAndReset(&ch[channel].open, 0);
}

void flushChannel(void *p) {
  int channel = (int)(uintptr_t)p;
  IOB a = ch[channel].iob.pc;
  while (!InterlockedAnd(&a->flush_bypass, 0xffffffff)) {
    WaitForSingleObject(a->Sem_Flush, INFINITE);
    if (!InterlockedAnd(&a->flush_bypass, 0xffffffff)) {
      EnterCriticalSection(&ch[channel].csDSP);
      EnterCriticalSection(&ch[channel].csEXCH);
      flush_iobuffs(channel);
      InterlockedBitTestAndSet(&a->exec_bypass, 0);
      flush_main(channel);
      LeaveCriticalSection(&ch[channel].csEXCH);
      LeaveCriticalSection(&ch[channel].csDSP);
      InterlockedBitTestAndReset(&ch[channel].flushflag, 0);
    }
  }
  InterlockedBitTestAndReset(&a->flush_bypass, 0);
  InterlockedBitTestAndReset(&a->flush_thread_active, 0);
}

/********************************************************************************************************
*                                                   *
*                   Channel Properties                        *
*                                                   *
********************************************************************************************************/

PORT
void SetType(int channel, int type) {
  if (!channel_is_open(channel) || !valid_channel_type(type)) { return; }
  // no need to rebuild buffers; but we did anyway
  if (type != ch[channel].type) {
    CloseChannel(channel);
    ch[channel].type = type;
    build_channel(channel);
    InterlockedBitTestAndSet(&ch[channel].open, 0);
  }
}

PORT
void SetInputBuffsize(int channel, int in_size) {
  if (!channel_is_open(channel) ||
      !valid_channel_sizes_rates(in_size, ch[channel].dsp_size, ch[channel].in_rate, ch[channel].dsp_rate,
                                 ch[channel].out_rate)) { return; }
  // we do not rebuild main here since it didn't change
  if (in_size != ch[channel].in_size) {
    pre_main_destroy(channel);
    post_main_destroy(channel);
    ch[channel].in_size = in_size;
    pre_main_build(channel);
    post_main_build(channel);
  }
}

PORT
void SetDSPBuffsize(int channel, int dsp_size) {
  if (!channel_is_open(channel) ||
      !valid_channel_sizes_rates(ch[channel].in_size, dsp_size, ch[channel].in_rate, ch[channel].dsp_rate,
                                 ch[channel].out_rate)) { return; }
  if (dsp_size != ch[channel].dsp_size) {
    int oldstate = SetChannelState(channel, 0, 1);
    pre_main_destroy(channel);
    post_main_destroy(channel);
    ch[channel].dsp_size = dsp_size;
    pre_main_build(channel);
    setDSPBuffsize_main(channel);
    post_main_build(channel);
    SetChannelState(channel, oldstate, 0);
  }
}

PORT
void SetInputSamplerate(int channel, int in_rate) {
  if (!channel_is_open(channel) ||
      !(valid_sample_time(ch[channel].tdelayup, in_rate) &&
        valid_sample_time(ch[channel].tslewup, in_rate)) ||
      !valid_channel_sizes_rates(ch[channel].in_size, ch[channel].dsp_size, in_rate, ch[channel].dsp_rate,
                                 ch[channel].out_rate)) { return; }
  // no re-build of main required
  if (in_rate != ch[channel].in_rate) {
    pre_main_destroy(channel);
    post_main_destroy(channel);
    ch[channel].in_rate = in_rate;
    pre_main_build(channel);
    setInputSamplerate_main(channel);
    post_main_build(channel);
  }
}

PORT
void SetDSPSamplerate(int channel, int dsp_rate) {
  if (!channel_is_open(channel) ||
      !valid_channel_sizes_rates(ch[channel].in_size, ch[channel].dsp_size, ch[channel].in_rate, dsp_rate,
                                 ch[channel].out_rate)) { return; }
  if (dsp_rate != ch[channel].dsp_rate) {
    int oldstate = SetChannelState(channel, 0, 1);
    pre_main_destroy(channel);
    post_main_destroy(channel);
    ch[channel].dsp_rate = dsp_rate;
    pre_main_build(channel);
    setDSPSamplerate_main(channel);
    post_main_build(channel);
    SetChannelState(channel, oldstate, 0);
  }
}

PORT
void SetOutputSamplerate(int channel, int out_rate) {
  if (!channel_is_open(channel) ||
      !(valid_sample_time(ch[channel].tdelaydown, out_rate) &&
        valid_sample_time(ch[channel].tslewdown, out_rate)) ||
      !valid_channel_sizes_rates(ch[channel].in_size, ch[channel].dsp_size, ch[channel].in_rate, ch[channel].dsp_rate,
                                 out_rate)) { return; }
  // no re-build of main required
  if (out_rate != ch[channel].out_rate) {
    pre_main_destroy(channel);
    post_main_destroy(channel);
    ch[channel].out_rate = out_rate;
    pre_main_build(channel);
    setOutputSamplerate_main(channel);
    post_main_build(channel);
  }
}

PORT
void SetAllRates(int channel, int in_rate, int dsp_rate, int out_rate) {
  if (!channel_is_open(channel) ||
      !(valid_sample_time(ch[channel].tdelayup, in_rate) &&
        valid_sample_time(ch[channel].tslewup, in_rate) &&
        valid_sample_time(ch[channel].tdelaydown, out_rate) &&
        valid_sample_time(ch[channel].tslewdown, out_rate)) ||
      !valid_channel_sizes_rates(ch[channel].in_size, ch[channel].dsp_size, in_rate, dsp_rate, out_rate)) { return; }
  if ((in_rate != ch[channel].in_rate) || (dsp_rate != ch[channel].dsp_rate) || (out_rate != ch[channel].out_rate)) {
    pre_main_destroy(channel);
    post_main_destroy(channel);
    ch[channel].in_rate  = in_rate;
    ch[channel].dsp_rate = dsp_rate;
    ch[channel].out_rate = out_rate;
    pre_main_build(channel);
    setInputSamplerate_main(channel);
    setDSPSamplerate_main(channel);
    setOutputSamplerate_main(channel);
    post_main_build(channel);
  }
}

static int waitChannelFlush(int channel, int timeout_ms) {
  IOB a = ch[channel].iob.pc;
  int count = 0;
  while (_InterlockedAnd(&ch[channel].flushflag, 1) && count < timeout_ms) {
    Sleep(1);
    count++;
  }
  if (count >= timeout_ms) {
    InterlockedBitTestAndReset(&ch[channel].exchange, 0);
    InterlockedBitTestAndReset(&ch[channel].flushflag, 0);
    InterlockedBitTestAndReset(&a->slew.downflag, 0);
    return 0;
  }
  return 1;
}

PORT
int SetChannelState(int channel, int state, int dmode) {
  if (!channel_is_open(channel) || (state != 0 && state != 1)) { return 0; }
  IOB a = ch[channel].iob.pc;
  int prior_state = ch[channel].state;
  const int timeout = 100;
  if (ch[channel].state != state) {
    ch[channel].state = state;
    switch (ch[channel].state) {
    case 0:
      InterlockedBitTestAndSet(&a->slew.downflag, 0);
      InterlockedBitTestAndSet(&ch[channel].flushflag, 0);
      if (dmode) {
        waitChannelFlush(channel, timeout);
      }
      break;
    case 1:
      // Finish any pending turn-off before starting the turn-on slew.
      waitChannelFlush(channel, timeout);
      InterlockedBitTestAndSet(&a->slew.upflag, 0);
      InterlockedBitTestAndSet(&ch[channel].iob.ch_upslew, 0);
      InterlockedBitTestAndReset(&ch[channel].iob.pc->exec_bypass, 0);
      InterlockedBitTestAndSet(&ch[channel].exchange, 0);
      break;
    }
  } else if (!state && dmode) {
    waitChannelFlush(channel, timeout);
  }
  return prior_state;
}

PORT
void SetChannelTDelayUp(int channel, double time) {
  if (!channel_is_open(channel) || !valid_sample_time(time, ch[channel].in_rate)) { return; }
  IOB a;
  EnterCriticalSection(&ch[channel].csEXCH);
  a = ch[channel].iob.pc;
  ch[channel].tdelayup = time;
  a->slew.ndelup = (int)(ch[a->channel].tdelayup * ch[a->channel].in_rate);
  flush_slews(a);
  LeaveCriticalSection(&ch[channel].csEXCH);
}

PORT
void SetChannelTSlewUp(int channel, double time) {
  if (!channel_is_open(channel) || !valid_sample_time(time, ch[channel].in_rate)) { return; }
  IOB a;
  EnterCriticalSection(&ch[channel].csEXCH);
  a = ch[channel].iob.pc;
  ch[channel].tslewup = time;
  destroy_slews(a);
  create_slews(a);
  LeaveCriticalSection(&ch[channel].csEXCH);
}

PORT
void SetChannelTDelayDown(int channel, double time) {
  if (!channel_is_open(channel) || !valid_sample_time(time, ch[channel].out_rate)) { return; }
  IOB a;
  EnterCriticalSection(&ch[channel].csEXCH);
  a = ch[channel].iob.pc;
  ch[channel].tdelaydown = time;
  a->slew.ndeldown = (int)(ch[a->channel].tdelaydown * ch[a->channel].out_rate);
  flush_slews(a);
  LeaveCriticalSection(&ch[channel].csEXCH);
}

PORT
void SetChannelTSlewDown(int channel, double time) {
  if (!channel_is_open(channel) || !valid_sample_time(time, ch[channel].out_rate)) { return; }
  IOB a;
  EnterCriticalSection(&ch[channel].csEXCH);
  a = ch[channel].iob.pc;
  ch[channel].tslewdown = time;
  destroy_slews(a);
  create_slews(a);
  LeaveCriticalSection(&ch[channel].csEXCH);
}