//
// FILE            ldPickOmit.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool
#include <stddef.h>                                    // NULL
#include <string.h>                                    // strcmp

#include "kjson/KjNode.h"                           // KjNode
#include "kjson/kjBuilder.h"                    // kjChildRemove

#include "swNgsild/ldPickOmit.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// isProtected - check if a field name is a mandatory NGSI-LD member
//
static bool isProtected(const char* name)
{
  if (strcmp(name, "id")       == 0)  return true;
  if (strcmp(name, "type")     == 0)  return true;
  if (strcmp(name, "@context") == 0)  return true;
  return false;
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
static void pickOmitImpl(KjNode* entityP, char** pickV, char** omitV)
{
  if (entityP == NULL)
    return;

  KjNode* childP = entityP->value.firstChildP;

  while (childP != NULL)
  {
    KjNode* nextP = childP->next;

    if (childP->name != NULL && strcmp(childP->name, "@context") != 0)
    {
      bool remove = false;

      if (pickV != NULL && !inStringV(childP->name, pickV))
        remove = true;
      else if (omitV != NULL && inStringV(childP->name, omitV))
        remove = true;

      if (remove)
        kjChildRemove(entityP, childP);
    }

    childP = nextP;
  }
}



// -----------------------------------------------------------------------------
//
// ldPickOmit -
//
void ldPickOmit(KjNode* entityP, char** pickV, char** omitV)
{
  pickOmitImpl(entityP, pickV, omitV);
}



// -----------------------------------------------------------------------------
//
// ldPickOmitNested -
//
// Identical semantics to ldPickOmit; kept as a separate symbol so callers
// can document intent (linked-entity vs root) without rebinding.
//
void ldPickOmitNested(KjNode* entityP, char** pickV, char** omitV)
{
  pickOmitImpl(entityP, pickV, omitV);
}
