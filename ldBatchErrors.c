//
// FILE            ldBatchErrors.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stddef.h>                                    // NULL
#include <string.h>                                    // strcmp

#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kalloc/kaStrdup.h"                           // kaStrdup
#include "kalloc/KAlloc.h"                             // KAlloc
#include "corTree/CorNode.h"                           // CorNode
#include "corTree/corTreeBuilder.h"                    // corTreeObject, corTreeArray, corTreeString, corTreeInteger, corTreeChildAdd

#include "corNgsild/LdBatchErrors.h"                    // Own interface



//
// LdBatchErrorList grows in 16-entry chunks. Realloc-out-of-arena: each
// growth allocates a fresh slab and memcpy's the existing entries.
//
#define LD_BATCH_ERROR_CHUNK 16



// -----------------------------------------------------------------------------
//
// ldBatchErrorListInit -
//
void ldBatchErrorListInit(LdBatchErrorList* listP, KAlloc* allocP)
{
  if (listP == NULL)
    return;

  listP->allocP  = allocP;
  listP->count   = 0;
  listP->cap     = 0;
  listP->entries = NULL;
}



// -----------------------------------------------------------------------------
//
// ldBatchErrorListAdd -
//
void ldBatchErrorListAdd(LdBatchErrorList* listP,
                         const char*       entityId,
                         int               statusCode,
                         const char*       errorType,
                         const char*       errorTitle,
                         const char*       errorDetail,
                         const char*       regId)
{
  if (listP == NULL || listP->allocP == NULL)
    return;

  if (listP->count == listP->cap)
  {
    int           newCap     = listP->cap + LD_BATCH_ERROR_CHUNK;
    LdBatchError* newEntries = (LdBatchError*) kaAlloc(listP->allocP, newCap * sizeof(LdBatchError));
    if (newEntries == NULL)
      return;

    for (int i = 0; i < listP->count; i++)
      newEntries[i] = listP->entries[i];

    listP->entries = newEntries;
    listP->cap     = newCap;
  }

  LdBatchError* e = &listP->entries[listP->count++];
  e->entityId    = kaStrdup(listP->allocP, entityId);
  e->statusCode  = statusCode;
  e->errorType   = kaStrdup(listP->allocP, errorType);
  e->errorTitle  = kaStrdup(listP->allocP, errorTitle);
  e->errorDetail = kaStrdup(listP->allocP, errorDetail);
  e->regId       = kaStrdup(listP->allocP, regId);
}



// -----------------------------------------------------------------------------
//
// ldBatchErrorListToTree -
//
CorNode* ldBatchErrorListToTree(const LdBatchErrorList* listP, KAlloc* allocP)
{
  CorNode* arrayP = corTreeArray(allocP, "errors");
  if (listP == NULL)
    return arrayP;

  for (int i = 0; i < listP->count; i++)
  {
    const LdBatchError* e = &listP->entries[i];

    CorNode* entry = corTreeObject(allocP, NULL);
    corTreeChildAdd(entry, corTreeString(allocP, "entityId", e->entityId));

    CorNode* pd = corTreeObject(allocP, "error");
    corTreeChildAdd(pd, corTreeString (allocP, "type", e->errorType));
    corTreeChildAdd(pd, corTreeString (allocP, "title", e->errorTitle));
    corTreeChildAdd(pd, corTreeInteger(allocP, "status", e->statusCode));
    corTreeChildAdd(pd, corTreeString (allocP, "detail", e->errorDetail));
    corTreeChildAdd(entry, pd);

    if (e->regId != NULL)
      corTreeChildAdd(entry, corTreeString(allocP, "registrationId", e->regId));

    corTreeChildAdd(arrayP, entry);
  }

  return arrayP;
}
