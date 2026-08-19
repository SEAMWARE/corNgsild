#ifndef CORNGSILD_LDPERNOTCACHE_OPS_H_
#define CORNGSILD_LDPERNOTCACHE_OPS_H_

//
// FILE            ldPernotCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include "kjson/KjNode.h"                              // KjNode
#include "kalloc/KAlloc.h"                             // KAlloc

#include "corNgsild/LdPernotCache.h"                    // LdPernotCache, LdPernotItem
#include "corNgsild/LdQ.h"                              // LdQNode



// ldPernotCacheCreate - allocate and initialize an empty pernot cache
extern LdPernotCache* ldPernotCacheCreate(void);

// ldPernotCacheItemAdd - add a subscription to the pernot cache
extern LdPernotItem* ldPernotCacheItemAdd(LdPernotCache* cacheP, KjNode* subTree,
                                           LdQNode* qExpr, void* tenantP);

// ldPernotCacheItemLookup - find by subscription ID
extern LdPernotItem* ldPernotCacheItemLookup(LdPernotCache* cacheP, const char* subId);

// ldPernotCacheItemRemove - remove by subscription ID
extern bool ldPernotCacheItemRemove(LdPernotCache* cacheP, const char* subId);

// ldPernotCacheRelease - free everything
extern void ldPernotCacheRelease(LdPernotCache* cacheP);

#endif  // CORNGSILD_LDPERNOTCACHE_OPS_H_
