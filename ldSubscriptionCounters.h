#ifndef CORNGSILD_LDSUBSCRIPTIONCOUNTERS_H_
#define CORNGSILD_LDSUBSCRIPTIONCOUNTERS_H_

//
// FILE            ldSubscriptionCounters.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//

#include "corTree/CorNode.h"                           // CorNode
#include "corNgsild/LdSubCache.h"                       // LdSubCacheItem
#include "corNgsild/LdPernotCache.h"                    // LdPernotItem

extern void ldSubscriptionCountersInject(CorNode* subP, LdSubCacheItem* itemP);
extern void ldPernotCountersInject(CorNode* subP, LdPernotItem* itemP);

#endif  // CORNGSILD_LDSUBSCRIPTIONCOUNTERS_H_
