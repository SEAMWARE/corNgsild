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
#include "corNgsild/ldRegSubMerge.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// isOpaqueValueObject - a JSON-LD value object whose contents are NOT NGSI-LD
// structure and must therefore not be deep-merged or delete-marker-interpreted:
// an "@value" box or an "@type":"@json" wrapper. This is the only opaque JSON a
// registration can carry (subscriptions carry none). The recursive merge stops
// here and replaces the member wholesale, leaving any "urn:ngsi-ld:null" inside
// it as the literal data it is.
//
static bool isOpaqueValueObject(CorNode* nodeP)
{
  if (nodeP == NULL || nodeP->type != CorObject)
    return false;

  for (CorNode* c = nodeP->value.head; c != NULL; c = c->next)
  {
    if (c->name == NULL)
      continue;
    if (strcmp(c->name, "@value") == 0)
      return true;
    if (strcmp(c->name, "@type") == 0 && c->type == CorString && strcmp(c->value.s, "@json") == 0)
      return true;
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// isDeleteMarker - the delete signal at any depth: the internal CorNull the
// top-level validator produces from a "urn:ngsi-ld:null" sentinel, or — for a
// NESTED member the validator never visited — the raw "urn:ngsi-ld:null" string.
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
// ldRegSubMerge - JSON Merge Patch (RFC 7396), recursive.
//
// A delete-marker member removes its target. Two NGSI-LD-structural objects are
// deep-merged so a fragment may touch a nested member (e.g. notification.format)
// without discarding its siblings, and so a nested "urn:ngsi-ld:null" deletes
// exactly its own field. Arrays and opaque JSON-LD value objects are replaced
// wholesale (RFC 7396 does not merge arrays; @json content is opaque). Whether
// the merged result is still valid — e.g. a mandatory member was deleted — is
// the caller's post-merge re-validation, not this mechanical merge.
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
    if (strcmp(fP->name, "id") == 0 || strcmp(fP->name, "type") == 0)
      continue;

    CorNode* existingP = corTreeLookup(target, fP->name);

    if (isDeleteMarker(fP))
    {
      if (existingP != NULL)
        corTreeChildRemove(target, existingP);
      continue;
    }

    // Deep-merge two structural objects (recurse); stop at arrays and opaque
    // value objects, which replace wholesale.
    if (fP->type == CorObject && existingP != NULL && existingP->type == CorObject
        && (isOpaqueValueObject(fP) == false) && (isOpaqueValueObject(existingP) == false))
    {
      ldRegSubMerge(existingP, fP, allocP);
      continue;
    }

    // Otherwise replace an existing member IN PLACE (preserving field order) or
    // append if new.
    CorNode* cloneP = corTreeClone(allocP, fP);

    if (existingP != NULL)
      corTreeChildReplace(target, existingP, cloneP);
    else
      corTreeChildAdd(target, cloneP);
  }
}
