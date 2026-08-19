//
// FILE            ldThrottleDirty.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <pthread.h>                                   // pthread_mutex_*
#include <stdlib.h>                                    // malloc, realloc, free
#include <string.h>                                    // strdup, strcmp
#include <stddef.h>                                    // NULL

#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjFree.h"                              // kjFree

#include "corNgsild/ldSubscriptionNotify.h"            // LdNotifyEntityDelete
#include "corNgsild/ldThrottleDirty.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// entryFreeContents - free the malloc'd members of one dirty entry
//
static void entryFreeContents(LdThrottleEntry* e)
{
  if (e->entityId != NULL)
    free(e->entityId);
  if (e->deleteState != NULL)
    kjFree(e->deleteState);
}



// -----------------------------------------------------------------------------
//
// ldThrottleDirtyUpsert -
//
void ldThrottleDirtyUpsert(LdSubCacheItem* itemP,
                           const char*     entityId,
                           int             reasonsMask,
                           int             op,
                           uint64_t        deletedAtNs,
                           KjNode*         entityP)
{
  if (itemP == NULL || entityId == NULL)
    return;

  pthread_mutex_lock(&itemP->dirtyLock);

  // Dedup: find an existing entry for this id.
  LdThrottleEntry* e = NULL;
  for (int i = 0; i < itemP->dirtyN; i++)
  {
    if (strcmp(itemP->dirtyV[i].entityId, entityId) == 0)
    {
      e = &itemP->dirtyV[i];
      break;
    }
  }

  if (e == NULL)
  {
    if (itemP->dirtyN == itemP->dirtyAlloc)
    {
      int newAlloc = (itemP->dirtyAlloc == 0) ? 8 : itemP->dirtyAlloc * 2;
      itemP->dirtyV = (LdThrottleEntry*) realloc(itemP->dirtyV, newAlloc * sizeof(LdThrottleEntry));
      itemP->dirtyAlloc = newAlloc;
    }

    e = &itemP->dirtyV[itemP->dirtyN++];
    e->entityId    = strdup(entityId);
    e->reasonsMask = 0;
    e->op          = LdNotifyEntityUpdate;
    e->deletedAtNs = 0;
    e->deleteState = NULL;
  }

  e->reasonsMask |= reasonsMask;

  // Delete wins over an earlier update — and a deleted entity can't be
  // re-queried at flush, so capture its final state now.
  if (op == LdNotifyEntityDelete)
  {
    e->op          = LdNotifyEntityDelete;
    e->deletedAtNs = deletedAtNs;
    if (e->deleteState != NULL)
      kjFree(e->deleteState);
    e->deleteState = (entityP != NULL) ? kjClone(NULL, entityP) : NULL;
  }

  pthread_mutex_unlock(&itemP->dirtyLock);
}



// -----------------------------------------------------------------------------
//
// ldThrottleDirtyDrain -
//
void ldThrottleDirtyDrain(LdSubCacheItem* itemP, LdThrottleEntry** outV, int* outN)
{
  pthread_mutex_lock(&itemP->dirtyLock);

  *outV = itemP->dirtyV;
  *outN = itemP->dirtyN;

  itemP->dirtyV     = NULL;
  itemP->dirtyN     = 0;
  itemP->dirtyAlloc = 0;

  pthread_mutex_unlock(&itemP->dirtyLock);
}



// -----------------------------------------------------------------------------
//
// ldThrottleDirtyEntriesFree -
//
void ldThrottleDirtyEntriesFree(LdThrottleEntry* v, int n)
{
  if (v == NULL)
    return;

  for (int i = 0; i < n; i++)
    entryFreeContents(&v[i]);

  free(v);
}



// -----------------------------------------------------------------------------
//
// ldThrottleDirtyFree -
//
void ldThrottleDirtyFree(LdSubCacheItem* itemP)
{
  if (itemP->dirtyV != NULL)
  {
    for (int i = 0; i < itemP->dirtyN; i++)
      entryFreeContents(&itemP->dirtyV[i]);
    free(itemP->dirtyV);
  }

  itemP->dirtyV     = NULL;
  itemP->dirtyN     = 0;
  itemP->dirtyAlloc = 0;
}
