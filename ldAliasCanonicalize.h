#ifndef CORNGSILD_LD_ALIAS_CANONICALIZE_H_
#define CORNGSILD_LD_ALIAS_CANONICALIZE_H_

//
// FILE            ldAliasCanonicalize.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool

#include "corTree/CorNode.h"                            // CorNode



// -----------------------------------------------------------------------------
//
// ldAliasCanonicalize - "@id" -> "id" and "@type" -> "type", everywhere in a payload
//
// "@type" IS "type" - it is what "type" expands to - and "@id" is "id" (KZ 2026-09-28).
// Code past this point sees ONE spelling, so no lookup by name can miss a member that
// arrived in the other one. A container holding both spellings is a duplicate member:
// 400, at every level, not only at the Entity's.
//
// Walks entities, subscriptions, registrations, batches; attributes, sub-attributes and
// nested objects (entity selectors, ...). Does NOT enter a value (value, object,
// languageMap, vocab, valueList, objectList, json), an @-member, or a JSON-LD value object
// ({"@type": ..., "@value": ...} - its "@type" is the literal's datatype, and stays).
//
// Returns false with ldError set on a duplicate.
//
// ⚠ STOPGAP until the parse hook (B.3). It is an extra walk of the payload; the hook sees
// every member's term id as it stamps it, points a core hit at its item's static name -
// which turns "@type" into "type" for free, as corLdCoreLookup resolves the alias - and
// catches a duplicate with a seen-bit per container. Then this file goes.
//
extern bool ldAliasCanonicalize(CorNode* treeP);

#endif  // CORNGSILD_LD_ALIAS_CANONICALIZE_H_
