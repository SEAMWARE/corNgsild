#ifndef CORNGSILD_LDSUBSCRIPTIONCOUNTERS_H_
#define CORNGSILD_LDSUBSCRIPTIONCOUNTERS_H_

//
// FILE            ldSubscriptionCounters.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

#include "kjson/KjNode.h"                              // KjNode
#include "corNgsild/LdSubCache.h"                       // LdSubCacheItem
#include "corNgsild/LdPernotCache.h"                    // LdPernotItem

extern void ldSubscriptionCountersInject(KjNode* subP, LdSubCacheItem* itemP);
extern void ldPernotCountersInject(KjNode* subP, LdPernotItem* itemP);

#endif  // CORNGSILD_LDSUBSCRIPTIONCOUNTERS_H_
