/*  linux_port.c

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013 Warren Pratt, NR0V and John Melton, G0ORX/N6LYT

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
john.d.melton@googlemail.com

*/

#include <errno.h>

#include "linux_port.h"
#include "comm.h"

/********************************************************************************************************
*                                                                                                       *
*   Linux Port Utilities                                                                                *
*                                                                                                       *
********************************************************************************************************/

#if defined(linux) || defined(__APPLE__)

void QueueUserWorkItem(void *function,void *context,int flags) {
    pthread_t t;
    pthread_create(&t, NULL, function, context);
    pthread_join(t, NULL);
}

void InitializeCriticalSectionAndSpinCount(pthread_mutex_t *mutex,int count) {
    pthread_mutexattr_t mAttr;
    pthread_mutexattr_init(&mAttr);
#ifdef __APPLE__
    // DL1YCF: MacOS X does not have PTHREAD_MUTEX_RECURSIVE_NP
    pthread_mutexattr_settype(&mAttr,PTHREAD_MUTEX_RECURSIVE);
#else
    pthread_mutexattr_settype(&mAttr,PTHREAD_MUTEX_RECURSIVE_NP);
#endif
    pthread_mutex_init(mutex,&mAttr);
    pthread_mutexattr_destroy(&mAttr);
    // ignore count
}

void EnterCriticalSection(pthread_mutex_t *mutex) {
    pthread_mutex_lock(mutex);
}

void LeaveCriticalSection(pthread_mutex_t *mutex) {
    pthread_mutex_unlock(mutex);
}

void DeleteCriticalSection(pthread_mutex_t *mutex) {
    pthread_mutex_destroy(mutex);
}

int LinuxWaitForSingleObject(sem_t *sem,int ms) {
    int result=0;
    if(ms==INFINITE) {
        // wait for the lock
        result=sem_wait(sem);
    } else {
        for (int i = 0; i < ms; i++) {
          result=sem_trywait(sem);
          if (result == 0) break;
          Sleep(1);
        }
    }

    return result;
}

sem_t *LinuxCreateSemaphore(int attributes,int initial_count,int maximum_count,char *name) {
        sem_t *sem;
#ifdef __APPLE__
    //
    // DL1YCF
    // This routine is usually invoked with name=NULL, so we have to make
    // a unique name of tpye WDSPxxxxxxxx for each invocation, since MacOS only
    // supports named semaphores. We shall unlink in due course, but first we
    // need to check whether the name is possibly already in use, e.g. by
    // another SDR program running on the same machine.
    //
    static long semcount=0;
    char sname[20];
        for (;;) {
          sprintf(sname,"WDSP%08ld",semcount++);
          sem=sem_open(sname, O_CREAT | O_EXCL, 0700, initial_count);
          if (sem == SEM_FAILED && errno == EEXIST) continue;
          break;
        }
    if (sem == SEM_FAILED) {
      perror("WDSP:CreateSemaphore");
    }
    //
    // we can unlink the semaphore NOW. It will remain functional
    // until sem_close() has been called by all threads using that
    // semaphore.
    //
    sem_unlink(sname);
#else
    sem=malloc0(sizeof(sem_t));
    int result;
    // DL1YCF: added correct initial count
    result=sem_init(sem, 0, initial_count);
    if (result < 0) {
      perror("WDSP:CreateSemaphore");
    }
#endif
    return sem;
}

void LinuxReleaseSemaphore(sem_t* sem,int release_count, int* previous_count) {
    //
    // Note WDSP always calls this with previous_count==NULL
    // so we do not bother about obtaining the previous value and
    // storing it in *previous_count.
    //
    while(release_count>0) {
        sem_post(sem);
        release_count--;
    }
}

sem_t *CreateEvent(void* security_attributes,int bManualReset,int bInitialState,char* name) {
    //
    // This is always called with bManualReset = bInitialState = FALSE
    //
    sem_t *sem;
    sem=LinuxCreateSemaphore(0,0,0,0);
    return sem;
}

void LinuxSetEvent(sem_t* sem) {
    //
    // WDSP uses this to set the semaphore (event) to
    // a "releasing" state.
    // we simulate this by posting
    sem_post(sem);
}

void LinuxResetEvent(sem_t* sem) {
    //
    // WDSP uses this to set the semaphore (event) to
    // a blocking state.
    // We mimic this by calling sem_trywait as long as it succeeds
    //
    while (sem_trywait(sem) == 0) ;
}

HANDLE _beginthread( void( __cdecl *start_address )( void * ), unsigned stack_size, void *arglist) {
    pthread_t threadid;
    pthread_attr_t  attr;

    if (pthread_attr_init(&attr)) {
        return (HANDLE)-1;
    }

    if(stack_size!=0) {
        if (pthread_attr_setstacksize(&attr, stack_size)) {
            return (HANDLE)-1;
        }
    }

    if(pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED)) {
        return (HANDLE)-1;
    }

    if (pthread_create(&threadid, &attr, (void*(*)(void*))start_address, arglist)) {
         return (HANDLE)-1;
    }

    //pthread_attr_destroy(&attr);
#ifndef __APPLE__
    // DL1YCF: this function does not exist on MacOS. You can only name the
    //         current thread.
    //         If this call should fail, continue anyway.
    (void) pthread_setname_np(threadid, "WDSP");
#endif

    return (HANDLE)threadid;

}

void _endthread() {
    pthread_exit(NULL);
}

void SetThreadPriority(HANDLE thread, int priority)  {
//
// In Linux, the scheduling priority only affects
// real-time threads (SCHED_FIFO, SCHED_RR), so this
// is basically a no-op here.
//
/*
    int policy;
    struct sched_param param;

    pthread_getschedparam(thread, &policy, &param);
    param.sched_priority = sched_get_priority_max(policy);
    pthread_setschedparam(thread, policy, &param);
*/
}

void CloseHandle(HANDLE hObject) {
//
// This routine is *ONLY* called to release semaphores
// The WDSP transmitter thread terminates upon each TX/RX
// transition, where it closes and re-opens a semaphore
// in flush_buffs() in iobuffs.c. Therefore, we have to
// release any resource associated with this semaphore, which
// may be a small memory patch (LINUX) or a file descriptor
// (MacOS).
//
#ifdef __APPLE__
if (sem_close((sem_t *)hObject) < 0) {
  perror("WDSP:CloseHandle:SemCLose");
}
#else
if (sem_destroy((sem_t *)hObject) < 0) {
  perror("WDSP:CloseHandle:SemDestroy");
} else {
  // if sem_destroy failed, do not release storage
  _aligned_free(hObject);
}
#endif

return;
}

//////////////////////////////////////////////////////////////////////////////////////////
//
// MALLOC debug facility.
// In the header file (linux_port.h), one can #define
//
//  _aligned_malloc(a,b) ==>  my_malloc(a)
//  _aligned_free(a)     ==>  my_free(a)
//
// Then all memory allocations/deallocations will be done via my_malloc() my_free()
// Note this is thread-safe, since an explicit mutex is used.
//
// my_alloc will build a "fence", 1k wide, to both sides of the allocated area,
// and fill it with some bit pattern.
//
// my_free will check for the integrity of the "fence" and report how many bytes
// in the upper and lower fence have illegally been changed
//
// furthermore, my_free will complain (and terminate the program) if its argument
// does not point to an active memory block allocated with my_malloc.
//
// P.S.1: Using "valgrind" with such time-critical programs is not a good idea,
//        so here is a solution.
//
// P.S.2: Further extensions are possible, e.g. include __FUNCTION__ and __LINE__
//        in the argument list of my_malloc(), store this in MEM_LIST, and report
//        upon failure.
//
// P.S.3: The standard definitions in linux_port.h are
//
//        __aligned_malloc(a,b) ==>   malloc(a)
//        __aligned_free(a)     ==>   free(a)
//
//        and with these, "MALLOC debug" code is not used.
//
//////////////////////////////////////////////////////////////////////////////////////////

static pthread_mutex_t malloc_mutex = PTHREAD_MUTEX_INITIALIZER;

struct _MEM_LIST {
  void *baseptr;
  void *freeptr;
  size_t size;
  int in_use;
};

typedef struct _MEM_LIST MEM_LIST;

#define MEM_LIST_SIZE 32768

MEM_LIST malloc_slot[MEM_LIST_SIZE] = {0};

void *my_malloc(size_t size) {
  int slot;
  void *baseptr, *freeptr;;
  uint8_t *p1, *p2;

  pthread_mutex_lock(&malloc_mutex);
  //
  // locate a free slot
  //
  slot=-1;
  for (int i=0; i<MEM_LIST_SIZE; i++) {
    if (malloc_slot[i].in_use == 0) {
      slot=i;
      break;
    }
  }
  if (slot < 0) {
    fprintf(stderr,"my_malloc: All Slots Exhausted.\n");
    fflush(stderr);
    pthread_mutex_unlock(&malloc_mutex);
    _exit(8);
  }
  baseptr=malloc(size+2048);
  if (baseptr == NULL) { return NULL; }

  freeptr=baseptr+1024;

  malloc_slot[slot].in_use = 1;
  malloc_slot[slot].baseptr = baseptr;
  malloc_slot[slot].freeptr = freeptr;
  malloc_slot[slot].size    = size;

  //
  // Create a "fence" around the allocated area
  //
  p1 = baseptr;
  p2 = freeptr + size;

  for (int i=0; i<256; i++) {
    *p1++ = 0xAA;
    *p1++ = 0x55;
    *p1++ = 0xEF;
    *p1++ = 0xFE;
    *p2++ = 0xAA;
    *p2++ = 0x55;
    *p2++ = 0xEF;
    *p2++ = 0xFE;
  }
  pthread_mutex_unlock(&malloc_mutex);
  //fprintf(stderr,"my_malloc: Allocated Block slot=%d addr=%p\n", slot, freeptr);
  return freeptr;
}

void my_free(void *ptr) {
  int slot;
  uint8_t *p1, *p2;

  pthread_mutex_lock(&malloc_mutex);
  //
  // Search for block
  //
  slot=-1;
  for (int i=0; i<4096; i++) {
    if (malloc_slot[i].in_use == 1 && malloc_slot[i].freeptr == ptr) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    fprintf(stderr,"my_free: Trying to free non-allocated block at addr=%p\n",ptr);
    fflush(stderr);
    pthread_mutex_unlock(&malloc_mutex);
    _exit(8);
  }
  //
  // Verify integrity of fence
  //
  int under_count=0;
  int over_count=0;

  p1 = malloc_slot[slot].baseptr;
  p2 = malloc_slot[slot].freeptr+malloc_slot[slot].size;

  for (int i=0; i<256; i++) {
    if (*p1++ != 0xAA) under_count++;
    if (*p1++ != 0x55) under_count++;
    if (*p1++ != 0xEF) under_count++;
    if (*p1++ != 0xFE) under_count++;
    if (*p2++ != 0xAA) over_count++;
    if (*p2++ != 0x55) over_count++;
    if (*p2++ != 0xEF) over_count++;
    if (*p2++ != 0xFE) over_count++;
  }
  if (under_count > 0) {
    fprintf(stderr,"WARNING: my_free: Fence underrun =%d\n", under_count);
  }
  if (over_count > 0) {
    fprintf(stderr,"WARNING: my_free: Fence overrun =%d\n", over_count);
  }
  if (over_count > 0 || under_count > 0) {
    fprintf(stderr,"WARNING: my_free: Block slot=%d size=%ld allocated addr=%p\n", slot,
                  (long) malloc_slot[slot].size, malloc_slot[slot].freeptr);
  }
  free(malloc_slot[slot].baseptr);
  malloc_slot[slot].in_use=0;

  pthread_mutex_unlock(&malloc_mutex);
}

#endif
