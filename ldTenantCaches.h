#ifndef CORNGSILD_LDTENANTCACHES_H_
#define CORNGSILD_LDTENANTCACHES_H_

//
// FILE            ldTenantCaches.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "corNgsild/LdSubCache.h"                       // LdSubCache
#include "corNgsild/LdRegCache.h"                       // LdRegCache



// -----------------------------------------------------------------------------
//
// LdTenantCaches - one tenant's caches, as the broker hands them to a periodic tick
//
// The periodic ticks (the throttle flush, periodic CSR-subscriptions) were registered with
// tenant0's caches alone, so on any other tenant a throttled subscription's coalesced
// notification never came and a periodic CSR-subscription never fired. The broker owns the
// tenants - including those created after startup - so a tick asks it for every tenant's caches
// on each run: LdTenantCachesFn calls visit once per tenant.
//
typedef struct LdTenantCaches
{
  void*        tenantP;           // opaque - handed back to the broker's callbacks (retrieve, ...)
  LdSubCache*  subCacheP;         // entity subscriptions
  LdSubCache*  regSubCacheP;      // CSR-subscriptions
  LdRegCache*  regCacheP;         // registrations
} LdTenantCaches;

typedef void (*LdTenantCachesVisitFn)(LdTenantCaches* tcP, void* arg);
typedef void (*LdTenantCachesFn)(LdTenantCachesVisitFn visit, void* arg);

#endif  // CORNGSILD_LDTENANTCACHES_H_
