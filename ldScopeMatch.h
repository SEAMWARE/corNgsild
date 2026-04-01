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

#endif  // SWNGSILD_LDSCOPEMATCH_H_
