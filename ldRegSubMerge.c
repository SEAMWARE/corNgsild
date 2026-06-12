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
#include "kjson/kjChildReplace.h"                      // kjChildReplace
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

    // A KjNull fragment member is a delete-marker (remove the target member).
    // Any other value replaces an existing member IN PLACE (preserving field
    // order, which is visible in insertion-order renders) or is appended if new.
    KjNode* existingP = kjLookup(target, fP->name);

    if (fP->type == KjNull)
    {
      if (existingP != NULL)
        kjChildRemove(target, existingP);
    }
    else
    {
      KjNode* cloneP = kjClone(allocP, fP);

      if (existingP != NULL)
        kjChildReplace(target, existingP, cloneP);
      else
        kjChildAdd(target, cloneP);
    }
  }
}
