#ifndef CORNGSILD_LDEXPANDPARAMS_H_
#define CORNGSILD_LDEXPANDPARAMS_H_

//
// FILE            ldExpandParams.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Expand all vocab-bearing URL params (type, pick, omit, attrs, geoproperty,
// geometryProperty) in-place inside corNgsild thread-local state. Must run
// after the payload parseHook (so @context is available) and after all URL
// params have been parsed (so the raw values are in corNgsild.*).
//
// Called once per request from the preServiceHook, before the service routine.
//
#include "corAlloc/CorAlloc.h"                       // CorAlloc



// ldExpandParams - expand vocab-bearing URL params in corNgsild
extern void ldExpandParams(CorAlloc* kaP);

// -----------------------------------------------------------------------------
//
// ldExpandParamsQ - apply expandValues and langProperties to the parsed q (ldExpandParams runs it;
// ldQueryBodyToParams runs it again for a POST Query's body). false: 400 already set.
//
extern bool ldExpandParamsQ(CorAlloc* kaP);

#endif  // CORNGSILD_LDEXPANDPARAMS_H_
