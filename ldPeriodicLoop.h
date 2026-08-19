#ifndef CORNGSILD_LDPERIODICLOOP_H_
#define CORNGSILD_LDPERIODICLOOP_H_

//
// FILE            ldPeriodicLoop.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Generic 1-Hz periodic-dispatch engine.
//
// Multiple subsystems need a "wake every second, walk a list of items,
// fire any whose timeInterval has elapsed" loop — periodic-notification
// subscriptions (§ 5.8 timeInterval) and CSR-Subscription periodic
// notifications (§ 5.11.7 timeInterval). They share the same cadence
// and the same throttling/cooldown/error-state shape.
//
// Rather than spawn a thread per consumer, each consumer registers a
// tick function. The engine spins one thread, calls every registered
// function once per second with a shared per-tick scratch allocator
// (reset before each call so callers don't leak across sources).
//
// Registration is one-shot at start-up; deregistration is not supported
// (consumers live for the broker's lifetime).
//
#include <stdint.h>                                    // uint64_t
#include "kalloc/KAlloc.h"                             // KAlloc


// -----------------------------------------------------------------------------
//
// LdPeriodicTickFn -
//
// Called once per second by the engine thread.
//   ctx   : the opaque pointer the consumer passed to ldPeriodicLoopRegister
//   nowNs : monotonic clock_gettime(CLOCK_REALTIME) at the start of this tick
//   kaP   : per-tick scratch allocator (reset before each callback)
//
typedef void (*LdPeriodicTickFn)(void* ctx, uint64_t nowNs, KAlloc* kaP);



// -----------------------------------------------------------------------------
//
// ldPeriodicLoopRegister - add a tick callback.
//
// Must be called BEFORE ldPeriodicLoopStart. Order of registration
// determines order of execution within each tick (early registrations
// run first).
//
extern void ldPeriodicLoopRegister(LdPeriodicTickFn fn, void* ctx);



// -----------------------------------------------------------------------------
//
// ldPeriodicLoopStart - spawn the dispatch thread.
//
// Idempotent: a second call is a no-op. Returns 0 on success, -1 if
// pthread_create fails.
//
extern int ldPeriodicLoopStart(void);



// -----------------------------------------------------------------------------
//
// ldPeriodicLoopStop - signal the thread to exit and join it.
//
// Safe to call when never started.
//
extern void ldPeriodicLoopStop(void);

#endif  // CORNGSILD_LDPERIODICLOOP_H_
