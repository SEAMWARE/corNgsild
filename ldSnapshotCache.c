//
// FILE            ldSnapshotCache.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// In-memory Snapshot cache — see header.
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint64_t
#include <stdlib.h>                                      // calloc, free
#include <string.h>                                      // strcmp, strdup

#include "kalloc/kaAlloc.h"                              // kaAlloc
#include "kalloc/kaBufferInit.h"                         // kaBufferInit
#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjLookup.h"                              // kjLookup
#include "kjson/kjClone.h"                               // kjClone
#include "kjson/kjBuilder.h"                             // kjString, kjChildAdd
#include "kjson/kjBufferCreate.h"                        // kjBufferCreate

#include "swRest/swRest.h"                               // swRest
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/LdSnapshotCache.h"                    // Own interface


static Kjson cacheKj;
static bool  cacheKjReady = false;



// -----------------------------------------------------------------------------
//
// ldSnapshotStatusToString -
//
const char* ldSnapshotStatusToString(LdSnapshotStatus s)
{
  switch (s)
  {
    case LdSnapshotPreparing: return "preparing";
    case LdSnapshotSuccess:   return "success";
    case LdSnapshotPartial:   return "partial";
    case LdSnapshotEmpty:     return "empty";
    case LdSnapshotFailure:   return "failure";
  }
  return "preparing";
}



// -----------------------------------------------------------------------------
//
// ldSnapshotCacheCreate -
//
LdSnapshotCache* ldSnapshotCacheCreate(void)
{
  LdSnapshotCache* cacheP = (LdSnapshotCache*) calloc(1, sizeof(LdSnapshotCache));
  if (cacheP == NULL) return NULL;

  kaBufferInit(&cacheP->alloc, cacheP->allocBuf, sizeof(cacheP->allocBuf),
               64 * 1024, NULL, "snapshotCache");

  if (!cacheKjReady)
  {
    kjBufferCreate(&cacheKj, &cacheP->alloc);
    cacheKjReady = true;
  }
  return cacheP;
}



// -----------------------------------------------------------------------------
//
// fieldAsLong - helper
//
static long fieldAsLong(KjNode* tree, const char* name, long def)
{
  KjNode* p = kjLookup(tree, name);
  if (p == NULL) return def;
  if (p->type == KjInt)   return (long) p->value.i;
  if (p->type == KjFloat) return (long) p->value.f;
  return def;
}



// -----------------------------------------------------------------------------
//
// ldSnapshotCacheItemAdd -
//
LdSnapshotCacheItem* ldSnapshotCacheItemAdd(LdSnapshotCache* cacheP, KjNode* snapshotTree)
{
  if (cacheP == NULL || snapshotTree == NULL) return NULL;

  KjNode* idP = kjLookup(snapshotTree, "id");
  if (idP == NULL || idP->type != KjString) return NULL;

  if (ldSnapshotCacheItemLookup(cacheP, idP->value.s) != NULL)
    return NULL;  // already exists

  LdSnapshotCacheItem* itemP = (LdSnapshotCacheItem*) kaAlloc(&cacheP->alloc,
                                                              sizeof(LdSnapshotCacheItem));
  if (itemP == NULL) return NULL;
  memset(itemP, 0, sizeof(*itemP));

  itemP->tree   = kjClone(&cacheKj, snapshotTree);
  itemP->id     = (kjLookup(itemP->tree, "id") != NULL)
                    ? kjLookup(itemP->tree, "id")->value.s
                    : (char*) idP->value.s;
  itemP->status = LdSnapshotPreparing;

  KjNode* prio = kjLookup(itemP->tree, "snapshotPriority");
  itemP->priority = (prio != NULL && (prio->type == KjInt || prio->type == KjFloat))
                      ? (int) fieldAsLong(itemP->tree, "snapshotPriority", 5L)
                      : 5;

  itemP->createdAt  = swRest.requestStartTime;
  itemP->modifiedAt = swRest.requestStartTime;
  itemP->lastUsedAt = swRest.requestStartTime;

  // expiresAt computed by caller; default to 1h from now.
  itemP->expiresAt  = swRest.requestStartTime + 3600ULL * 1000000000ULL;

  itemP->next  = cacheP->head;
  cacheP->head = itemP;
  cacheP->count++;
  return itemP;
}



// -----------------------------------------------------------------------------
//
// ldSnapshotCacheItemLookup -
//
LdSnapshotCacheItem* ldSnapshotCacheItemLookup(LdSnapshotCache* cacheP, const char* id)
{
  if (cacheP == NULL || id == NULL) return NULL;
  for (LdSnapshotCacheItem* p = cacheP->head; p != NULL; p = p->next)
    if (p->id != NULL && strcmp(p->id, id) == 0)
      return p;
  return NULL;
}



// -----------------------------------------------------------------------------
//
// ldSnapshotCacheItemDelete -
//
bool ldSnapshotCacheItemDelete(LdSnapshotCache* cacheP, const char* id)
{
  if (cacheP == NULL || id == NULL) return false;

  LdSnapshotCacheItem* prev = NULL;
  for (LdSnapshotCacheItem* p = cacheP->head; p != NULL; p = p->next)
  {
    if (p->id != NULL && strcmp(p->id, id) == 0)
    {
      if (prev == NULL) cacheP->head = p->next;
      else              prev->next   = p->next;
      cacheP->count--;
      // We don't reclaim p's memory — kalloc is reset on broker restart.
      // For long-lived correctness, a future phase will rebuild the
      // cache periodically.
      return true;
    }
    prev = p;
  }
  return false;
}
