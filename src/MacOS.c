/* Copyright (C)
* 2021 - Christoph van Wüllen, DL1YCF
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

/*
 * File MacOS.c
 *
 * This file need only be compiled on MacOS.
 * It contains some functions only needed there:
 *
 * apple_sem(int init)          : return a pointer to a semaphore
 *
 */

#ifdef __APPLE__

#include <stdio.h>
#include <semaphore.h>
#include <errno.h>

#include "message.h"

/////////////////////////////////////////////////////////////////////////////
//
// MacOS semaphores
//
// Since MacOS only supports named semaphores, we have to be careful to
// allow serveral instances of this program to run at the same time on the
// same machine
//
/////////////////////////////////////////////////////////////////////////////

sem_t *apple_sem(int initial_value) {
  sem_t *sem;
  static long semcount = 0;
  char sname[20];

  for (;;) {
    snprintf(sname, 20, "PI_%08ld", semcount++);
    sem = sem_open(sname, O_CREAT | O_EXCL, 0700, initial_value);

    //
    // This can happen if a semaphore of that name is already in use,
    // for example by another SDR program running on the same machine
    //
    if (sem == SEM_FAILED && errno == EEXIST) { continue; }

    break;
  }

  if (sem == SEM_FAILED) {
    t_perror("NewProtocol:SemOpen");
    exit (-1);
  }

  // we can unlink the semaphore NOW. It will remain functional
  // until sem_close() has been called by all threads using that
  // semaphore.
  sem_unlink(sname);
  return sem;
}

#endif
