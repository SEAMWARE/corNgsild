//
// FILE            ldStatsFlushLoop.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <pthread.h>                                  // pthread_create
#include <stdbool.h>                                  // bool
#include <stddef.h>                                   // NULL
#include <time.h>                                     // nanosleep

#include "corNgsild/ldStatsFlushLoop.h"                // Own interface



static pthread_t          loopThread;
static bool               loopRunning = false;
static bool               loopStarted = false;
static int                loopIntervalSec = 0;
static LdStatsFlushAllFn  loopFlushFn = NULL;



// -----------------------------------------------------------------------------
//
// flushLoopThread -
//
static void* flushLoopThread(void* vP)
{
  (void) vP;

  // Sleep in 1-second slices so a stop request takes effect quickly
  // (up to 1 second latency), regardless of the configured interval.
  struct timespec slice = { 1, 0 };
  int elapsed = 0;

  while (loopRunning)
  {
    nanosleep(&slice, NULL);
    if (!loopRunning) break;

    elapsed++;
    if (elapsed < loopIntervalSec)
      continue;
    elapsed = 0;

    if (loopFlushFn != NULL)
      loopFlushFn();
  }
  return NULL;
}



// -----------------------------------------------------------------------------
//
// ldStatsFlushLoopStart -
//
void ldStatsFlushLoopStart(int intervalSec, LdStatsFlushAllFn flushFn)
{
  if (loopStarted)      return;
  if (intervalSec <= 0) return;
  if (flushFn == NULL)  return;

  loopIntervalSec = intervalSec;
  loopFlushFn     = flushFn;
  loopRunning     = true;
  loopStarted     = true;

  pthread_create(&loopThread, NULL, flushLoopThread, NULL);
  pthread_detach(loopThread);
}



// -----------------------------------------------------------------------------
//
// ldStatsFlushLoopStop -
//
void ldStatsFlushLoopStop(void)
{
  loopRunning = false;
}
