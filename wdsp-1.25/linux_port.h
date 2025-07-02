/*  linux_port.h

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

#if defined(linux) || defined(__APPLE__)


#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define CRITICAL_SECTION pthread_mutex_t
#define byte unsigned char
#define String char *
#define LONG long
#define DWORD long
#define HANDLE void *
#define WINAPI
#define FALSE 0
#define TRUE 1
#define TEXT(x) x
#define InterlockedIncrement(base) __sync_add_and_fetch(base,1L)
#define InterlockedDecrement(base) __sync_sub_and_fetch(base,1L)

//#define InterlockedBitTestAndSet(base,bit) __sync_or_and_fetch(base,1L<<bit)
//#define InterlockedBitTestAndReset(base,bit) __sync_and_and_fetch(base,~(1L<<bit))

#define InterlockedBitTestAndSet(base,bit) __sync_fetch_and_or(base,1L<<bit)
#define InterlockedBitTestAndReset(base,bit) __sync_fetch_and_and(base,~(1L<<bit))

#define InterlockedExchange(target,value) __sync_lock_test_and_set(target,value)
#define InterlockedAnd(base,mask) __sync_fetch_and_and(base,mask)
#define _InterlockedAnd(base,mask) __sync_fetch_and_and(base,mask)
#define __declspec(x)
#define __cdecl
#define __stdcall
#define __forceinline

#define _aligned_malloc(x,y) malloc(x)
#define _aligned_free(x)     free(x)
// Activate these for malloc debug
//#define _aligned_malloc(x,y) my_malloc(x);
//#define _aligned_free(x) my_free(x);

void *my_malloc(size_t size);
void my_free(void *p);

#define freopen_s freopen
#define min(x,y) (x<y?x:y)
#define max(x,y) (x<y?y:x)
#define THREAD_PRIORITY_HIGHEST 0

#define Sleep(ms) usleep((ms)*1000)

#define CreateSemaphore(a,b,c,d) LinuxCreateSemaphore(a,b,c,d)
#define WaitForSingleObject(x, y) LinuxWaitForSingleObject(x, y)
#define ReleaseSemaphore(x,y,z) LinuxReleaseSemaphore(x,y,z)
#define SetEvent(x) LinuxSetEvent(x)
#define ResetEvent(x) LinuxResetEvent(x)

#define INFINITE -1

void QueueUserWorkItem(void *function,void *context,int flags);

void InitializeCriticalSectionAndSpinCount(pthread_mutex_t *mutex,int count);

void EnterCriticalSection(pthread_mutex_t *mutex);

void LeaveCriticalSection(pthread_mutex_t *mutex);

void DeleteCriticalSection(pthread_mutex_t *mutex);


sem_t *LinuxCreateSemaphore(int attributes,int initial_count,int maximum_count,char *name);

int LinuxWaitForSingleObject(sem_t *sem,int x);

void LinuxReleaseSemaphore(sem_t *sem,int release_count, int* previous_count);

sem_t *CreateEvent(void* security_attributes,int bManualReset,int bInitialState,char* name);

void LinuxSetEvent(sem_t* sem);
void LinuxResetEvent(sem_t* sem);

HANDLE _beginthread( void( __cdecl *start_address )( void * ), unsigned stack_size, void *arglist);

void _endthread();

void SetThreadPriority(HANDLE thread, int priority);

void CloseHandle(HANDLE hObject);

#endif

