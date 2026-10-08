//
// FILE            ldRegSubMerge.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <string.h>                                   // strcmp

#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corTree/CorNode.h"                           // CorNode
#include "corTree/corTreeLookup.h"                     // corTreeLookup
#include "corTree/corTreeBuilder.h"                    // corTreeChildAdd, corTreeChildRemove
#include "corTree/corTreeChildReplace.h"               // corTreeChildReplace
#include "corTree/corTreeClone.h"                      // corTreeClone

#include "corNgsild/LdVocab.h"                          // LD_VOCAB_NGSILD_NULL
#include "corNgsild/ldTermId.h"                         // ldTermId, CorTerm*
#include "corNgsild/ldRegSubMerge.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// isDeleteMarker - the delete signal: the internal CorNull the validator produces from a
// "urn:ngsi-ld:null" sentinel, or the raw "urn:ngsi-ld:null" string
//
static bool isDeleteMarker(CorNode* fP)
{
  if (fP->type == CorNull)
    return true;
  if (fP->type == CorString && strcmp(fP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
    return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// ldRegSubMerge - the partial update of a Registration or a Subscription (TS 104-175 § 8.4.2)
//
// First level only: a member of the fragment the target lacks is added, one it has REPLACES it - the
// whole value, an object included (a fragment's "notification": {"format": ...} is the whole new
// notification) - and a delete marker removes it. § 8.4.3's merge "up to an arbitrary depth" is the
// Merge Entity's, not this. Whether the result is still valid - a mandatory member replaced away - is
// the caller's re-validation of the complete document.
//
void ldRegSubMerge(CorNode* target, CorNode* fragment, CorAlloc* allocP)
{
  if (target == NULL || fragment == NULL)
    return;

  for (CorNode* fP = fragment->value.head; fP != NULL; fP = fP->next)
  {
    if (fP->name == NULL)
      continue;

    // id and type are immutable — never merged (the validator strips/rejects
    // them; this is belt-and-braces against an internal caller).
    if (ldTermId(fP) == CorTermId || ldTermId(fP) == CorTermType)
      continue;

    CorNode* existingP = corTreeLookup(target, fP->name);

    if (isDeleteMarker(fP))
    {
      if (existingP != NULL)
        corTreeChildRemove(target, existingP);
      continue;
    }

    // Replace an existing member IN PLACE (preserving field order), or append if new
    CorNode* cloneP = corTreeClone(allocP, fP);

    if (existingP != NULL)
      corTreeChildReplace(target, existingP, cloneP);
    else
      corTreeChildAdd(target, cloneP);
  }
}
