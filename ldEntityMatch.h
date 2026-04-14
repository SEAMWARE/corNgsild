#ifndef SWNGSILD_LDENTITYMATCH_H_
#define SWNGSILD_LDENTITYMATCH_H_

//
// FILE            ldEntityMatch.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Entity matching primitives — reusable by both entity queries and
// subscription matching.
//
#include <stdbool.h>                                  // bool

#include "kjson/KjNode.h"                             // KjNode

#include "swNgsild/LdQ.h"                             // LdQNode
#include "swNgsild/LdScopeExpr.h"                     // LdScopeExpr
#include "swNgsild/LdTypeExpr.h"                      // LdTypeExpr



// -----------------------------------------------------------------------------
//
// ldEntityMatchType - check if entity type matches a type selection expression
//
extern bool ldEntityMatchType(KjNode* typeP, LdTypeExpr* expr);



// -----------------------------------------------------------------------------
//
// ldEntityMatchScope - check if entity scope matches a scopeQ expression
//
extern bool ldEntityMatchScope(KjNode* scopeP, LdScopeExpr* expr);



// -----------------------------------------------------------------------------
//
// ldEntityMatchQ - evaluate a q-filter expression tree against an entity
//
extern bool ldEntityMatchQ(KjNode* entityP, LdQNode* node);

#endif  // SWNGSILD_LDENTITYMATCH_H_
