//
// FILE            ldEntityFragment.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//

#include <string.h>                                    // strcmp

#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corTree/CorNode.h"                           // CorNode, CorJson
#include "corTree/corTreeBuilder.h"                    // corTreeObject, corTreeChildAdd, corTreeChildRemove
#include "corTree/corTreeClone.h"                      // corTreeClone
#include "corTree/corTreeLookup.h"                     // corTreeLookup

#include "corNgsild/ldTermClass.h"                     // ldTermClass, LD_TC_*
#include "corNgsild/ldTermId.h"                        // ldTermId
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
static bool isKeywordAttr(CorNode* nodeP)
{
  CorTerm term = ldTermId(nodeP);

  return (term == CorTermId) || (term == CorTermType);
}



// -----------------------------------------------------------------------------
//
// ldEntityFragmentForInfo -
//
CorNode* ldEntityFragmentForInfo(CorNode*   entityP,
                                LdRegInfo*  riP,
                                CorAlloc*   allocP,
                                bool        detach)
{
  if (entityP == NULL || riP == NULL || allocP == NULL)
    return NULL;

  bool wildcard = (riP->attributeNamesV == NULL);

  //
  // First pass — count claimed attrs. Avoid building an empty fragment.
  //
  int matched = 0;
  for (CorNode* curP = entityP->value.head; curP != NULL; curP = curP->next)
  {
    if (isKeywordAttr(curP))
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
  CorNode* fragP = corTreeObject(allocP, NULL);

  CorNode* idP     = corTreeLookup(entityP, "id");
  CorNode* typeP   = corTreeLookup(entityP, "type");
  CorNode* contextP = corTreeLookup(entityP, "@context");

  if (idP      != NULL) corTreeChildAdd(fragP, corTreeClone(allocP, idP));
  if (typeP    != NULL) corTreeChildAdd(fragP, corTreeClone(allocP, typeP));
  if (contextP != NULL) corTreeChildAdd(fragP, corTreeClone(allocP, contextP));

  //
  // Second pass — move or link the claimed attrs.
  //
  CorNode* curP = entityP->value.head;
  while (curP != NULL)
  {
    CorNode* nextP = curP->next;

    if (isKeywordAttr(curP))
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
      corTreeChildRemove(entityP, curP);
      curP->next = NULL;
      corTreeChildAdd(fragP, curP);
    }
    else
    {
      corTreeChildAdd(fragP, corTreeClone(allocP, curP));
    }

    curP = nextP;
  }

  return fragP;
}
