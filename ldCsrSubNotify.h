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

#endif  // SWNGSILD_LDCSRSUBNOTIFY_H_
