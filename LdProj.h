#ifndef CORNGSILD_LDPROJ_H_
#define CORNGSILD_LDPROJ_H_

//
// FILE            LdProj.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// § 4.21 NGSI-LD Attribute Projection Language — tree representation of
// `?pick=` / `?omit=`. Supports linked-entity nesting:
//
//   pick = id,locatedAt{id,name,isInCountry{id,description}}
//
// Parses to:
//
//   [ "id"
//   , "locatedAt" -> [ "id"
//                    , "name"
//                    , "isInCountry" -> [ "id", "description" ] ] ]
//
// Both ',' and '|' are accepted as the disjunction (or) separator.
//
#include <stdbool.h>                      // bool

#include "kalloc/kaAlloc.h"               // KAlloc



// -----------------------------------------------------------------------------
//
// LdProjItem - one node in the projection tree
//
//   name       Attribute name. Stored as the user-supplied short form at parse
//              time, then JSON-LD-expanded in-place by ldExpandParams (so the
//              broker can match against the storage form).
//   child      Sub-projection applied INSIDE the linked entity reached via this
//              attribute (the {...} portion). NULL for plain leaves.
//   next       Next sibling in the same projection list.
//
typedef struct LdProjItem
{
  char*               name;
  struct LdProjItem*  child;
  struct LdProjItem*  next;
} LdProjItem;



// -----------------------------------------------------------------------------
//
// ldProjectionParse - parse a `?pick=` / `?omit=` value into a tree.
//
// Allocates entries from `kaP`; the returned tree is read-only after this
// call (names alias into the input buffer for now, JSON-LD expansion happens
// in ldExpandParams). Returns NULL on syntax error (unbalanced braces / empty
// name / etc.) and leaves `*errMsgP` pointing at a static description.
//
extern LdProjItem* ldProjectionParse(char* value, KAlloc* kaP, const char** errMsgP);



// -----------------------------------------------------------------------------
//
// ldProjectionTopLevelNames - flatten a tree's TOP level into a NULL-terminated
// char* array.
//
// `includeNested`:
//   true  — include items that carry a sub-projection (correct for `pick`:
//           `pick=id,locatedAt{x,y}` keeps `locatedAt` at root).
//   false — skip items that carry a sub-projection (correct for `omit`:
//           `omit=locatedAt{x,y}` does NOT remove `locatedAt` at root —
//           the {x,y} clause says "navigate into locatedAt's linked entity
//           and apply the omit there", so locatedAt itself stays).
//
extern char** ldProjectionTopLevelNames(LdProjItem* tree, KAlloc* kaP, bool includeNested);



// -----------------------------------------------------------------------------
//
// ldProjectionFindChild - given a tree and a top-level attribute name, return
// the sub-projection (`child` of the matching item) or NULL when not found or
// when the matching item has no nested {...} clause.
//
extern LdProjItem* ldProjectionFindChild(LdProjItem* tree, const char* name);

#endif  // CORNGSILD_LDPROJ_H_
