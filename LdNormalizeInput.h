#ifndef CORNGSILD_LDNORMALIZEINPUT_H_
#define CORNGSILD_LDNORMALIZEINPUT_H_

//
// FILE            LdNormalizeInput.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
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
// simplified: the request declared its body to be in the simplified format
// (?format=simplified, or the deprecated ?options=keyValues). It is a declared
// flag, not something to sniff — § 10.2.9.3 lists it among the operation's input
// data. Only a merge does anything with it, and what it buys is § 10.2.9.4: "the
// type of any pre-existing Attribute in the target entity shall be preserved".
// So bare scalars and bare arrays are left raw here for ldEntityMerge to shape
// against the target. Undeclared, the body is normalized or concise, where
// § 5.3.2.3 admits no such target-dependence: a JSON primitive is a Property.
//
extern void ldNormalizeInput(KjNode* entityP, KAlloc* kaP, bool mergeMode, bool simplified);



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

#endif  // CORNGSILD_LDNORMALIZEINPUT_H_
