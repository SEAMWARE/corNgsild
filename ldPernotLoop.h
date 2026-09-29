#ifndef CORNGSILD_LDPERNOTLOOP_H_
#define CORNGSILD_LDPERNOTLOOP_H_

//
// FILE            ldPernotLoop.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Background thread for periodic notification subscriptions.
//
#include "corTree/CorNode.h"                           // CorNode
#include "corNgsild/LdPernotCache.h"                    // LdPernotCache



// -----------------------------------------------------------------------------
//
// LdPernotQueryFunc - callback for querying entities
//
// The broker registers this at startup. The pernot loop calls it to get
// matching entities for a periodic subscription. Returns a CorArray of
// entities in storage format, or NULL if none match.
//
// tenantP:  opaque tenant pointer (from LdPernotItem.tenantP)
// itemP:    the pernot subscription item (carries entity selectors, q, etc.)
// allocP:   arena for the result tree
//
typedef CorNode* (*LdPernotQueryFunc)(void* tenantP, LdPernotItem* itemP, void* allocP);



//
// LdPernotCachesFn - the broker's pernot caches, one per tenant: call visit(cacheP, arg) for each
//
// The loop was started with ONE cache - tenant0's - so a periodic subscription on any other tenant
// was accepted, stored and listed, and never fired. The broker owns the tenants, including those
// created after startup, so the loop asks it for the caches on every tick.
//
typedef void (*LdPernotCacheVisitFn)(LdPernotCache* cacheP, void* arg);
typedef void (*LdPernotCachesFn)(LdPernotCacheVisitFn visit, void* arg);

// ldPernotLoopStart - register the periodic tick (the engine's thread runs it)
// cachesFn: broker-provided walk of every tenant's pernot cache
// queryFn:  broker-provided callback for entity queries
extern void ldPernotLoopStart(LdPernotCachesFn cachesFn, LdPernotQueryFunc queryFn);

// ldPernotLoopStop - signal the thread to stop
extern void ldPernotLoopStop(void);

#endif  // CORNGSILD_LDPERNOTLOOP_H_
