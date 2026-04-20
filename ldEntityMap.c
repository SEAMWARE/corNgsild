//
// FILE            ldEntityMap.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdlib.h>                                    // calloc, malloc, free
#include <string.h>                                    // strdup, strcmp
#include <stdio.h>                                     // snprintf
#include <time.h>                                      // clock_gettime

#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjBuilder.h"                           // kjObject, kjString, kjArray, kjChildAdd

#include "swNgsild/LdEntityMap.h"                      // LdEntityMap, LdEntityMapStore
#include "swNgsild/ldEntityMap.h"                      // Own interface



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
  return (LdEntityMapStore*) calloc(1, sizeof(LdEntityMapStore));
}



// ldEntityMapCreate
LdEntityMap* ldEntityMapCreate(LdEntityMapStore* storeP, uint64_t lifetimeNs, void* tenantP)
{
  if (storeP == NULL)
    return NULL;

  LdEntityMap* mapP = (LdEntityMap*) calloc(1, sizeof(LdEntityMap));

  // Generate ID: urn:ngsi-ld:EntityMap:<hex-timestamp>
  char idBuf[80];
  snprintf(idBuf, sizeof(idBuf), "urn:ngsi-ld:EntityMap:%08x%04x",
           (unsigned)(nowNanos() >> 16), (unsigned)(nowNanos() & 0xFFFF));
  mapP->mapId = strdup(idBuf);

  // Default lifetime: 5 minutes if not specified
  if (lifetimeNs == 0)
    lifetimeNs = 5ULL * 60 * 1000000000ULL;
  mapP->expiresAt = nowNanos() + lifetimeNs;
  mapP->tenantP   = tenantP;

  // Append to store
  if (storeP->tail == NULL)
    storeP->head = mapP;
  else
    storeP->tail->next = mapP;
  storeP->tail = mapP;

  return mapP;
}



// ldEntityMapAddEntry
void ldEntityMapAddEntry(LdEntityMap* mapP, const char* entityId,
                          const char** sourceIdV, int sourceCount)
{
  if (mapP == NULL || entityId == NULL)
    return;

  LdEntityMapEntry* entryP = (LdEntityMapEntry*) calloc(1, sizeof(LdEntityMapEntry));
  entryP->entityId    = strdup(entityId);
  entryP->sourceCount = sourceCount;

  if (sourceCount > 0 && sourceIdV != NULL)
  {
    entryP->sourceIdV = (char**) malloc((sourceCount + 1) * sizeof(char*));
    for (int i = 0; i < sourceCount; i++)
      entryP->sourceIdV[i] = strdup(sourceIdV[i]);
    entryP->sourceIdV[sourceCount] = NULL;
  }

  if (mapP->tail == NULL)
    mapP->head = entryP;
  else
    mapP->tail->next = entryP;
  mapP->tail = entryP;
  mapP->entryCount++;
}



// ldEntityMapSetExpiresAt
void ldEntityMapSetExpiresAt(LdEntityMap* mapP, uint64_t expiresAtNs)
{
  if (mapP == NULL) return;
  mapP->expiresAt = expiresAtNs;
}



// ldEntityMapLookup
LdEntityMap* ldEntityMapLookup(LdEntityMapStore* storeP, const char* mapId)
{
  if (storeP == NULL || mapId == NULL) return NULL;
  for (LdEntityMap* p = storeP->head; p != NULL; p = p->next)
    if (p->mapId != NULL && strcmp(p->mapId, mapId) == 0)
      return p;
  return NULL;
}



// entryFree
static void entryFree(LdEntityMapEntry* entryP)
{
  free(entryP->entityId);
  if (entryP->sourceIdV != NULL)
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
  free(mapP->mapId);
  LdEntityMapEntry* p = mapP->head;
  while (p != NULL)
  {
    LdEntityMapEntry* next = p->next;
    entryFree(p);
    p = next;
  }
  free(mapP);
}



// ldEntityMapRemove
bool ldEntityMapRemove(LdEntityMapStore* storeP, const char* mapId)
{
  if (storeP == NULL || mapId == NULL) return false;

  LdEntityMap* prev = NULL;
  for (LdEntityMap* p = storeP->head; p != NULL; p = p->next)
  {
    if (p->mapId != NULL && strcmp(p->mapId, mapId) == 0)
    {
      if (prev == NULL) storeP->head = p->next;
      else              prev->next   = p->next;
      if (storeP->tail == p) storeP->tail = prev;
      mapFree(p);
      return true;
    }
    prev = p;
  }
  return false;
}



// ldEntityMapToTree
KjNode* ldEntityMapToTree(LdEntityMap* mapP)
{
  if (mapP == NULL) return NULL;

  KjNode* treeP = kjObject(NULL, NULL);

  kjChildAdd(treeP, kjString(NULL, "id", mapP->mapId));
  kjChildAdd(treeP, kjString(NULL, "type", "EntityMap"));

  char isoBuf[64];
  isoFromNanos(mapP->expiresAt, isoBuf, sizeof(isoBuf));
  kjChildAdd(treeP, kjString(NULL, "expiresAt", isoBuf));

  // entityMap: { "urn:e1": ["@none", "urn:CSR:1"], "urn:e2": ["urn:CSR:2"], ... }
  KjNode* emObj = kjObject(NULL, "entityMap");
  for (LdEntityMapEntry* entryP = mapP->head; entryP != NULL; entryP = entryP->next)
  {
    KjNode* sourcesArr = kjArray(NULL, entryP->entityId);
    for (int i = 0; i < entryP->sourceCount; i++)
      kjChildAdd(sourcesArr, kjString(NULL, NULL, entryP->sourceIdV[i]));
    kjChildAdd(emObj, sourcesArr);
  }
  kjChildAdd(treeP, emObj);

  // linkedMaps: empty for now (no cascaded entity maps)
  kjChildAdd(treeP, kjObject(NULL, "linkedMaps"));

  return treeP;
}



// ldEntityMapPurgeExpired
void ldEntityMapPurgeExpired(LdEntityMapStore* storeP)
{
  if (storeP == NULL) return;

  uint64_t now = nowNanos();
  LdEntityMap* prev = NULL;
  LdEntityMap* p    = storeP->head;

  while (p != NULL)
  {
    LdEntityMap* next = p->next;
    if (p->expiresAt > 0 && p->expiresAt <= now)
    {
      if (prev == NULL) storeP->head = next;
      else              prev->next   = next;
      if (storeP->tail == p) storeP->tail = prev;
      mapFree(p);
    }
    else
    {
      prev = p;
    }
    p = next;
  }
}
