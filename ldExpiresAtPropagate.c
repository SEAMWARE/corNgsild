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

#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjLookup.h"                              // kjLookup
#include "kjson/kjBuilder.h"                             // kjString, kjInteger, kjChildAdd

#include "corNgsild/LdVocab.h"                            // LD_VOCAB_EXPIRES_AT
#include "corNgsild/ldCheckDateTime.h"                    // ldIsoToNanoseconds

#include "corNgsild/ldExpiresAtPropagate.h"               // Own interface



// -----------------------------------------------------------------------------
//
// isAttributeContainer - top-level child of an entity that carries attribute
//                        instances (i.e., not a system field or "id"/"type").
//
static bool isAttributeContainer(KjNode* nodeP)
{
  if (nodeP == NULL || nodeP->name == NULL)              return false;
  if (nodeP->name[0] == '@')                             return false;
  if (strcmp(nodeP->name, "id")                  == 0)   return false;
  if (strcmp(nodeP->name, "type")                == 0)   return false;
  if (strcmp(nodeP->name, "scope")               == 0)   return false;
  if (strcmp(nodeP->name, "createdAt")           == 0)   return false;
  if (strcmp(nodeP->name, "modifiedAt")          == 0)   return false;
  if (strcmp(nodeP->name, "deletedAt")           == 0)   return false;
  if (strcmp(nodeP->name, LD_VOCAB_EXPIRES_AT)   == 0)   return false;
  return (nodeP->type == KjObject);
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
static void applyToInstance(KjNode* instP, KjNode* entityExpP, int64_t entityNs, Kjson* kjsonP)
{
  KjNode* attrExpP = kjLookup(instP, LD_VOCAB_EXPIRES_AT);

  if (attrExpP == NULL)
  {
    KjNode* newP = (entityExpP->type == KjInt)
                     ? kjInteger(kjsonP, LD_VOCAB_EXPIRES_AT, entityExpP->value.i)
                     : kjString (kjsonP, LD_VOCAB_EXPIRES_AT, entityExpP->value.s);
    kjChildAdd(instP, newP);
    return;
  }

  // Nanosecond-int form (DB model): compare and shorten in place.
  if (attrExpP->type == KjInt)
  {
    if (attrExpP->value.i > entityNs)
      attrExpP->value.i = entityNs;
    return;
  }

  // Attr-level expiresAt may arrive as a non-reified string OR a reified
  // Property object {"type": "Property", "value": "<iso>"}. Locate the
  // ISO-8601 string in either shape.
  KjNode* dateP = NULL;
  if (attrExpP->type == KjString)
    dateP = attrExpP;
  else if (attrExpP->type == KjObject)
  {
    KjNode* valP = kjLookup(attrExpP, "value");
    if (valP != NULL && valP->type == KjString)
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
void ldExpiresAtPropagate(KjNode* entityP, Kjson* kjsonP)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  KjNode* entityExpP = kjLookup(entityP, LD_VOCAB_EXPIRES_AT);
  if (entityExpP == NULL)
    return;

  int64_t entityNs = 0;
  if      (entityExpP->type == KjString)  entityNs = ldIsoToNanoseconds(entityExpP->value.s);
  else if (entityExpP->type == KjInt)     entityNs = (int64_t) entityExpP->value.i;

  if (entityNs == 0)
    return;

  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (!isAttributeContainer(attrP))
      continue;

    // Storage shape: attrP is an object whose children are instance objects
    // keyed by datasetId (or "@none" for the default).
    for (KjNode* instP = attrP->value.firstChildP; instP != NULL; instP = instP->next)
    {
      if (instP->type != KjObject)
        continue;
      applyToInstance(instP, entityExpP, entityNs, kjsonP);
    }
  }
}
