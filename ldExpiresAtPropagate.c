//
// FILE            ldExpiresAtPropagate.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool
#include <stddef.h>                                      // NULL
#include <stdint.h>                                      // int64_t
#include <string.h>                                      // strcmp

#include "corAlloc/CorAlloc.h"                           // CorAlloc
#include "corTree/CorNode.h"                             // CorNode
#include "corTree/corTreeLookup.h"                       // corTreeLookup
#include "corTree/corTreeBuilder.h"                      // corTreeString, corTreeInteger, corTreeChildAdd

#include "corNgsild/LdVocab.h"                            // LD_VOCAB_EXPIRES_AT
#include "corNgsild/ldCheckDateTime.h"                    // ldIsoToNanoseconds

#include "corNgsild/ldTermId.h"                           // ldTermId, CorTerm*
#include "corNgsild/ldExpiresAtPropagate.h"               // Own interface



// -----------------------------------------------------------------------------
//
// isAttributeContainer - top-level child of an entity that carries attribute
//                        instances (i.e., not a system field or "id"/"type").
//
static bool isAttributeContainer(CorNode* nodeP)
{
  if (nodeP == NULL || nodeP->name == NULL)              return false;
  if (nodeP->name[0] == '@')                             return false;
  if (ldTermId(nodeP) == CorTermId)          return false;
  if (ldTermId(nodeP) == CorTermType)        return false;
  if (ldTermId(nodeP) == CorTermScope)       return false;
  if (ldTermId(nodeP) == CorTermCreatedAt)   return false;
  if (ldTermId(nodeP) == CorTermModifiedAt)  return false;
  if (ldTermId(nodeP) == CorTermDeletedAt)   return false;
  if (ldTermId(nodeP) == CorTermExpiresAt)   return false;
  return (nodeP->type == CorObject);
}



// -----------------------------------------------------------------------------
//
// applyToInstance -
//
// Set or shorten the per-instance expiresAt to the Entity-level value when the
// latter is earlier than what's already there.
//
// 'entityExpP' is the Entity-level node, in one of the two shapes an expiresAt
// takes: an ISO-8601 string (an Entity fresh off a Context Source, on the read
// paths) or epoch-nanoseconds as an integer (the DB model, e.g. the Snapshot
// capture path). An instance inherits the shape of the value it copies, and an
// instance that already has one keeps its own shape.
//
static void applyToInstance(CorNode* instP, CorNode* entityExpP, int64_t entityNs, CorAlloc* allocP)
{
  CorNode* attrExpP = corTreeLookup(instP, LD_VOCAB_EXPIRES_AT);

  if (attrExpP == NULL)
  {
    CorNode* newP = (entityExpP->type == CorInt)
                     ? corTreeInteger(allocP, LD_VOCAB_EXPIRES_AT, entityExpP->value.i)
                     : corTreeString (allocP, LD_VOCAB_EXPIRES_AT, entityExpP->value.s);
    corTreeChildAdd(instP, newP);
    return;
  }

  // Nanosecond-int form (DB model): compare and shorten in place.
  if (attrExpP->type == CorInt)
  {
    if (attrExpP->value.i > entityNs)
      attrExpP->value.i = entityNs;
    return;
  }

  // Attr-level expiresAt may arrive as a non-reified string OR a reified
  // Property object {"type": "Property", "value": "<iso>"}. Locate the
  // ISO-8601 string in either shape.
  CorNode* dateP = NULL;
  if (attrExpP->type == CorString)
    dateP = attrExpP;
  else if (attrExpP->type == CorObject)
  {
    CorNode* valP = corTreeLookup(attrExpP, "value");
    if (valP != NULL && valP->type == CorString)
      dateP = valP;
  }
  if (dateP == NULL)
    return;

  int64_t  attrNs = ldIsoToNanoseconds(dateP->value.s);
  if (attrNs == 0)
    return;

  if (attrNs > entityNs)
    dateP->value.s = (char*) entityExpP->value.s;
}



// -----------------------------------------------------------------------------
//
// ldExpiresAtPropagate -
//
void ldExpiresAtPropagate(CorNode* entityP, CorAlloc* allocP)
{
  if (entityP == NULL || entityP->type != CorObject)
    return;

  CorNode* entityExpP = corTreeLookup(entityP, LD_VOCAB_EXPIRES_AT);
  if (entityExpP == NULL)
    return;

  int64_t entityNs = 0;
  if      (entityExpP->type == CorString) entityNs = ldIsoToNanoseconds(entityExpP->value.s);
  else if (entityExpP->type == CorInt)    entityNs = (int64_t) entityExpP->value.i;

  if (entityNs == 0)
    return;

  for (CorNode* attrP = entityP->value.head; attrP != NULL; attrP = attrP->next)
  {
    if (!isAttributeContainer(attrP))
      continue;

    // Storage shape: attrP is an object whose children are instance objects
    // keyed by datasetId (or "@none" for the default).
    for (CorNode* instP = attrP->value.head; instP != NULL; instP = instP->next)
    {
      if (instP->type != CorObject)
        continue;
      applyToInstance(instP, entityExpP, entityNs, allocP);
    }
  }
}
