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

#include "swNgsild/LdVocab.h"                          // LD_VOCAB_NGSILD_NULL
#include "swNgsild/ldRegSubMerge.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// isOpaqueValueObject - a JSON-LD value object whose contents are NOT NGSI-LD
// structure and must therefore not be deep-merged or delete-marker-interpreted:
// an "@value" box or an "@type":"@json" wrapper. This is the only opaque JSON a
// registration can carry (subscriptions carry none). The recursive merge stops
// here and replaces the member wholesale, leaving any "urn:ngsi-ld:null" inside
// it as the literal data it is.
//
static bool isOpaqueValueObject(KjNode* nodeP)
{
  if (nodeP == NULL || nodeP->type != KjObject)
    return false;

  for (KjNode* c = nodeP->value.firstChildP; c != NULL; c = c->next)
  {
    if (c->name == NULL)
      continue;
    if (strcmp(c->name, "@value") == 0)
      return true;
    if (strcmp(c->name, "@type") == 0 && c->type == KjString && strcmp(c->value.s, "@json") == 0)
      return true;
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// isDeleteMarker - the delete signal at any depth: the internal KjNull the
// top-level validator produces from a "urn:ngsi-ld:null" sentinel, or — for a
// NESTED member the validator never visited — the raw "urn:ngsi-ld:null" string.
//
static bool isDeleteMarker(KjNode* fP)
{
  if (fP->type == KjNull)
    return true;
  if (fP->type == KjString && strcmp(fP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
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

    KjNode* existingP = kjLookup(target, fP->name);

    if (isDeleteMarker(fP))
    {
      if (existingP != NULL)
        kjChildRemove(target, existingP);
      continue;
    }

    // Deep-merge two structural objects (recurse); stop at arrays and opaque
    // value objects, which replace wholesale.
    if (fP->type == KjObject && existingP != NULL && existingP->type == KjObject
        && (isOpaqueValueObject(fP) == false) && (isOpaqueValueObject(existingP) == false))
    {
      ldRegSubMerge(existingP, fP, allocP);
      continue;
    }

    // Otherwise replace an existing member IN PLACE (preserving field order) or
    // append if new.
    KjNode* cloneP = kjClone(allocP, fP);

    if (existingP != NULL)
      kjChildReplace(target, existingP, cloneP);
    else
      kjChildAdd(target, cloneP);
  }
}
