#ifndef CORNGSILD_LDTYPEEXPR_H_
#define CORNGSILD_LDTYPEEXPR_H_

//
// FILE            LdTypeExpr.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stdbool.h>                                     // bool

#include "kalloc/KAlloc.h"                             // KAlloc



// -----------------------------------------------------------------------------
//
// LdTypeGroup - one AND group: entity must have ALL these types
//
typedef struct LdTypeGroup
{
  char** typeV;   // NULL-terminated array of expanded URIs (AND members)
  int    count;   // number of types in this group
} LdTypeGroup;



// -----------------------------------------------------------------------------
//
// LdTypeExpr - OR-of-AND groups parsed from ?type= URL parameter
//
// Examples:
//   "Building,House"           → 2 groups of 1 (pure OR)
//   "(Home;Vehicle)"           → 1 group of 2 (AND)
//   "(Home;Vehicle)|Motorhome" → 2 groups: one AND(2), one AND(1)
//
typedef struct LdTypeExpr
{
  LdTypeGroup* groupV;      // array of OR groups
  int          groupCount;
  bool         isSimple;    // all groups have count==1 → pure OR, use $in shortcut
} LdTypeExpr;



// -----------------------------------------------------------------------------
//
// ldTypeExprParse - parse a type selection expression
//
// Returns a kaP-allocated LdTypeExpr, or NULL on error (ldError(400) called).
//
// If kaP is NULL the tree is malloc-allocated; the caller owns it and
// must release it with ldTypeExprFree when the subscription is removed
// from the cache. Used by the sub-cache to keep a parsed tree past the
// request that created it.
//
extern LdTypeExpr* ldTypeExprParse(const char* value, KAlloc* kaP);



// -----------------------------------------------------------------------------
//
// ldTypeExprFree - release a malloc-mode parsed tree
//
extern void ldTypeExprFree(LdTypeExpr* expr);

#endif  // CORNGSILD_LDTYPEEXPR_H_
