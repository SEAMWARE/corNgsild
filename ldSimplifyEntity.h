#ifndef SWNGSILD_LDSIMPLIFYENTITY_H_
#define SWNGSILD_LDSIMPLIFYENTITY_H_

//
// FILE            ldSimplifyEntity.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

#include "kjson/KjNode.h"                              // KjNode



// -----------------------------------------------------------------------------
//
// ldSimplifyEntity - convert normalized entity to simplified (keyValues) format
//
// Each attribute wrapper { "type": "Property", "value": X } becomes just X.
// Relationships become the object URI string.
// GeoProperties become the GeoJSON value object.
// id, type, scope are preserved as-is.
//
extern void ldSimplifyEntity(KjNode* entityP);



// -----------------------------------------------------------------------------
//
// ldConciseEntity - convert normalized entity to concise format
//
// Properties without sub-attrs become just the value.
// Relationships become { "object": "urn:..." }.
// Properties WITH sub-attrs keep { "value": X, ... } but drop "type".
// GeoProperty values (JSON objects with "type" field) stay normalized.
//
extern void ldConciseEntity(KjNode* entityP);

#endif  // SWNGSILD_LDSIMPLIFYENTITY_H_
