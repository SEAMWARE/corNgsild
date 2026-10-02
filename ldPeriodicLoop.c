//
// FILE            ldPeriodicLoop.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <pthread.h>                                   // pthread_*
#include <stdbool.h>                                   // bool
#include <stdint.h>                                    // uint64_t
#include <stddef.h>                                    // NULL
#include <string.h>                                    // memset
#include <time.h>                                      // clock_gettime

#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corAlloc/corAllocBufferInit.h"               // corAllocBufferInit
#include "corAlloc/corAllocBufferReset.h"              // corAllocBufferReset
#include "corJson/corJsonCreate.h"                     // corJsonCreate

#include "corRest/CorRestState.h"                        // corRest (__thread)

#include "corNgsild/ldPeriodicLoop.h"                   // Own interface


//
// Engine state. The slot table is fixed-size; eight tickables is more
// than the broker will ever need (subscriptions / CSR-subs / sub-stats
// flush / metrics flush — all of those plus headroom).
//
#define LD_PERIODIC_MAX_SOURCES 8

typedef struct
{
  LdPeriodicTickFn  fn;
  void*             ctx;
} PeriodicSource;

static PeriodicSource     sources[LD_PERIODIC_MAX_SOURCES];
static int                sourceCount = 0;
static volatile bool      loopRunning = false;
static pthread_t          loopThread;
static bool               threadStarted = false;

//
// The tick's wait: on a condition, not nanosleep - ldPeriodicLoopStop wakes it at once instead of
// waiting out the second (every broker stop paid ~0.5 s for it, every functest at least one stop)
//
static pthread_mutex_t    tickMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t     tickCond;
static pthread_once_t     tickOnce  = PTHREAD_ONCE_INIT;

static void tickCondInit(void)
{
  pthread_condattr_t attr;

  pthread_condattr_init(&attr);
  pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  pthread_cond_init(&tickCond, &attr);
  pthread_condattr_destroy(&attr);
}



//
// nowNanos -
//
static uint64_t nowNanos(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}



//
// dispatchThread -
//
static void* dispatchThread(void* unused)
{
  (void) unused;

  char            allocBuffer[8192];
  CorAlloc        ka;

  // Per-tick scratch allocator — reset before each callback so each
  // consumer starts clean.
  corAllocBufferInit(&ka, allocBuffer, sizeof(allocBuffer), 16384, NULL, "periodic");

  // Per-thread corRest init — many ngsild notification helpers
  // (ldCsrSubNotify, ldSubscriptionNotify) reach into corRest.kalloc /
  // corRest.kallocP for builders. Init once; refresh requestStartTime per
  // tick. Reset corRest.kalloc per tick so it doesn't accumulate.
  memset(&corRest, 0, sizeof(corRest));
  corAllocBufferInit(&corRest.kalloc, corRest.kallocBuffer, sizeof(corRest.kallocBuffer),
                     256 * 1024, NULL, "periodic-rest");
  corRest.corJsonP = corJsonCreate(&corRest.corJson, &corRest.kalloc);
  corRest.kallocP  = &corRest.kalloc;

  while (loopRunning)
  {
    uint64_t now = nowNanos();
    corRest.requestStartTime = now;

    for (int i = 0; i < sourceCount; i++)
    {
      corAllocBufferReset(&ka, true);
      corAllocBufferReset(&corRest.kalloc, true);
      sources[i].fn(sources[i].ctx, now, &ka);
    }

    //
    // One second, or until ldPeriodicLoopStop
    //
    struct timespec until;

    clock_gettime(CLOCK_MONOTONIC, &until);
    until.tv_sec += 1;

    pthread_mutex_lock(&tickMutex);
    while ((loopRunning == true) && (pthread_cond_timedwait(&tickCond, &tickMutex, &until) == 0))
      ;
    pthread_mutex_unlock(&tickMutex);
  }

  corAllocBufferReset(&ka, true);
  corAllocBufferReset(&corRest.kalloc, true);
  return NULL;
}



void ldPeriodicLoopRegister(LdPeriodicTickFn fn, void* ctx)
{
  if (fn == NULL) return;
  if (sourceCount >= LD_PERIODIC_MAX_SOURCES) return;  // silent overflow guard

  sources[sourceCount].fn  = fn;
  sources[sourceCount].ctx = ctx;
  sourceCount++;
}



int ldPeriodicLoopStart(void)
{
  if (threadStarted) return 0;
  pthread_once(&tickOnce, tickCondInit);
  loopRunning   = true;
  threadStarted = (pthread_create(&loopThread, NULL, dispatchThread, NULL) == 0);
  if (!threadStarted)
  {
    loopRunning = false;
    return -1;
  }
  return 0;
}



void ldPeriodicLoopStop(void)
{
  if (!threadStarted) return;

  pthread_mutex_lock(&tickMutex);
  loopRunning = false;
  pthread_cond_signal(&tickCond);
  pthread_mutex_unlock(&tickMutex);

  pthread_join(loopThread, NULL);
  threadStarted = false;
}
