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
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjson.h"                               // Kjson
#include "kjson/kjBuilder.h"                           // kjObject, kjArray, kjString, kjInteger, kjChildAdd

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
KjNode* ldBatchErrorListToTree(const LdBatchErrorList* listP, Kjson* kjsonP)
{
  KjNode* arrayP = kjArray(kjsonP, "errors");
  if (listP == NULL)
    return arrayP;

  for (int i = 0; i < listP->count; i++)
  {
    const LdBatchError* e = &listP->entries[i];

    KjNode* entry = kjObject(kjsonP, NULL);
    kjChildAdd(entry, kjString(kjsonP, "entityId", e->entityId));

    KjNode* pd = kjObject(kjsonP, "error");
    kjChildAdd(pd, kjString (kjsonP, "type",   e->errorType));
    kjChildAdd(pd, kjString (kjsonP, "title",  e->errorTitle));
    kjChildAdd(pd, kjInteger(kjsonP, "status", e->statusCode));
    kjChildAdd(pd, kjString (kjsonP, "detail", e->errorDetail));
    kjChildAdd(entry, pd);

    if (e->regId != NULL)
      kjChildAdd(entry, kjString(kjsonP, "registrationId", e->regId));

    kjChildAdd(arrayP, entry);
  }

  return arrayP;
}



// -----------------------------------------------------------------------------
//
// ldBatchErrorListSingleStatus -
//
int ldBatchErrorListSingleStatus(const LdBatchErrorList* listP)
{
  if (listP == NULL || listP->count == 0)
    return -1;

  int s = listP->entries[0].statusCode;
  for (int i = 1; i < listP->count; i++)
    if (listP->entries[i].statusCode != s)
      return -1;

  return s;
}



// -----------------------------------------------------------------------------
//
// ldBatchErrorListFirstAsProblemDetails -
//
KjNode* ldBatchErrorListFirstAsProblemDetails(const LdBatchErrorList* listP, Kjson* kjsonP)
{
  if (listP == NULL || listP->count == 0)
    return NULL;

  const LdBatchError* e = &listP->entries[0];

  KjNode* pd = kjObject(kjsonP, NULL);
  kjChildAdd(pd, kjString (kjsonP, "type",   e->errorType));
  kjChildAdd(pd, kjString (kjsonP, "title",  e->errorTitle));
  kjChildAdd(pd, kjInteger(kjsonP, "status", e->statusCode));
  kjChildAdd(pd, kjString (kjsonP, "detail", e->errorDetail));

  return pd;
}
