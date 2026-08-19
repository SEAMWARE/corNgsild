#ifndef CORNGSILD_LDAPIENTITYTODBMODEL_H_
#define CORNGSILD_LDAPIENTITYTODBMODEL_H_

//
// FILE            ldApiEntityToDbModel.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode



// -----------------------------------------------------------------------------
//
// ldApiEntityToDbModel - transform API-format entity tree to DB storage format
//
// Wraps each attribute in a dataset-keyed object:
//   "attrName": { "@none": { ...attr... } }
// or for multi-attribute:
//   "attrName": { "@none": { ... }, "urn:ds:1": { ... } }
//
// Also adds createdAt/modifiedAt timestamps to the entity and each attribute instance.
//
// createdAt: entity-level createdAt to stamp, in nanoseconds. Pass 0 for a
// create (stamp 'now'); on a Replace pass the stored entity's createdAt so it
// survives the write (§ 6.5.3.3). Attribute-instance createdAt is always 'now'.
//
extern void ldApiEntityToDbModel(KjNode* entityP, KAlloc* faP, int64_t createdAt);

#endif  // CORNGSILD_LDAPIENTITYTODBMODEL_H_
