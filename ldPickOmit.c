//
// FILE            ldPickOmit.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stdbool.h>                                     // bool
#include <stddef.h>                                    // NULL
#include <string.h>                                    // strcmp

#include "corTree/CorNode.h"                        // CorNode
#include "corTree/corTreeBuilder.h"             // corTreeChildRemove

#include "corNgsild/ldIsEntityKeyword.h"          // ldIsEntityKeyword
#include "corNgsild/CorNgsild.h"                   // corNgsild (geometryPropertyExpanded)
#include "corNgsild/ldPickOmit.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// isGeoJsonProtected - the selected GeoJSON geometry GeoProperty must survive
// a pick/omit/attrs projection so ldToGeoJson can still build the "geometry"
// field from it (§ 5.3.3.2 — geometry is selected from the stored entity, not
// from the projected member set). Only set for a geo+json response; the
// attribute is later pruned from "properties" if the user did not request it.
//
static bool isGeoJsonProtected(const char* name)
{
  return (corNgsild.geometryPropertyExpanded != NULL && name != NULL &&
          strcmp(name, corNgsild.geometryPropertyExpanded) == 0);
}



// -----------------------------------------------------------------------------
//
// inStringV - check if name is in a NULL-terminated string array
//
static bool inStringV(const char* name, char** strV)
{
  for (int ix = 0; strV[ix] != NULL; ix++)
  {
    if (strcmp(name, strV[ix]) == 0)
      return true;
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// pickOmitImpl - shared body. Always pure pick/omit semantics.
//
// Per § 6.3.6: "the Entity is reduced down to only contain the listed Entity
// members" — `id`/`type`/`scope` count as Entity members and ARE filtered if
// the user didn't list them. Only `@context` is left alone (added at render
// time, not part of the storage entity).
//
static void pickOmitImpl(CorNode* entityP, char** pickV, char** omitV)
{
  if (entityP == NULL)
    return;

  CorNode* childP = entityP->value.head;

  while (childP != NULL)
  {
    CorNode* nextP = childP->next;

    if (childP->name != NULL && strcmp(childP->name, "@context") != 0)
    {
      bool remove = false;

      if (pickV != NULL && !inStringV(childP->name, pickV))
        remove = true;
      else if (omitV != NULL && inStringV(childP->name, omitV))
        remove = true;

      if (remove && !isGeoJsonProtected(childP->name))
        corTreeChildRemove(entityP, childP);
    }

    childP = nextP;
  }
}



// -----------------------------------------------------------------------------
//
// ldPickOmit -
//
void ldPickOmit(CorNode* entityP, char** pickV, char** omitV)
{
  pickOmitImpl(entityP, pickV, omitV);
}



// -----------------------------------------------------------------------------
//
// ldAttrsFilter - § 6.4.3.2 / § 5.10.2 ?attrs= response filter
//
// Unlike pick (§ 6.3.6), `attrs` only filters Attributes — entity members
// (id, type, scope, @context, createdAt, modifiedAt, ...) are always
// preserved. The filter walks the entity's children once and removes any
// non-keyword child whose name is not in attrsV.
//
void ldAttrsFilter(CorNode* entityP, char** attrsV)
{
  if (entityP == NULL || attrsV == NULL)
    return;

  CorNode* childP = entityP->value.head;

  while (childP != NULL)
  {
    CorNode* nextP = childP->next;

    if (childP->name != NULL && !ldIsEntityMember(childP))
    {
      if (!inStringV(childP->name, attrsV) && !isGeoJsonProtected(childP->name))
        corTreeChildRemove(entityP, childP);
    }

    childP = nextP;
  }
}
