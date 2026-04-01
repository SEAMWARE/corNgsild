#ifndef SWNGSILD_LDAPIENTITYTODBMODEL_H_
#define SWNGSILD_LDAPIENTITYTODBMODEL_H_

//
// FILE            ldApiEntityToDbModel.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
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
extern void ldApiEntityToDbModel(KjNode* entityP, KAlloc* faP);

#endif  // SWNGSILD_LDAPIENTITYTODBMODEL_H_
