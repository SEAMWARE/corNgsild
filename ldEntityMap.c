//
// FILE            ldEntityMap.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdlib.h>                                    // calloc, malloc, free
#include <string.h>                                    // strdup, strcmp
#include <stdio.h>                                     // snprintf
#include <time.h>                                      // clock_gettime
#include <strings.h>                                   // strcasecmp

#include "corTree/CorNode.h"                           // CorNode
#include "corTree/corTreeBuilder.h"                    // corTreeObject, corTreeString, corTreeArray, corTreeChildAdd

#include "corRest/CorRestState.h"                        // corRest (kallocP — the per-request arena)
#include "corNgsild/CorNgsild.h"                        // corNgsild.entityMapPinned
#include "corNgsild/LdProblem.h"                        // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/ldError.h"                          // ldError
#include "corNgsild/LdEntityMap.h"                      // LdEntityMap, LdEntityMapStore
#include "corNgsild/ldEntityMap.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// ldEntityMapMaxBytes - the memory every EntityMap of the broker may hold together (0: no cap)
//
// See ldEntityMap.h. mapBytesTotal is what they hold now - every map until it is freed, a map
// unlinked from its store but still paged by a request included.
//
int64_t        ldEntityMapMaxBytes = 64LL * 1024 * 1024;
static int64_t mapBytesTotal       = 0;

//
// The estimated cost of one malloc beyond what was asked for (glibc: the chunk header, rounded up).
// An estimate, as is the whole account: what it has to be right about is the order of magnitude.
//
#define MALLOC_OVERHEAD 16



// -----------------------------------------------------------------------------
//
// mapBytesAdd - account n more (or fewer) bytes to mapP and to the total
//
static void mapBytesAdd(LdEntityMap* mapP, int64_t n)
{
  mapP->bytes += n;
  __atomic_add_fetch(&mapBytesTotal, n, __ATOMIC_SEQ_CST);
}



// -----------------------------------------------------------------------------
//
// ldEntityMapBytesTotal - the memory all EntityMaps hold now
//
int64_t ldEntityMapBytesTotal(void)
{
  return __atomic_load_n(&mapBytesTotal, __ATOMIC_SEQ_CST);
}



// -----------------------------------------------------------------------------
//
// noneSourceV - the one source list of every entry held only by this broker ("@none")
//
// Shared by all of them rather than two mallocs per entry: an automatic map of a local query is
// nothing but such entries, and there can be many of them.
//
static char  noneSource[]  = "@none";
static char* noneSourceV[] = { noneSource, NULL };



// -----------------------------------------------------------------------------
//
// nowNanos -
//
static uint64_t nowNanos(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}



// -----------------------------------------------------------------------------
//
// ldEntityMapSetStoredQ - the bound q in its resolved (stored) form - see LdEntityMap.boundQStored
//
void ldEntityMapSetStoredQ(LdEntityMap* mapP, const char* storedQ)
{
  if ((mapP == NULL) || (storedQ == NULL))
    return;

  if (mapP->boundQStored != NULL)
  {
    mapBytesAdd(mapP, -(int64_t) (strlen(mapP->boundQStored) + 1 + MALLOC_OVERHEAD));
    free(mapP->boundQStored);
  }

  mapP->boundQStored = strdup(storedQ);
  mapBytesAdd(mapP, strlen(storedQ) + 1 + MALLOC_OVERHEAD);
}



// -----------------------------------------------------------------------------
//
// isoFromNanos -
//
static void isoFromNanos(uint64_t ns, char* buf, int bufLen)
{
  time_t secs = (time_t)(ns / 1000000000ULL);
  int    ms   = (int)((ns % 1000000000ULL) / 1000000ULL);
  struct tm tm;

  gmtime_r(&secs, &tm);
  snprintf(buf, bufLen, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
           tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
}



// ldEntityMapStoreCreate
LdEntityMapStore* ldEntityMapStoreCreate(void)
{
  LdEntityMapStore* storeP = (LdEntityMapStore*) calloc(1, sizeof(LdEntityMapStore));

  if (storeP != NULL)
    pthread_rwlock_init(&storeP->lock, NULL);

  return storeP;
}



// ldEntityMapCreate
LdEntityMap* ldEntityMapCreate(LdEntityMapStore* storeP, uint64_t lifetimeNs, void* tenantP)
{
  if (storeP == NULL)
    return NULL;

  LdEntityMap* mapP = (LdEntityMap*) calloc(1, sizeof(LdEntityMap));
  if (mapP == NULL)
    return NULL;

  //
  // Generate ID: urn:ngsi-ld:EntityMap:<hex-timestamp><hex-sequence>
  //
  // The sequence number makes it unique: the time alone (it was the time twice over) gave two maps
  // created within the same 65 microseconds the same id - not rare once the broker creates maps by itself.
  //
  static uint32_t sequence = 0;
  uint32_t        seq      = __atomic_add_fetch(&sequence, 1, __ATOMIC_SEQ_CST);
  uint64_t        now      = nowNanos();
  char            idBuf[80];

  snprintf(idBuf, sizeof(idBuf), "urn:ngsi-ld:EntityMap:%08x%04x", (unsigned)(now >> 16), (unsigned)(seq & 0xFFFF));
  mapP->mapId = strdup(idBuf);

  // Default lifetime: 5 minutes if not specified
  if (lifetimeNs == 0)
    lifetimeNs = 5ULL * 60 * 1000000000ULL;
  mapP->expiresAt  = now + lifetimeNs;
  mapP->lifetimeNs = lifetimeNs;
  mapP->lastUsed   = now;
  mapP->tenantP    = tenantP;

  mapP->refCount = 2;   // the store's reference + the caller's pin

  mapBytesAdd(mapP, sizeof(LdEntityMap) + strlen(idBuf) + 1 + 2 * MALLOC_OVERHEAD);

  // Append to store - under the wrlock: requests create, remove and purge concurrently
  pthread_rwlock_wrlock(&storeP->lock);
  if (storeP->tail == NULL)
    storeP->head = mapP;
  else
    storeP->tail->next = mapP;
  storeP->tail = mapP;
  pthread_rwlock_unlock(&storeP->lock);

  return mapP;
}



// ldEntityMapSetFilters
void ldEntityMapSetFilters(LdEntityMap* mapP, const char* type, const char* q,
                           const char* scopeQ, const char* georel, const char* geometry,
                           const char* coordinates, const char* geoproperty)
{
  if (mapP == NULL)
    return;

  if (type        != NULL) mapP->boundType        = strdup(type);
  if (q           != NULL) mapP->boundQ           = strdup(q);
  if (scopeQ      != NULL) mapP->boundScopeQ      = strdup(scopeQ);
  if (georel      != NULL) mapP->boundGeorel      = strdup(georel);
  if (geometry    != NULL) mapP->boundGeometry    = strdup(geometry);
  if (coordinates != NULL) mapP->boundCoordinates = strdup(coordinates);
  if (geoproperty != NULL) mapP->boundGeoproperty = strdup(geoproperty);

  const char* boundV[] = { type, q, scopeQ, georel, geometry, coordinates, geoproperty };
  int64_t     n        = 0;

  for (int ix = 0; ix < (int) (sizeof(boundV) / sizeof(boundV[0])); ix++)
  {
    if (boundV[ix] != NULL)
      n += strlen(boundV[ix]) + 1 + MALLOC_OVERHEAD;
  }

  mapBytesAdd(mapP, n);
}



// ldEntityMapAddEntry
//
// The entry and its entity id are ONE allocation (the id right after the struct), and an entry held
// only by this broker shares noneSourceV - one malloc per entity instead of four.
//
void ldEntityMapAddEntry(LdEntityMap* mapP, const char* entityId,
                          const char** sourceIdV, int sourceCount)
{
  if (mapP == NULL || entityId == NULL)
    return;

  size_t            idLen  = strlen(entityId);
  LdEntityMapEntry* entryP = (LdEntityMapEntry*) calloc(1, sizeof(LdEntityMapEntry) + idLen + 1);
  int64_t           bytes  = sizeof(LdEntityMapEntry) + idLen + 1 + MALLOC_OVERHEAD;

  if (entryP == NULL)
    return;

  entryP->entityId    = (char*) (entryP + 1);
  memcpy(entryP->entityId, entityId, idLen + 1);
  entryP->sourceCount = sourceCount;

  if ((sourceCount == 1) && (sourceIdV != NULL) && (strcmp(sourceIdV[0], noneSource) == 0))
    entryP->sourceIdV = noneSourceV;
  else if (sourceCount > 0 && sourceIdV != NULL)
  {
    entryP->sourceIdV = (char**) malloc((sourceCount + 1) * sizeof(char*));
    bytes += (sourceCount + 1) * sizeof(char*) + MALLOC_OVERHEAD;

    for (int i = 0; i < sourceCount; i++)
    {
      entryP->sourceIdV[i] = strdup(sourceIdV[i]);
      bytes += strlen(sourceIdV[i]) + 1 + MALLOC_OVERHEAD;
    }
    entryP->sourceIdV[sourceCount] = NULL;
  }

  if (mapP->tail == NULL)
    mapP->head = entryP;
  else
    mapP->tail->next = entryP;
  mapP->tail = entryP;
  mapP->entryCount++;

  mapBytesAdd(mapP, bytes);
}



// ldEntityMapLinkedMapLookup
const char* ldEntityMapLinkedMapLookup(LdEntityMap* mapP, const char* csrId)
{
  if (mapP == NULL || csrId == NULL) return NULL;
  for (LdEntityMapLink* p = mapP->linkedHead; p != NULL; p = p->next)
    if (p->csrId != NULL && strcmp(p->csrId, csrId) == 0)
      return p->remoteMapId;
  return NULL;
}



// ldEntityMapAddLinkedMap
void ldEntityMapAddLinkedMap(LdEntityMap* mapP, const char* csrId, const char* remoteMapId)
{
  if (mapP == NULL || csrId == NULL || remoteMapId == NULL)
    return;

  // Dedup: same csrId only carries one remote map id (the most recent wins).
  for (LdEntityMapLink* p = mapP->linkedHead; p != NULL; p = p->next)
  {
    if (p->csrId != NULL && strcmp(p->csrId, csrId) == 0)
    {
      free(p->remoteMapId);
      p->remoteMapId = strdup(remoteMapId);
      return;
    }
  }

  LdEntityMapLink* linkP = (LdEntityMapLink*) calloc(1, sizeof(LdEntityMapLink));
  linkP->csrId       = strdup(csrId);
  linkP->remoteMapId = strdup(remoteMapId);

  mapBytesAdd(mapP, sizeof(LdEntityMapLink) + strlen(csrId) + strlen(remoteMapId) + 2 + 3 * MALLOC_OVERHEAD);

  if (mapP->linkedTail == NULL)
    mapP->linkedHead = linkP;
  else
    mapP->linkedTail->next = linkP;
  mapP->linkedTail = linkP;
}



// ldEntityMapSetExpiresAt
void ldEntityMapSetExpiresAt(LdEntityMap* mapP, uint64_t expiresAtNs)
{
  if (mapP == NULL) return;
  mapP->expiresAt = expiresAtNs;
}



// ldEntityMapLookup
//
// An expired map is not found, purged or not: the purge runs when a map is created, and until then an
// expired map was served as if it were still there.
//
LdEntityMap* ldEntityMapLookup(LdEntityMapStore* storeP, const char* mapId)
{
  if (storeP == NULL || mapId == NULL) return NULL;

  uint64_t now = nowNanos();

  for (LdEntityMap* p = storeP->head; p != NULL; p = p->next)
  {
    if (p->mapId != NULL && strcmp(p->mapId, mapId) == 0)
      return ((p->expiresAt > 0) && (p->expiresAt <= now)) ? NULL : p;
  }

  return NULL;
}



// entryFree
static void entryFree(LdEntityMapEntry* entryP)
{
  if (entryP->entityId != (char*) (entryP + 1))         // not the one allocation with the entry (ldEntityMapAddEntry)
    free(entryP->entityId);

  if ((entryP->sourceIdV != NULL) && (entryP->sourceIdV != noneSourceV))
  {
    for (int i = 0; i < entryP->sourceCount; i++)
      free(entryP->sourceIdV[i]);
    free(entryP->sourceIdV);
  }
  free(entryP);
}



// mapFree
static void mapFree(LdEntityMap* mapP)
{
  __atomic_sub_fetch(&mapBytesTotal, mapP->bytes, __ATOMIC_SEQ_CST);

  free(mapP->mapId);
  LdEntityMapEntry* p = mapP->head;
  while (p != NULL)
  {
    LdEntityMapEntry* next = p->next;
    entryFree(p);
    p = next;
  }
  LdEntityMapLink* lp = mapP->linkedHead;
  while (lp != NULL)
  {
    LdEntityMapLink* next = lp->next;
    free(lp->csrId);
    free(lp->remoteMapId);
    free(lp);
    lp = next;
  }
  if (mapP->boundType        != NULL) free(mapP->boundType);
  if (mapP->boundQ           != NULL) free(mapP->boundQ);
  if (mapP->boundQStored     != NULL) free(mapP->boundQStored);
  if (mapP->boundScopeQ      != NULL) free(mapP->boundScopeQ);
  if (mapP->boundGeorel      != NULL) free(mapP->boundGeorel);
  if (mapP->boundGeometry    != NULL) free(mapP->boundGeometry);
  if (mapP->boundCoordinates != NULL) free(mapP->boundCoordinates);
  if (mapP->boundGeoproperty != NULL) free(mapP->boundGeoproperty);
  if (mapP->queryParamV != NULL)
  {
    for (int ix = 0; ix < 2 * mapP->queryParamCount; ix++)
      free(mapP->queryParamV[ix]);
    free(mapP->queryParamV);
  }
  free(mapP);
}



// ldEntityMapRemove
bool ldEntityMapRemove(LdEntityMapStore* storeP, const char* mapId)
{
  if (storeP == NULL || mapId == NULL) return false;

  LdEntityMap* found = NULL;
  LdEntityMap* prev  = NULL;

  pthread_rwlock_wrlock(&storeP->lock);
  for (LdEntityMap* p = storeP->head; p != NULL; p = p->next)
  {
    if (p->mapId != NULL && strcmp(p->mapId, mapId) == 0)
    {
      if (prev == NULL) storeP->head = p->next;
      else              prev->next   = p->next;
      if (storeP->tail == p) storeP->tail = prev;
      found = p;
      break;
    }
    prev = p;
  }
  pthread_rwlock_unlock(&storeP->lock);

  // The store's reference - freed now, or by whoever still pages it, when done
  if (found != NULL)
    ldEntityMapUnpin(found);

  return (found != NULL);
}



// ldEntityMapToTree
CorNode* ldEntityMapToTree(LdEntityMap* mapP)
{
  if (mapP == NULL) return NULL;

  CorNode* treeP = corTreeObject(corRest.kallocP, NULL);

  corTreeChildAdd(treeP, corTreeString(corRest.kallocP, "id", mapP->mapId));
  corTreeChildAdd(treeP, corTreeString(corRest.kallocP, "type", "EntityMap"));

  char isoBuf[64];
  isoFromNanos(mapP->expiresAt, isoBuf, sizeof(isoBuf));
  corTreeChildAdd(treeP, corTreeString(corRest.kallocP, "expiresAt", isoBuf));

  // entityMap: { "urn:e1": ["@none", "urn:CSR:1"], "urn:e2": ["urn:CSR:2"], ... }
  CorNode* emObj = corTreeObject(corRest.kallocP, "entityMap");
  for (LdEntityMapEntry* entryP = mapP->head; entryP != NULL; entryP = entryP->next)
  {
    CorNode* sourcesArr = corTreeArray(corRest.kallocP, entryP->entityId);
    for (int i = 0; i < entryP->sourceCount; i++)
      corTreeChildAdd(sourcesArr, corTreeString(corRest.kallocP, NULL, entryP->sourceIdV[i]));
    corTreeChildAdd(emObj, sourcesArr);
  }
  corTreeChildAdd(treeP, emObj);

  // linkedMaps: { "<csrId>": "<remoteMapId>", ... }  (§ 5.14.4.4)
  CorNode* linkedObj = corTreeObject(corRest.kallocP, "linkedMaps");
  for (LdEntityMapLink* linkP = mapP->linkedHead; linkP != NULL; linkP = linkP->next)
  {
    if (linkP->csrId == NULL || linkP->remoteMapId == NULL)
      continue;
    corTreeChildAdd(linkedObj, corTreeString(corRest.kallocP, linkP->csrId, linkP->remoteMapId));
  }
  corTreeChildAdd(treeP, linkedObj);

  return treeP;
}



// ldEntityMapPurgeExpired
void ldEntityMapPurgeExpired(LdEntityMapStore* storeP)
{
  if (storeP == NULL) return;

  uint64_t     now     = nowNanos();
  LdEntityMap* prev    = NULL;
  LdEntityMap* expired = NULL;         // unlinked here, their store references dropped below

  pthread_rwlock_wrlock(&storeP->lock);
  LdEntityMap* p = storeP->head;

  while (p != NULL)
  {
    LdEntityMap* next = p->next;
    if (p->expiresAt > 0 && p->expiresAt <= now)
    {
      if (prev == NULL) storeP->head = next;
      else              prev->next   = next;
      if (storeP->tail == p) storeP->tail = prev;
      p->next = expired;
      expired = p;
    }
    else
    {
      prev = p;
    }
    p = next;
  }
  pthread_rwlock_unlock(&storeP->lock);

  //
  // It freed them right here - while another request was paging one of them (a PATCH of the
  // map's expiresAt to the past is enough to make that happen on the next GET). Now a map being
  // paged is freed by that request, when it is done with it.
  //
  while (expired != NULL)
  {
    LdEntityMap* next = expired->next;
    ldEntityMapUnpin(expired);
    expired = next;
  }
}



// -----------------------------------------------------------------------------
//
// Locking and references - see ldEntityMap.h
//
void ldEntityMapStoreRdLock(LdEntityMapStore* storeP) { if (storeP != NULL) pthread_rwlock_rdlock(&storeP->lock); }
void ldEntityMapStoreUnlock(LdEntityMapStore* storeP) { if (storeP != NULL) pthread_rwlock_unlock(&storeP->lock); }

void ldEntityMapPin(LdEntityMap* mapP)
{
  if (mapP != NULL)
    __atomic_add_fetch(&mapP->refCount, 1, __ATOMIC_SEQ_CST);
}

void ldEntityMapUnpin(LdEntityMap* mapP)
{
  if ((mapP != NULL) && (__atomic_sub_fetch(&mapP->refCount, 1, __ATOMIC_SEQ_CST) == 0))
    mapFree(mapP);       // out of the store (it holds a reference while the map is in it)
}

LdEntityMap* ldEntityMapLookupPinned(LdEntityMapStore* storeP, const char* mapId)
{
  if (storeP == NULL)
    return NULL;

  pthread_rwlock_rdlock(&storeP->lock);
  LdEntityMap* mapP = ldEntityMapLookup(storeP, mapId);
  if (mapP != NULL)
    ldEntityMapPin(mapP);
  pthread_rwlock_unlock(&storeP->lock);

  return mapP;
}

void ldEntityMapRequestPin(LdEntityMap* mapP)
{
  ldEntityMapRequestRelease();        // one per request
  corNgsild.entityMapPinned = mapP;
}

void ldEntityMapRequestRelease(void)
{
  if (corNgsild.entityMapPinned == NULL)
    return;

  ldEntityMapUnpin(corNgsild.entityMapPinned);
  corNgsild.entityMapPinned = NULL;
}



// -----------------------------------------------------------------------------
//
// ldEntityMapTouch - a page is served from mapP: it was used now, and an automatic map lives on
//
void ldEntityMapTouch(LdEntityMap* mapP)
{
  if (mapP == NULL)
    return;

  uint64_t now = nowNanos();

  mapP->lastUsed = now;

  if (mapP->automatic)
    mapP->expiresAt = now + mapP->lifetimeNs;
}



// -----------------------------------------------------------------------------
//
// ldEntityMapAdmit - mapP is filled: keep it if the maps' memory allows it
//
// See ldEntityMap.h. The expired maps go first, then the automatic maps of this store, least
// recently used first - never mapP itself, never a map a client asked for. If that is not enough,
// mapP is taken out of the store (the caller's pin still holds it) and false is returned.
//
bool ldEntityMapAdmit(LdEntityMapStore* storeP, LdEntityMap* mapP)
{
  if ((storeP == NULL) || (mapP == NULL) || (ldEntityMapMaxBytes <= 0))
    return true;

  if (ldEntityMapBytesTotal() <= ldEntityMapMaxBytes)
    return true;

  if (mapP->bytes > ldEntityMapMaxBytes)               // too big on its own: nothing else has to go for it
  {
    ldEntityMapRemove(storeP, mapP->mapId);
    return false;
  }

  ldEntityMapPurgeExpired(storeP);

  while (ldEntityMapBytesTotal() > ldEntityMapMaxBytes)
  {
    LdEntityMap* lruP     = NULL;
    LdEntityMap* lruPrevP = NULL;
    LdEntityMap* prevP    = NULL;

    pthread_rwlock_wrlock(&storeP->lock);
    for (LdEntityMap* p = storeP->head; p != NULL; p = p->next)
    {
      if ((p != mapP) && (p->automatic == true) && ((lruP == NULL) || (p->lastUsed < lruP->lastUsed)))
      {
        lruP     = p;
        lruPrevP = prevP;
      }
      prevP = p;
    }

    if (lruP != NULL)
    {
      if (lruPrevP == NULL) storeP->head   = lruP->next;
      else                  lruPrevP->next = lruP->next;
      if (storeP->tail == lruP) storeP->tail = lruPrevP;
      lruP->next = NULL;
    }
    pthread_rwlock_unlock(&storeP->lock);

    if (lruP == NULL)
      break;

    ldEntityMapUnpin(lruP);    // the store's reference - freed now, or by the request paging it
  }

  if (ldEntityMapBytesTotal() <= ldEntityMapMaxBytes)
    return true;

  ldEntityMapRemove(storeP, mapP->mapId);
  return false;
}



// -----------------------------------------------------------------------------
//
// ldEntityMapRequestHeader - the NGSILD-EntityMap request header: the EntityMap to use (TS 104-176 clause 7)
//
// The header is the alternative to ?entityMap=<id>: "If present, the EntityMap supplied is used for
// determining the set of Entities requested during the query operation". Its value is the map's
// resource URI (/ngsi-ld/v1/entityMaps/<id>) - or just the id; the id is the last path segment.
//
// Both forms naming DIFFERENT maps is a contradiction: 400 BadRequestData and false. The same map
// twice is fine. With ?entityMap=true the supplied map is used - the flag asks for a map to be
// returned, "and no Entity Map currently exists" is when a new one is created (§ 10.4.3).
//
// Read before the request's parameters are validated: a page served from a map needs no selector of
// its own (ldParamsValidate), whichever form named the map.
//
bool ldEntityMapRequestHeader(void)
{
  for (int i = 0; i < corRest.in.httpHeaderCount; i++)
  {
    if (strcasecmp(corRest.in.httpHeaderV[i].key, "NGSILD-EntityMap") != 0)
      continue;

    const char* val = corRest.in.httpHeaderV[i].value;

    if ((val == NULL) || (val[0] == 0))
      return true;

    const char* slash = strrchr(val, '/');
    const char* mapId = ((slash != NULL) && (slash[1] != 0)) ? slash + 1 : val;

    if ((corNgsild.entityMapId != NULL) && (strcmp(corNgsild.entityMapId, mapId) != 0))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Bad Request",
              "the NGSILD-EntityMap header names EntityMap '%s', the URL parameter 'entityMap' names '%s'",
              mapId, corNgsild.entityMapId);
      return false;
    }

    corNgsild.entityMapId     = (char*) mapId;
    corNgsild.entityMapCreate = false;
    return true;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldEntityMapSetQueryParams - the URL parameters of the creating request into the map (LdEntityMap.queryParamV)
//
void ldEntityMapSetQueryParams(LdEntityMap* mapP)
{
  if ((mapP == NULL) || (corRest.in.uriParamCount <= 0))
    return;

  char**  pairV = (char**) calloc(2 * corRest.in.uriParamCount, sizeof(char*));
  int     n     = 0;
  int64_t bytes = 2 * corRest.in.uriParamCount * sizeof(char*) + MALLOC_OVERHEAD;

  if (pairV == NULL)
    return;

  for (int i = 0; i < corRest.in.uriParamCount; i++)
  {
    const char* key   = corRest.in.uriParamV[i].key;
    const char* value = (corRest.in.uriParamV[i].value != NULL) ? corRest.in.uriParamV[i].value : "";

    if ((strcmp(key, "limit")             == 0) || (strcmp(key, "offset")    == 0) ||
        (strcmp(key, "entityMap")         == 0) || (strcmp(key, "entityMapLifetime") == 0))
      continue;

    pairV[2 * n]     = strdup(key);
    pairV[2 * n + 1] = strdup(value);
    bytes           += strlen(key) + strlen(value) + 2 + 2 * MALLOC_OVERHEAD;
    n++;
  }

  mapP->queryParamV     = pairV;
  mapP->queryParamCount = n;
  mapBytesAdd(mapP, bytes);
}



// -----------------------------------------------------------------------------
//
// ldEntityMapQueryParam - the value of a URL parameter of the creating request (NULL: it had none)
//
const char* ldEntityMapQueryParam(LdEntityMap* mapP, const char* key)
{
  if ((mapP == NULL) || (mapP->queryParamV == NULL))
    return NULL;

  for (int ix = 0; ix < mapP->queryParamCount; ix++)
  {
    if (strcmp(mapP->queryParamV[2 * ix], key) == 0)
      return mapP->queryParamV[2 * ix + 1];
  }

  return NULL;
}
