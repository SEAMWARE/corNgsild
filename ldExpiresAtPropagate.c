//
// FILE            ldExpiresAtPropagate.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                     // bool
#include <stddef.h>                                      // NULL
#include <stdint.h>                                      // uint64_t
#include <string.h>                                      // strcmp

#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjLookup.h"                              // kjLookup
#include "kjson/kjBuilder.h"                             // kjString, kjChildAdd

#include "swNgsild/LdVocab.h"                            // LD_VOCAB_EXPIRES_AT
#include "swNgsild/ldCheckDateTime.h"                    // ldIsoToNanoseconds

#include "swNgsild/ldExpiresAtPropagate.h"               // Own interface



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
// Set or shorten the per-instance expiresAt to entityIso when entityIso is
// earlier than what's already there.
//
static void applyToInstance(KjNode* instP, const char* entityIso, uint64_t entityNs, Kjson* kjsonP)
{
  KjNode* attrExpP = kjLookup(instP, LD_VOCAB_EXPIRES_AT);

  if (attrExpP == NULL)
  {
    kjChildAdd(instP, kjString(kjsonP, LD_VOCAB_EXPIRES_AT, entityIso));
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

  uint64_t attrNs = ldIsoToNanoseconds(dateP->value.s);
  if (attrNs == 0)
    return;

  if (attrNs > entityNs)
    dateP->value.s = (char*) entityIso;
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
  if (entityExpP == NULL || entityExpP->type != KjString)
    return;

  uint64_t entityNs = ldIsoToNanoseconds(entityExpP->value.s);
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
      applyToInstance(instP, entityExpP->value.s, entityNs, kjsonP);
    }
  }
}
