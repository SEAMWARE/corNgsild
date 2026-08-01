#ifndef SWNGSILD_LDSCOPEMATCH_H_
#define SWNGSILD_LDSCOPEMATCH_H_

//
// FILE            ldScopeMatch.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                    // bool

#include "kalloc/KAlloc.h"                              // KAlloc
#include "kjson/KjNode.h"                               // KjNode



// -----------------------------------------------------------------------------
//
// ldScopePatternMatch - check if a scope value matches a scope pattern
//
// Patterns use '+' for single-level wildcard and '/#' for multi-level wildcard.
// Example: "/Madrid/+/Parque" matches "/Madrid/Gardens/Parque"
//          "/Madrid/#" matches "/Madrid" and "/Madrid/Gardens" and "/Madrid/Gardens/Parque"
//          "/#" matches any non-empty scope string
//
extern bool ldScopePatternMatch(const char* pattern, const char* value);



// -----------------------------------------------------------------------------
//
// ldScopeToRegex - convert a scope pattern to a regex string (for MongoDB)
//
// Pattern rules:
//   /Madrid              -> ^/Madrid$                  (exact match)
//   /Madrid/#            -> ^/Madrid(/.*)?$            (Madrid and everything below)
//   /Madrid/+/Parque     -> ^/Madrid/[^/]+/Parque$     (single-level wildcard)
//   /Madrid/+/#          -> ^/Madrid/[^/]+(/.*)?$      (wildcard + multi-level)
//   /#                   -> .+                          (any non-empty scope)
//
// Returns the number of bytes written (excluding NUL).
//
extern int ldScopeToRegex(const char* pattern, char* buf, int bufSize);



// -----------------------------------------------------------------------------
//
// ldScopeCanonicalize - give every Scope of an Entity/Registration its implicit leading '/'
//
// § 5.2.7 makes the leading '/' of a Scope optional - the slash is there, it is just
// implicit - while the scope query language of § 7.2.5 has no such option: every scope
// query names a Scope beginning with '/'. Storing the canonical form is what keeps a
// Scope written without the slash selectable, and what lets the two spellings of one
// Scope deduplicate when the versions of a distributed Entity are merged.
//
// The NGSI-LD Null is left alone: it marks a deleted Scope, it is not one.
//
// Takes an Entity/Registration object or an array of them (batch operations).
//
extern void ldScopeCanonicalize(KjNode* treeP, KAlloc* kaP);

#endif  // SWNGSILD_LDSCOPEMATCH_H_
