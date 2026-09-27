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
  return (LdSnapshotCache*) calloc(1, sizeof(LdSnapshotCache));
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

  if (ldSnapshotCacheItemLookup(cacheP, idP->value.s) != NULL)
    return NULL;  // already exists

  LdSnapshotCacheItem* itemP = (LdSnapshotCacheItem*) calloc(1, sizeof(LdSnapshotCacheItem));
  if (itemP == NULL) return NULL;

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
      // p->id points into p->tree, so corTreeFree reclaims it too. The snap-tenant
      // (p->snapTenantP) is owned and freed by the caller (deleteSnapshot /
      // purgeSnapshots, via snapshotTenantDestroy) — it must be captured
      // before this call, as p is freed here.
      if (p->tree != NULL)
        corTreeFree(p->tree);
      free(p);
      return true;
    }
    prev = p;
  }
  return false;
}
