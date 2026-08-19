#ifndef CORNGSILD_LDENTITYMATCH_H_
#define CORNGSILD_LDENTITYMATCH_H_

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

#include "corNgsild/LdQ.h"                             // LdQNode
#include "corNgsild/LdScopeExpr.h"                     // LdScopeExpr
#include "corNgsild/LdTypeExpr.h"                      // LdTypeExpr



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
// LdQEntityFetchFunc - callback that resolves a Relationship target id
//
// Used by ldEntityMatchQ to evaluate § 4.9 LinkedEntityRelation
// sub-queries (q=rel{sub-q}) against the *target* of the Relationship.
// corNgsild stays free of db.* coupling — the broker passes a fetcher
// that wraps db.entityRetrieve (or a dist-op fallback). Returning
// non-zero / NULL means the target is unavailable; the linked sub-q
// then evaluates to false (linking entity is excluded from results).
//
typedef int (*LdQEntityFetchFunc)(const char* entityId, KjNode** entityPP, void* userData);



// -----------------------------------------------------------------------------
//
// ldEntityMatchQ - evaluate a q-filter expression tree against an entity
//
// Linked sub-queries (LdQLinkedNode) require a fetcher to resolve the
// Relationship target. ldEntityMatchQ wraps ldEntityMatchQEx with a
// NULL fetcher — Linked nodes evaluate to false in that mode.
//
extern bool ldEntityMatchQ  (KjNode* entityP, LdQNode* node);
extern bool ldEntityMatchQEx(KjNode* entityP, LdQNode* node,
                             LdQEntityFetchFunc fetcher, void* userData);

#endif  // CORNGSILD_LDENTITYMATCH_H_
