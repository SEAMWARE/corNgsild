//
// FILE            ldEntityFragment.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//

#include <string.h>                                    // strcmp

#include "kjson/KjNode.h"                              // KjNode, Kjson
#include "kjson/kjBuilder.h"                           // kjObject, kjChildAdd, kjChildRemove
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjLookup.h"                            // kjLookup

#include "corNgsild/LdRegCache.h"                       // LdRegInfo

#include "corNgsild/ldEntityFragment.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// nameInList - true if NULL-terminated string array v contains s
//
static bool nameInList(const char* s, char** v)
{
  if (v == NULL || s == NULL)
    return false;

  for (int i = 0; v[i] != NULL; i++)
    if (strcmp(s, v[i]) == 0)
      return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// isKeywordAttr - top-level nodes that are never "attributes" in NGSI-LD sense
//
static bool isKeywordAttr(const char* name)
{
  if (name == NULL)              return true;
  if (name[0] == '@')            return true;          // @context, @id, ...
  if (strcmp(name, "id")   == 0) return true;
  if (strcmp(name, "type") == 0) return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// ldEntityFragmentForInfo -
//
KjNode* ldEntityFragmentForInfo(KjNode*     entityP,
                                LdRegInfo*  riP,
                                Kjson*      kjP,
                                bool        detach)
{
  if (entityP == NULL || riP == NULL || kjP == NULL)
    return NULL;

  bool wildcard = (riP->attributeNamesV == NULL);

  //
  // First pass — count claimed attrs. Avoid building an empty fragment.
  //
  int matched = 0;
  for (KjNode* curP = entityP->value.firstChildP; curP != NULL; curP = curP->next)
  {
    if (isKeywordAttr(curP->name))
      continue;

    if (wildcard ||
        nameInList(curP->name, riP->attributeNamesV))
      matched++;
  }

  if (matched == 0)
    return NULL;

  //
  // Build the fragment. id / type / @context are always cloned so they
  // remain on entityP for subsequent passes + local storage.
  //
  KjNode* fragP = kjObject(kjP, NULL);

  KjNode* idP      = kjLookup(entityP, "id");
  KjNode* typeP    = kjLookup(entityP, "type");
  KjNode* contextP = kjLookup(entityP, "@context");

  if (idP      != NULL) kjChildAdd(fragP, kjClone(kjP, idP));
  if (typeP    != NULL) kjChildAdd(fragP, kjClone(kjP, typeP));
  if (contextP != NULL) kjChildAdd(fragP, kjClone(kjP, contextP));

  //
  // Second pass — move or link the claimed attrs.
  //
  KjNode* curP = entityP->value.firstChildP;
  while (curP != NULL)
  {
    KjNode* nextP = curP->next;

    if (isKeywordAttr(curP->name))
    {
      curP = nextP;
      continue;
    }

    if (!wildcard &&
        !nameInList(curP->name, riP->attributeNamesV))
    {
      curP = nextP;
      continue;
    }

    if (detach)
    {
      kjChildRemove(entityP, curP);
      curP->next = NULL;
      kjChildAdd(fragP, curP);
    }
    else
    {
      kjChildAdd(fragP, kjClone(kjP, curP));
    }

    curP = nextP;
  }

  return fragP;
}
