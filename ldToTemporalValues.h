#ifndef CORNGSILD_LDTOTEMPORALVALUES_H_
#define CORNGSILD_LDTOTEMPORALVALUES_H_

//
// FILE            ldToTemporalValues.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "kalloc/KAlloc.h"                              // KAlloc
#include "corTree/CorNode.h"                            // CorNode



// -----------------------------------------------------------------------------
//
// ldToTemporalValues - § 4.5.8 simplified temporal representation.
//
// Transforms the temporal entity tree (each attribute is a CorArray of
// instance objects with type/value/observedAt/...) into:
//
//   "<attrName>": {
//     "type":   "<Property|Relationship|...>",
//     "values": [ [<value>, "<observedAt>"], ... ]
//   }
//
// Relationship → "objects", LanguageProperty → "languageMaps".
//
// Single-entity tree (CorObject) and multi-entity tree (CorArray of entities)
// are both handled — the caller passes the responseTree as-is.
//
// timeProp picks which timestamp goes into each [value, ts] pair:
// "observedAt" (default) | "modifiedAt" | "createdAt".
//
extern void ldToTemporalValues(CorNode* treeP, const char* timeProp, KAlloc* allocP, KAlloc* faP);

#endif  // CORNGSILD_LDTOTEMPORALVALUES_H_
