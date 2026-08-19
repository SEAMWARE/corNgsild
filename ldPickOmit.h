#ifndef CORNGSILD_LDPICKOMIT_H_
#define CORNGSILD_LDPICKOMIT_H_

//
// FILE            ldPickOmit.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include "kjson/KjNode.h"                           // KjNode



// -----------------------------------------------------------------------------
//
// ldPickOmit - apply pick/omit attribute projection to an entity (root form).
//
// Per § 6.3.6 the entity is reduced to ONLY the listed members; id/type/scope
// count as Entity members and are NOT protected — they are stripped when not
// in pickV (or are removed when in omitV). Only `@context` (added at render
// time, not part of the stored entity) survives. This is the key difference
// vs. ldAttrsFilter (the deprecated `attrs`), which always keeps id/type/scope.
//
extern void ldPickOmit(KjNode* entityP, char** pickV, char** omitV);



// -----------------------------------------------------------------------------
//
// ldPickOmitNested - identical semantics to ldPickOmit; a separate symbol so
// call sites on Linked Entities reached via § 4.5.23 can document intent. A
// sub-projection `attr{...}` lists exactly what to keep (or remove), and
// id/type have no special status there either. The ETSI test suite (018_20,
// 018_21, 019_20, 019_21, 019_22, 019_23) checks this directly.
//
extern void ldPickOmitNested(KjNode* entityP, char** pickV, char** omitV);



// -----------------------------------------------------------------------------
//
// ldAttrsFilter - apply ?attrs= response filter (deprecated alias of pick)
//
// § 6.4.3.2 / § 5.10.2: `attrs` only filters Attributes — entity members
// (id, type, scope, @context, createdAt, modifiedAt, ...) are always
// preserved. This is the key difference vs. ldPickOmit, which strips
// id/type/scope when not listed in pickV.
//
extern void ldAttrsFilter(KjNode* entityP, char** attrsV);

#endif  // CORNGSILD_LDPICKOMIT_H_
