/* Copyright (C)
* 2024-2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
* SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef DESKHPSDR_SEM_UTILS_H
#define DESKHPSDR_SEM_UTILS_H

#include <errno.h>
#include <semaphore.h>

/*
 * POSIX sem_wait() may return early when interrupted by a signal.  Callers
 * must not continue as if the semaphore had been acquired in that case.
 */
static inline int sem_wait_nointr(sem_t *sem) {
  int rc;
  do {
    rc = sem_wait(sem);
  } while (rc == -1 && errno == EINTR);
  return rc;
}

#endif
