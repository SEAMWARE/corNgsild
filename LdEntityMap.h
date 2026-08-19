#ifndef CORNGSILD_LDENTITYMAP_H_
#define CORNGSILD_LDENTITYMAP_H_

//
// FILE            LdEntityMap.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
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
// LdEntityMapLink - one (CSR id → remote EntityMap id) mapping
//
// § 5.14.4.4: when a distributed EntityMap creation forwards POST /entityMap
// to a CSR, the CP returns its own (local-to-it) EntityMap. The local broker
// records the (CSR.regId → remote-EntityMap.id) pair so that subsequent
// pagination can re-use the CP's frozen snapshot instead of re-querying.
//
typedef struct LdEntityMapLink
{
  char*                    csrId;         // CSR registration id (malloc'd)
  char*                    remoteMapId;   // remote EntityMap id at that CSR (malloc'd)
  struct LdEntityMapLink*  next;
} LdEntityMapLink;



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
  LdEntityMapLink*     linkedHead;     // linked-maps list (CSR → remote map)
  LdEntityMapLink*     linkedTail;
  void*                tenantP;        // owning tenant (opaque)

  // Bound query filters (§ 9 "shall use the same parameters") — the filter
  // URL params present on the request that created the map. On reuse a bound
  // filter may be re-sent with the SAME value or omitted (then re-applied),
  // but not modified or newly introduced (→ 400). malloc'd, or NULL if absent.
  char*                boundType;
  char*                boundQ;
  char*                boundScopeQ;
  char*                boundGeorel;
  char*                boundGeometry;
  char*                boundCoordinates;
  char*                boundGeoproperty;

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

#endif  // CORNGSILD_LDENTITYMAP_H_
