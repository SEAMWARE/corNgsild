#ifndef CORNGSILD_LD_DISCOVERY_H_
#define CORNGSILD_LD_DISCOVERY_H_
//
// FILE            ldDiscovery.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>
#include "kjson/KjNode.h"
#include "corNgsild/LdRegCache.h"

//
// ldDiscoveryRegAugmentTypes / ldDiscoveryRegAugmentAttrs -
// fold CSR-declared types and attributes into the aggregation result
// produced by db.typeList / db.attrList. See ldDiscovery.c for the
// exact semantics.
//
extern void ldDiscoveryRegAugmentTypes(KjNode* agg, LdRegCache* cacheP, bool details);
extern void ldDiscoveryRegAugmentAttrs(KjNode* agg, LdRegCache* cacheP, bool details);

#endif
