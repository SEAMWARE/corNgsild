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
// Called from entity service routines after a successful entity write.
// Walks all active subscriptions for the tenant, matches each against
// the entity and change event, builds the NGSI-LD Notification payload,
// and POSTs it to the subscription's endpoint.uri.
//
#include <stdbool.h>
#include <string.h>                                    // strcmp (for inline helpers)

#include "kjson/KjNode.h"

#include "swNgsild/ldEntityMerge.h"                  // LdMergeReport
#include "swNgsild/LdSubCache.h"                     // LdSubCache



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

// Default: entity create + attribute created + attribute modified
#define LD_TRIGGER_DEFAULT            (LD_TRIGGER_ENTITY_CREATED | LD_TRIGGER_ATTR_CREATED | LD_TRIGGER_ATTR_MODIFIED)



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
// ldSubscriptionNotify - match entity change against subscriptions and send notifications
//
// cacheP:          per-tenant subscription cache
// entityP:         the entity after the operation (for create/update) or before (for delete)
// op:              create / update / delete
// reportP:         merge report from patchEntity (NULL for create/delete)
//
extern void ldSubscriptionNotify(LdSubCache*     cacheP,
                                 KjNode*         entityP,
                                 LdNotifyOp      op,
                                 LdMergeReport*  reportP);

#endif  // SWNGSILD_LDSUBSCRIPTIONNOTIFY_H_
