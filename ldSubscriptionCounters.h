#ifndef SWNGSILD_LDSUBSCRIPTIONCOUNTERS_H_
#define SWNGSILD_LDSUBSCRIPTIONCOUNTERS_H_

//
// FILE            ldSubscriptionCounters.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

#include "kjson/KjNode.h"                              // KjNode
#include "swNgsild/LdSubCache.h"                       // LdSubCacheItem
#include "swNgsild/LdPernotCache.h"                    // LdPernotItem

extern void ldSubscriptionCountersInject(KjNode* subP, LdSubCacheItem* itemP);
extern void ldPernotCountersInject(KjNode* subP, LdPernotItem* itemP);

#endif  // SWNGSILD_LDSUBSCRIPTIONCOUNTERS_H_
