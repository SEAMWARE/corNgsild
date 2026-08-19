//
// FILE            ldPeriodicLoop.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <pthread.h>                                   // pthread_*
#include <stdbool.h>                                   // bool
#include <stdint.h>                                    // uint64_t
#include <stddef.h>                                    // NULL
#include <string.h>                                    // memset
#include <time.h>                                      // clock_gettime, nanosleep

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kalloc/kaBufferInit.h"                       // kaBufferInit
#include "kalloc/kaBufferReset.h"                      // kaBufferReset
#include "kjson/kjBufferCreate.h"                      // kjBufferCreate

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
  KAlloc          ka;
  struct timespec sleepTime = { 1, 0 };  // 1 second

  // Per-tick scratch allocator — reset before each callback so each
  // consumer starts clean.
  kaBufferInit(&ka, allocBuffer, sizeof(allocBuffer), 16384, NULL, "periodic");

  // Per-thread corRest init — many ngsild notification helpers
  // (ldCsrSubNotify, ldSubscriptionNotify) reach into corRest.kalloc /
  // corRest.kjsonP for builders. Init once; refresh requestStartTime per
  // tick. Reset corRest.kalloc per tick so it doesn't accumulate.
  memset(&corRest, 0, sizeof(corRest));
  kaBufferInit(&corRest.kalloc, corRest.kallocBuffer, sizeof(corRest.kallocBuffer),
               256 * 1024, NULL, "periodic-rest");
  corRest.kjsonP = kjBufferCreate(&corRest.kjson, &corRest.kalloc);

  while (loopRunning)
  {
    uint64_t now = nowNanos();
    corRest.requestStartTime = now;

    for (int i = 0; i < sourceCount; i++)
    {
      kaBufferReset(&ka, true);
      kaBufferReset(&corRest.kalloc, true);
      sources[i].fn(sources[i].ctx, now, &ka);
    }

    nanosleep(&sleepTime, NULL);
  }

  kaBufferReset(&ka, true);
  kaBufferReset(&corRest.kalloc, true);
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
  loopRunning = false;
  pthread_join(loopThread, NULL);
  threadStarted = false;
}
