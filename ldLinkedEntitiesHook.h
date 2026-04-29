#ifndef SWNGSILD_LD_LINKED_ENTITIES_HOOK_H_
#define SWNGSILD_LD_LINKED_ENTITIES_HOOK_H_

//
// FILE            ldLinkedEntitiesHook.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Optional callback fired during notification body shaping when the
// subscription has notification.join set (§ 4.5.23 / § 5.2.14). The
// implementation lives in the broker (it needs db.entityRetrieve and
// the reg cache) and is registered at startup.
//
// dataArrayP : the notification "data" array of matched entities,
//              mutated in place — flat: appends linked entities,
//              inline: adds "entity" sub-attribute to Relationships.
// mode       : "flat" or "inline" (per § 4.5.23.2 / § 4.5.23.3).
// joinLevel  : recursion depth; spec default is 1.
// tenantP    : opaque tenant pointer (Tenant*) for the broker's lookup.
//

#include "kjson/KjNode.h"                              // KjNode



typedef void (*LdLinkedEntitiesExpandHook)(KjNode* dataArrayP, const char* mode, int joinLevel, void* tenantP);



extern void ldLinkedEntitiesHookSet(LdLinkedEntitiesExpandHook fn);
extern void ldLinkedEntitiesHookInvoke(KjNode* dataArrayP, const char* mode, int joinLevel, void* tenantP);

#endif
