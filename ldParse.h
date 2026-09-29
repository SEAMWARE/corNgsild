#ifndef CORNGSILD_LD_PARSE_H_
#define CORNGSILD_LD_PARSE_H_

//
// FILE            ldParse.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool

#include "corTree/CorNode.h"                            // CorNode
#include "corJson/CorJson.h"                            // CorJson



// -----------------------------------------------------------------------------
//
// ldParseKeyHook - corJson's key hook (CorJson.keyF) for NGSI-LD request bodies
//
// Called the moment a member's name is final, before its value is parsed: stamps the
// node with its term id - the core term it is (corLdCoreLookup, one probe, no
// allocation), or LD_TERM_NOT_CORE. Every member of the body arrives stamped, so
// ldTermId never has to look a request node up, and the expander uses the stamp
// instead of expanding a core term's name again.
//
// Stamping only (step B.3a): no renaming, no duplicate check, no deferred list - those
// need to know where values and JSON literals are, which comes with the deferred pass.
//
extern bool ldParseKeyHook(CorJson* corJsonP, CorNode* containerP, CorNode* nodeP, int depth);



// -----------------------------------------------------------------------------
//
// LdParseState - what the key hook keeps while one body is parsed
//
// Per depth: the container last seen there, whether it lies inside a VALUE (a Property's
// value, an @-member, anything under one - where names are somebody's JSON and are left
// alone), and whether its latest member does. corJson parses depth-first, so the member
// whose value is being parsed is always the latest one at its depth.
//
// aliasV: the "@id"/"@type" members met in structural positions, in document order, for
// ldParseAliasesApply once the body is complete.
//
#define LD_PARSE_DEPTH_MAX  64

typedef struct LdParseLevel
{
  CorNode*  containerP;
  bool      inValue;          // the container lies inside a value
  bool      memberSeen;       // a member has been met at this depth, in this branch
  bool      memberInValue;    // ... and its value lies inside a value
} LdParseLevel;

typedef struct LdParseState
{
  LdParseLevel  level[LD_PARSE_DEPTH_MAX];
  int           deepest;      // deepest level in use - the ones below are reset when the parse climbs back
  CorNode**     aliasV;       // "@id"/"@type" members ...
  CorNode**     aliasContainerV;  // ... and their containers
  int           aliasN;
  int           aliasMax;
} LdParseState;



// -----------------------------------------------------------------------------
//
// ldParseAliasesApply - "@id"/"@type" become "id"/"type"; both spellings in one container: 400
//
// "@type" IS "type" (what "type" expands to), "@id" is "id" (KZ 2026-09-28). Works on the
// list the key hook collected - usually empty - not on the tree. A container holding
// "@value" is a JSON-LD value object, whose "@type" is the literal's datatype: left alone.
// Returns false with ldError set on a duplicate.
//
extern bool ldParseAliasesApply(void);



// -----------------------------------------------------------------------------
//
// ldPrePayloadParseHook - corRest's pre-parse hook: the key hook for NGSI-LD bodies only
//
// A route with an NGSI-LD operation carries an NGSI-LD payload; any other body (an
// @context document posted to /jsonldContexts, ...) is parsed as plain JSON.
//
extern void ldPrePayloadParseHook(void);

#endif  // CORNGSILD_LD_PARSE_H_
