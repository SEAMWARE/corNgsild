#ifndef SWNGSILD_LDENTITYMAP_H_
#define SWNGSILD_LDENTITYMAP_H_

//
// FILE            LdEntityMap.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// EntityMap (§ 5.2.39): a cached mapping of entity IDs to their source
// Context Source Registrations. Used for consistent pagination of
// distributed query results.
//
// The entityMap field maps each entityId to an array of source IDs:
// "@none" = local broker, "urn:CSR:X" = registered context source.
//
#include <stdint.h>                                    // uint64_t
#include <stdbool.h>                                   // bool



// -----------------------------------------------------------------------------
//
// LdEntityMapEntry - one entity → sources mapping
//
typedef struct LdEntityMapEntry
{
  char*                      entityId;     // entity ID (malloc'd)
  char**                     sourceIdV;    // NULL-terminated array of source IDs ("@none" or CSR ID)
  int                        sourceCount;
  struct LdEntityMapEntry*   next;
} LdEntityMapEntry;



// -----------------------------------------------------------------------------
//
// LdEntityMap - cached entity map
//
typedef struct LdEntityMap
{
  char*                mapId;          // entity map ID (malloc'd, e.g. "urn:ngsi-ld:EntityMap:xxx")
  uint64_t             expiresAt;      // epoch nanoseconds
  LdEntityMapEntry*    head;           // linked list of entries
  LdEntityMapEntry*    tail;
  int                  entryCount;     // total entries
  void*                tenantP;        // owning tenant (opaque)
  struct LdEntityMap*  next;           // linked list in store
} LdEntityMap;



// -----------------------------------------------------------------------------
//
// LdEntityMapStore - per-tenant store of entity maps
//
typedef struct LdEntityMapStore
{
  LdEntityMap*  head;
  LdEntityMap*  tail;
} LdEntityMapStore;

#endif  // SWNGSILD_LDENTITYMAP_H_
