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
extern void ldNormalizeInput(KjNode* entityP, KAlloc* kaP, bool mergeMode);



// -----------------------------------------------------------------------------
//
// ldWrapAsGeoProperty - per-field GeoProperty wrap.
//
// Replaces `childP` (a member of `entityP`) with `{type:GeoProperty, value:<orig>}`.
// Caller already knows the attr should be a GeoProperty, so this skips the
// auto-detection ldNormalizeInput does and just performs the wrap. Used by
// the CSR intake path for location / observationSpace / operationSpace which
// arrive as bare GeoJSON Geometry on the wire (§ 5.2.9) but live as
// normalized GeoProperty wrappers in storage and on output.
//
extern void ldWrapAsGeoProperty(KjNode* entityP, KjNode* childP, KAlloc* kaP);

#endif  // SWNGSILD_LDNORMALIZEINPUT_H_
