#ifndef SWNGSILD_LDCSRSUBNOTIFY_H_
#define SWNGSILD_LDCSRSUBNOTIFY_H_

//
// FILE            ldCsrSubNotify.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// CSR-Subscription matcher + CsourceNotification (§ 5.3.2) sender.
//
// The matcher walks per-tenant LdRegCache, comparing a CSR-sub's
// entity selector / watchedAttributes / notification.attributes
// against each LdRegInfo per § 5.12. Matching CSRs are POSTed to
// the sub's endpoint as a single CsourceNotification with the
// given triggerReason.
//
#include <stdbool.h>                                    // bool

#include "kalloc/KAlloc.h"                              // KAlloc

#include "swNgsild/LdSubCache.h"                        // LdSubCacheItem
#include "swNgsild/LdRegCache.h"                        // LdRegCache



// -----------------------------------------------------------------------------
//
// ldCsrSubInitialNotify - dispatch the § 5.11.2.4 initial notification
//
// Called from postCsourceSubscriptions right after the sub is added
// to regSubCacheP. Finds all currently-registered CSRs in regCacheP
// that match this sub and fires a single CsourceNotification with
// triggerReason="newlyMatching". No-op when zero matches.
//
extern void ldCsrSubInitialNotify(LdRegCache* regCacheP, LdSubCacheItem* subItemP);



// -----------------------------------------------------------------------------
//
// ldCsrSubOnRegCreate - dispatch § 5.11.7 notifications on CSR creation
//
// Called from postCsourceRegistration after the new CSR is added to the
// reg cache. Walks regSubCacheP; for each CSR-sub that matches the new
// CSR per § 5.12, fires a single CsourceNotification carrying the CSR
// with triggerReason="newlyMatching".
//
extern void ldCsrSubOnRegCreate(LdSubCache* regSubCacheP, LdRegCacheItem* regItemP);



// -----------------------------------------------------------------------------
//
// ldCsrSubOnRegDelete - dispatch § 5.11.7 notifications on CSR deletion
//
// Called from deleteCsourceRegistration BEFORE the item is removed
// from the reg cache — the matcher reads regItemP's pre-parsed fields,
// which are freed by ldRegCacheItemRemove.
//
// Walks regSubCacheP; for each CSR-sub that matches the CSR about to
// be deleted, fires a single CsourceNotification carrying the CSR
// with triggerReason="noLongerMatching".
//
extern void ldCsrSubOnRegDelete(LdSubCache* regSubCacheP, LdRegCacheItem* regItemP);



// -----------------------------------------------------------------------------
//
// ldCsrSubMatchingSubIds - snapshot sub ids matching a CSR
//
// Returns a NULL-terminated char** (each entry borrowed from the sub
// cache, stable for cache lifetime) allocated in allocP. NULL when no
// subs match.
//
// Used to capture the "was matching" set before a CSR update so that
// ldCsrSubOnRegUpdate can emit the right triggerReason per sub.
//
extern char** ldCsrSubMatchingSubIds(LdSubCache* regSubCacheP, LdRegCacheItem* regItemP, KAlloc* allocP);



// -----------------------------------------------------------------------------
//
// ldCsrSubOnRegUpdate - § 5.11.7 fan-out on CSR update
//
// Called from patchCsourceRegistration AFTER the reg cache has been
// refreshed with the post-update tree. Classifies each CSR-sub by
// comparing "was matching" (wasMatchingIds, captured before the
// update) with "now matches" (evaluated against regItemAfterP):
//
//   was && now    -> "updated"
//   !was && now   -> "newlyMatching"
//   was && !now   -> "noLongerMatching"
//
// data[] always carries regItemAfterP's tree — the authoritative
// current state of the CSR.
//
extern void ldCsrSubOnRegUpdate(LdSubCache* regSubCacheP,
                                LdRegCacheItem* regItemAfterP,
                                char** wasMatchingIds);



// -----------------------------------------------------------------------------
//
// ldCsrSubPeriodicLoopRegister - § 5.11.7 periodic CSR-Sub notifications.
//
// Registers a tick callback with the shared periodic-dispatch engine
// (ldPeriodicLoop). Each second, the engine fires the callback which
// walks regSubCacheP looking for CSR-Subs with timeInterval > 0 whose
// interval has elapsed; for each such sub it collects the matching
// CSRs from regCacheP and POSTs a CsourceNotification (triggerReason
// "updated").
//
// Single-tenant (broker-wide tenant0). Multi-tenant would either
// register once per tenant or look the right caches up via subItem
// tenantP — defer.
//
extern void ldCsrSubPeriodicLoopRegister(LdSubCache* regSubCacheP, LdRegCache* regCacheP);

#endif  // SWNGSILD_LDCSRSUBNOTIFY_H_
