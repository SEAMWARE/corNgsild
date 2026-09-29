//
// FILE            ldSnapshotCache.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// In-memory Snapshot cache — see header.
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint64_t
#include <stdlib.h>                                      // calloc, free
#include <string.h>                                      // strcmp, strdup

#include "corTree/CorNode.h"                             // CorNode
#include "corTree/corTreeLookup.h"                       // corTreeLookup
#include "corTree/corTreeClone.h"                        // corTreeClone
#include "corTree/corTreeFree.h"                         // corTreeFree
#include "corTree/corTreeBuilder.h"                      // corTreeString, corTreeChildAdd

#include "corRest/corRest.h"                               // corRest
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "corNgsild/CorNgsild.h"                          // corNgsild.snapshotPinned
#include "corNgsild/LdSnapshotCache.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ldSnapshotCacheCreate -
//
LdSnapshotCache* ldSnapshotCacheCreate(void)
{
  // Cache items are individually malloc'd (calloc) and their snapshot trees
  // are malloc clones, so deleting a snapshot truly reclaims its memory
  // (ldSnapshotCacheItemDelete). No per-cache arena is needed.
  LdSnapshotCache* cacheP = (LdSnapshotCache*) calloc(1, sizeof(LdSnapshotCache));

  if (cacheP != NULL)
    pthread_rwlock_init(&cacheP->lock, NULL);

  return cacheP;
}



// -----------------------------------------------------------------------------
//
// fieldAsLong - helper
//
static long fieldAsLong(CorNode* tree, const char* name, long def)
{
  CorNode* p = corTreeLookup(tree, name);
  if (p == NULL) return def;
  if (p->type == CorInt)  return (long) p->value.i;
  if (p->type == CorFloat) return (long) p->value.f;
  return def;
}



// -----------------------------------------------------------------------------
//
// ldSnapshotCacheItemAdd -
//
LdSnapshotCacheItem* ldSnapshotCacheItemAdd(LdSnapshotCache* cacheP, CorNode* snapshotTree)
{
  if (cacheP == NULL || snapshotTree == NULL) return NULL;

  CorNode* idP = corTreeLookup(snapshotTree, "id");
  if (idP == NULL || idP->type != CorString) return NULL;

  //
  // The existence check, the sequence number and the link under ONE wrlock: two POSTs with the
  // same id could both pass the check (and the caller did the same check first, unlocked), and
  // two at once could get the same snapSeq - the same snap-tenant database.
  //
  pthread_rwlock_wrlock(&cacheP->lock);

  if (ldSnapshotCacheItemLookup(cacheP, idP->value.s) != NULL)
  {
    pthread_rwlock_unlock(&cacheP->lock);
    return NULL;  // already exists
  }

  LdSnapshotCacheItem* itemP = (LdSnapshotCacheItem*) calloc(1, sizeof(LdSnapshotCacheItem));
  if (itemP == NULL)
  {
    pthread_rwlock_unlock(&cacheP->lock);
    return NULL;
  }

  // Clone the snapshot doc with the malloc allocator (NULL) so it survives the
  // request/worker that created it; freed in ldSnapshotCacheItemDelete via
  // corTreeFree. Any later grafts (postSnapshot _snapSeq, ldSnapshotExecQueries
  // details, patchSnapshot fragments) must likewise use NULL=malloc to keep
  // the tree a single clean all-malloc tree that corTreeFree can release whole.
  itemP->tree   = corTreeClone(NULL, snapshotTree);
  itemP->id     = (corTreeLookup(itemP->tree, "id") != NULL)
                    ? corTreeLookup(itemP->tree, "id")->value.s
                    : (char*) idP->value.s;
  itemP->status = LdSnapshotPreparing;

  CorNode* prio = corTreeLookup(itemP->tree, "snapshotPriority");
  itemP->priority = (prio != NULL && (prio->type == CorInt || prio->type == CorFloat))
                      ? (int) fieldAsLong(itemP->tree, "snapshotPriority", 5L)
                      : 5;

  itemP->createdAt  = corRest.requestStartTime;
  itemP->modifiedAt = corRest.requestStartTime;
  itemP->lastUsedAt = corRest.requestStartTime;

  // expiresAt computed by caller; default to 1h from now.
  itemP->expiresAt  = corRest.requestStartTime + 3600ULL * 1000000000ULL;

  // Monotonic per-tenant sequence — used to name the snap-tenant DB
  // (see snapshotTenantCreate). Reload at boot bumps nextSnapSeq to
  // max(persisted snapSeq) + 1 so newly-created snapshots never reuse
  // a name that's still on disk.
  itemP->snapSeq = cacheP->nextSnapSeq++;

  itemP->refCount = 2;          // the cache's reference + the caller's pin

  itemP->next  = cacheP->head;
  cacheP->head = itemP;
  cacheP->count++;

  pthread_rwlock_unlock(&cacheP->lock);
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

  LdSnapshotCacheItem* found = NULL;
  LdSnapshotCacheItem* prev  = NULL;

  pthread_rwlock_wrlock(&cacheP->lock);
  for (LdSnapshotCacheItem* p = cacheP->head; p != NULL; p = p->next)
  {
    if (p->id != NULL && strcmp(p->id, id) == 0)
    {
      if (prev == NULL) cacheP->head = p->next;
      else              prev->next   = p->next;
      cacheP->count--;
      found = p;
      break;
    }
    prev = p;
  }
  pthread_rwlock_unlock(&cacheP->lock);

  //
  // Unlinked: nobody can pin it any more. Drop the cache's reference - destroyed now if nobody
  // has it pinned, else by the last unpin. It used to be freed here, while the capture worker
  // or a read could be using it, and the snapshot tenant was then destroyed by the caller -
  // twice, by two concurrent DELETEs, which both found the item.
  //
  if (found != NULL)
    ldSnapshotCacheItemUnpin(found);

  return (found != NULL);
}



// -----------------------------------------------------------------------------
//
// Locking and references - see LdSnapshotCache.h
//
static LdSnapshotItemDestroyFn destroyHook = NULL;

void ldSnapshotCacheDestroyHookSet(LdSnapshotItemDestroyFn fn) { destroyHook = fn; }

void ldSnapshotCacheRdLock(LdSnapshotCache* cacheP) { if (cacheP != NULL) pthread_rwlock_rdlock(&cacheP->lock); }
void ldSnapshotCacheWrLock(LdSnapshotCache* cacheP) { if (cacheP != NULL) pthread_rwlock_wrlock(&cacheP->lock); }
void ldSnapshotCacheUnlock(LdSnapshotCache* cacheP) { if (cacheP != NULL) pthread_rwlock_unlock(&cacheP->lock); }

void ldSnapshotCacheItemPin(LdSnapshotCacheItem* itemP)
{
  if (itemP != NULL)
    __atomic_add_fetch(&itemP->refCount, 1, __ATOMIC_SEQ_CST);
}

void ldSnapshotCacheItemUnpin(LdSnapshotCacheItem* itemP)
{
  if (itemP == NULL)
    return;

  if (__atomic_sub_fetch(&itemP->refCount, 1, __ATOMIC_SEQ_CST) != 0)
    return;

  // The last reference: the item is out of the cache (the cache holds one while it is in it)
  if (destroyHook != NULL)
    destroyHook(itemP);

  // p->id points into p->tree, so corTreeFree reclaims it too
  if (itemP->tree != NULL)
    corTreeFree(itemP->tree);
  free(itemP);
}

LdSnapshotCacheItem* ldSnapshotCacheItemLookupPinned(LdSnapshotCache* cacheP, const char* id)
{
  if (cacheP == NULL)
    return NULL;

  pthread_rwlock_rdlock(&cacheP->lock);
  LdSnapshotCacheItem* itemP = ldSnapshotCacheItemLookup(cacheP, id);
  if (itemP != NULL)
    ldSnapshotCacheItemPin(itemP);
  pthread_rwlock_unlock(&cacheP->lock);

  return itemP;
}



// -----------------------------------------------------------------------------
//
// ldSnapshotRequestRelease -
//
void ldSnapshotRequestRelease(void)
{
  if (corNgsild.snapshotPinned == NULL)
    return;

  ldSnapshotCacheItemUnpin(corNgsild.snapshotPinned);
  corNgsild.snapshotPinned = NULL;
}
