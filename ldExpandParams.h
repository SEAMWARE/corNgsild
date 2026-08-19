#ifndef CORNGSILD_LDEXPANDPARAMS_H_
#define CORNGSILD_LDEXPANDPARAMS_H_

//
// FILE            ldExpandParams.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Expand all vocab-bearing URL params (type, pick, omit, attrs, geoproperty,
// geometryProperty) in-place inside corNgsild thread-local state. Must run
// after the payload parseHook (so @context is available) and after all URL
// params have been parsed (so the raw values are in corNgsild.*).
//
// Called once per request from the preServiceHook, before the service routine.
//
#include "kalloc/KAlloc.h"                           // KAlloc



// ldExpandParams - expand vocab-bearing URL params in corNgsild
extern void ldExpandParams(KAlloc* kaP);

#endif  // CORNGSILD_LDEXPANDPARAMS_H_
