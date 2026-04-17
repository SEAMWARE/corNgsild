#ifndef SWNGSILD_LDPERNOTLOOP_H_
#define SWNGSILD_LDPERNOTLOOP_H_

//
// FILE            ldPernotLoop.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Background thread for periodic notification subscriptions.
//
#include "kjson/KjNode.h"                              // KjNode
#include "swNgsild/LdPernotCache.h"                    // LdPernotCache



// -----------------------------------------------------------------------------
//
// LdPernotQueryFunc - callback for querying entities
//
// The broker registers this at startup. The pernot loop calls it to get
// matching entities for a periodic subscription. Returns a KjArray of
// entities in storage format, or NULL if none match.
//
// tenantP:  opaque tenant pointer (from LdPernotItem.tenantP)
// itemP:    the pernot subscription item (carries entity selectors, q, etc.)
// allocP:   arena for the result tree
//
typedef KjNode* (*LdPernotQueryFunc)(void* tenantP, LdPernotItem* itemP, void* allocP);



// ldPernotLoopStart - launch the background timer thread
// cacheP:   the global pernot cache to scan
// queryFn:  broker-provided callback for entity queries
extern void ldPernotLoopStart(LdPernotCache* cacheP, LdPernotQueryFunc queryFn);

// ldPernotLoopStop - signal the thread to stop
extern void ldPernotLoopStop(void);

#endif  // SWNGSILD_LDPERNOTLOOP_H_
