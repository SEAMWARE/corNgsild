#ifndef SWNGSILD_LDTOTEMPORALVALUES_H_
#define SWNGSILD_LDTOTEMPORALVALUES_H_

//
// FILE            ldToTemporalValues.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include "kalloc/KAlloc.h"                              // KAlloc
#include "kjson/kjson.h"                                // Kjson
#include "kjson/KjNode.h"                               // KjNode



// -----------------------------------------------------------------------------
//
// ldToTemporalValues - § 4.5.8 simplified temporal representation.
//
// Transforms the temporal entity tree (each attribute is a KjArray of
// instance objects with type/value/observedAt/...) into:
//
//   "<attrName>": {
//     "type":   "<Property|Relationship|...>",
//     "values": [ [<value>, "<observedAt>"], ... ]
//   }
//
// Relationship → "objects", LanguageProperty → "languageMaps".
//
// Single-entity tree (KjObject) and multi-entity tree (KjArray of entities)
// are both handled — the caller passes the responseTree as-is.
//
// timeProp picks which timestamp goes into each [value, ts] pair:
// "observedAt" (default) | "modifiedAt" | "createdAt".
//
extern void ldToTemporalValues(KjNode* treeP, const char* timeProp, Kjson* kjsonP, KAlloc* faP);

#endif  // SWNGSILD_LDTOTEMPORALVALUES_H_
