#ifndef SWNGSILD_LDSCOPEEXPR_H_
#define SWNGSILD_LDSCOPEEXPR_H_

//
// FILE            LdScopeExpr.h
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
// LdScopeGroup - one AND group: entity must match ALL these scope patterns
//
typedef struct LdScopeGroup
{
  char** scopeV;   // NULL-terminated array of scope patterns (AND members)
  int    count;    // number of patterns in this group
} LdScopeGroup;



// -----------------------------------------------------------------------------
//
// LdScopeExpr - OR-of-AND groups parsed from ?scopeQ= URL parameter
//
// Examples:
//   "/Madrid/Gardens/#"                          → 1 group of 1 (single scope pattern)
//   "/Madrid/Gardens/#,/Barcelona/#"             → 2 groups of 1 (pure OR)
//   "(/Madrid/Districts;/CompanyA)"              → 1 group of 2 (AND)
//   "(/Madrid/Districts;/CompanyA)|/Barcelona/#" → 2 groups: one AND(2), one AND(1)
//
typedef struct LdScopeExpr
{
  LdScopeGroup* groupV;      // array of OR groups
  int           groupCount;
  bool          isSimple;    // all groups have count==1 → pure OR
} LdScopeExpr;



// -----------------------------------------------------------------------------
//
// ldScopeExprParse - parse a scopeQ selection expression
//
// Returns a faP-allocated LdScopeExpr, or NULL on error (ldError(400) called).
//
extern LdScopeExpr* ldScopeExprParse(const char* value, KAlloc* faP);

#endif  // SWNGSILD_LDSCOPEEXPR_H_
