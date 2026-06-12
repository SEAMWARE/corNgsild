//
// FILE            ldRegSubMerge.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <string.h>                                   // strcmp

#include "kjson/kjson.h"                               // Kjson
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjBuilder.h"                           // kjChildAdd, kjChildRemove
#include "kjson/kjClone.h"                             // kjClone

#include "swNgsild/ldRegSubMerge.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ldRegSubMerge -
//
void ldRegSubMerge(KjNode* target, KjNode* fragment, Kjson* allocP)
{
  if (target == NULL || fragment == NULL)
    return;

  for (KjNode* fP = fragment->value.firstChildP; fP != NULL; fP = fP->next)
  {
    if (fP->name == NULL)
      continue;

    // id and type are immutable — never merged (the validator strips/rejects
    // them; this is belt-and-braces against an internal caller).
    if (strcmp(fP->name, "id") == 0 || strcmp(fP->name, "type") == 0)
      continue;

    // Replace any existing member; a KjNull fragment member is a delete-marker
    // so the member is simply left removed.
    KjNode* existingP = kjLookup(target, fP->name);
    if (existingP != NULL)
      kjChildRemove(target, existingP);

    if (fP->type != KjNull)
      kjChildAdd(target, kjClone(allocP, fP));
  }
}
