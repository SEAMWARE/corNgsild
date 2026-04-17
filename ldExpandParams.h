#ifndef SWNGSILD_LDEXPANDPARAMS_H_
#define SWNGSILD_LDEXPANDPARAMS_H_

//
// FILE            ldExpandParams.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Expand all vocab-bearing URL params (type, pick, omit, attrs, geoproperty,
// geometryProperty) in-place inside swNgsild thread-local state. Must run
// after the payload parseHook (so @context is available) and after all URL
// params have been parsed (so the raw values are in swNgsild.*).
//
// Called once per request from the preServiceHook, before the service routine.
//
#include "kalloc/KAlloc.h"                           // KAlloc



// ldExpandParams - expand vocab-bearing URL params in swNgsild
extern void ldExpandParams(KAlloc* kaP);

#endif  // SWNGSILD_LDEXPANDPARAMS_H_
