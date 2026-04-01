#ifndef SWNGSILD_LDTYPEEXPR_H_
#define SWNGSILD_LDTYPEEXPR_H_

//
// FILE            LdTypeExpr.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
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
// Returns a faP-allocated LdTypeExpr, or NULL on error (ldError(400) called).
//
extern LdTypeExpr* ldTypeExprParse(const char* value, KAlloc* faP);

#endif  // SWNGSILD_LDTYPEEXPR_H_
