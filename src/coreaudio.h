/* Copyright (C)
* 2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
* Native CoreAudio output backend.
*/

#ifndef _COREAUDIO_H
#define _COREAUDIO_H

#include "receiver.h"

extern void *coreaudio_output_open(RECEIVER *rx, const char *device_name, int *channels);
extern void coreaudio_output_close(void *handle);

extern void *coreaudio_input_open(const char *device_name);
extern void coreaudio_input_close(void *handle);

extern void *coreaudio_tci_monitor_open(const char *device_name, int *channels);
extern void coreaudio_tci_monitor_close(void *handle);

extern int coreaudio_get_cards(void);

#endif
