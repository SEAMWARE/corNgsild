#ifndef SWNGSILD_LDSUBSCRIPTIONNOTIFY_H_
#define SWNGSILD_LDSUBSCRIPTIONNOTIFY_H_

//
// FILE            ldSubscriptionNotify.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Subscription matching and notification delivery.
//
// The service routine defers work via ldNotifyDefer(); after the HTTP
// response has been sent, the post-response hook calls
// ldSubscriptionNotifyBatch() which matches every deferred entry
// against every subscription in the cache, then emits one notification
// per sub — whose `data[]` array carries every matched entity in the
// order they were deferred.
//
#include <stdbool.h>
#include <stdint.h>                                    // uint64_t
#include <string.h>                                    // strcmp (for inline helpers)

#include "kjson/KjNode.h"

#include "swNgsild/ldEntityMerge.h"                    // LdMergeReport
#include "swNgsild/LdSubCache.h"                       // LdSubCache



// -----------------------------------------------------------------------------
//
// LdNotifyOp - the type of entity operation that triggered the notification
//
typedef enum LdNotifyOp
{
  LdNotifyEntityCreate,
  LdNotifyEntityUpdate,
  LdNotifyEntityDelete
} LdNotifyOp;



// -----------------------------------------------------------------------------
//
// LdTrigger - bitmask for notificationTrigger
//
#define LD_TRIGGER_ENTITY_CREATED     0x01
#define LD_TRIGGER_ENTITY_UPDATED     0x02
#define LD_TRIGGER_ENTITY_DELETED     0x04
#define LD_TRIGGER_ATTR_CREATED       0x08
#define LD_TRIGGER_ATTR_MODIFIED      0x10
#define LD_TRIGGER_ATTR_DELETED       0x20

// Default per § 5.2.12: attributeCreated + attributeUpdated only.
#define LD_TRIGGER_DEFAULT            (LD_TRIGGER_ATTR_CREATED | LD_TRIGGER_ATTR_MODIFIED)



// -----------------------------------------------------------------------------
//
// ldTriggerFromString - convert a trigger string to bitmask value (0 if unknown)
//
static inline int ldTriggerFromString(const char* s)
{
  if (strcmp(s, "entityCreated")    == 0) return LD_TRIGGER_ENTITY_CREATED;
  if (strcmp(s, "entityUpdated")    == 0) return LD_TRIGGER_ENTITY_UPDATED;
  if (strcmp(s, "entityDeleted")    == 0) return LD_TRIGGER_ENTITY_DELETED;
  if (strcmp(s, "attributeCreated") == 0) return LD_TRIGGER_ATTR_CREATED;
  if (strcmp(s, "attributeUpdated") == 0) return LD_TRIGGER_ATTR_MODIFIED;  // spec says "Updated", report says "Modified"
  if (strcmp(s, "attributeDeleted") == 0) return LD_TRIGGER_ATTR_DELETED;
  return 0;
}



// -----------------------------------------------------------------------------
//
// ldTriggerFromReport - convert a merge report reason to bitmask value
//
static inline int ldTriggerFromReport(const char* reason)
{
  if (strcmp(reason, "attributeCreated")  == 0) return LD_TRIGGER_ATTR_CREATED;
  if (strcmp(reason, "attributeModified") == 0) return LD_TRIGGER_ATTR_MODIFIED;
  if (strcmp(reason, "attributeDeleted")  == 0) return LD_TRIGGER_ATTR_DELETED;
  return 0;
}



// -----------------------------------------------------------------------------
//
// LdNotifyPendingEntry - one entity change captured for later sub dispatch.
//
// Populated by ldNotifyDefer(), consumed by ldSubscriptionNotifyBatch().
// entityP must live until the post-response hook runs (request arena).
// `report` is copied by value so callers can let their LdMergeReport go
// out of scope after the defer call.
//
typedef struct LdNotifyPendingEntry
{
  KjNode*        entityP;
  LdNotifyOp     op;
  bool           hasReport;
  LdMergeReport  report;
  int            reasonsMask;   // OR of LD_TRIGGER_ATTR_* for report.changes — computed
                                // once at defer time so the per-sub trigger check is a
                                // single AND (subs × changes strcmps otherwise)
  uint64_t       deletedAtNs;    // epoch-ns when op == LdNotifyEntityDelete; 0 otherwise
} LdNotifyPendingEntry;



// -----------------------------------------------------------------------------
//
// ldSubscriptionNotifyBatch - match pending entries against every sub and
// emit one notification per sub with data[] of its matches.
//
// For each subscription in cacheP:
//   - run the per-sub static checks (status / expiration / throttling) once;
//   - walk pendingV, applying per-entry matching (trigger, entities, watchedAttrs,
//     scopeQ, q, geoQ) using each entry's own op + merge report;
//   - if any match, collect them in encounter-order and emit ONE HTTP
//     notification whose data[] carries the per-entry transformed entities.
//
// pendingN == 1 is the single-entity case — still valid, produces the same
// notification shape as the pre-batch implementation (one data[] of one).
//
extern void ldSubscriptionNotifyBatch(LdSubCache*           cacheP,
                                      LdNotifyPendingEntry* pendingV,
                                      int                   pendingN);

#endif  // SWNGSILD_LDSUBSCRIPTIONNOTIFY_H_
