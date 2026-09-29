//
// FILE            ldIdGenerate.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdio.h>                                   // snprintf
#include <time.h>                                    // time

#include "corAlloc/corAlloc.h"                       // corAlloc

#include "corNgsild/ldIdGenerate.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// idCounter - one for the whole process
//
// There were seven, each a `static int counter` of its own, incremented with a plain ++ by any
// number of threads at once: two POSTs in the same second could get the same number, the same
// id, and the second one a 409 for a resource the client never created. Two of them also
// formatted into a shared static buffer, so a notification id could come out garbled. And
// /subscriptions and /csourceSubscriptions both make "urn:ngsi-ld:Subscription:" ids, from
// separate counters, while sharing one id space (one collection): an entity subscription and
// a CSR-subscription created in the same second collided even on a single thread - as did the
// three notification-id counters. One counter, incremented atomically; no id repeats within a
// process.
//
static unsigned int idCounter = 0;



// -----------------------------------------------------------------------------
//
// ldIdGenerate -
//
char* ldIdGenerate(CorAlloc* allocP, const char* kind)
{
  unsigned int n   = __atomic_add_fetch(&idCounter, 1, __ATOMIC_RELAXED);
  char*        buf = corAlloc(allocP, 128);

  if (buf != NULL)
    snprintf(buf, 128, "urn:ngsi-ld:%s:%lx:%04x", kind, (long) time(NULL), n & 0xFFFF);

  return buf;
}
