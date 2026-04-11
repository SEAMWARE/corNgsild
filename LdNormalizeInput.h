#ifndef SWNGSILD_LDNORMALIZEINPUT_H_
#define SWNGSILD_LDNORMALIZEINPUT_H_

//
// FILE            LdNormalizeInput.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                   // bool

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode



// -----------------------------------------------------------------------------
//
// ldNormalizeInput - convert simplified/concise attributes to normalized format
//
// Called after JSON-LD expansion in ldParseHook.  Walks the entity tree and
// ensures every attribute is an KjObject with an explicit "type" field and
// the appropriate value key (hasValue, hasObject, etc.).
//
// mergeMode: set to true for PATCH /entities/{id} (Merge Entity § 5.6.17) so
// that a fragment attribute which is a plain object without a type/value key
// is left untouched instead of being wrapped as a simplified Property — in
// merge semantics, such an object is a partial fragment targeting existing
// sub-attributes, not a new Property whose value happens to be an object.
//
extern void ldNormalizeInput(KjNode* entityP, KAlloc* faP, bool mergeMode);

#endif  // SWNGSILD_LDNORMALIZEINPUT_H_
