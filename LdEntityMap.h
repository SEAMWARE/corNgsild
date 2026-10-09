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
#include <pthread.h>                                   // pthread_rwlock_t
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

  //
  // The bound q as a subscription stores its own (ldQRenderStored): attribute names expanded, the
  // values expandValues named expanded, a [..] under jsonKeys / langProperties as sent. A page that
  // omits q re-applies THIS one (ldQParseStored), not boundQ re-parsed: the raw string would be
  // expanded with that page's @context, without the expandValues, jsonKeys and langProperties of the
  // query that created the map. boundQ stays for the "same parameters" comparison.
  //
  char*                boundQStored;

  //
  // The URL parameters of the request that created the map - all but pagination (limit, offset) and
  // entityMap / entityMapLifetime - as key, value pairs (decoded, malloc'd; NULL when none). A link to
  // a page of the map repeats them (TS 104-175 § 9.6: a request referencing a map "shall use the same
  // parameters as in the original request"), so a followed link is a complete query by itself - and
  // can create a new map should this one be gone.
  //
  char**               queryParamV;    // key0, value0, key1, value1, ...
  int                  queryParamCount; // pairs

  // References (as the snapshot cache): one held by the store while the map is in it, one per
  // pin. A request that uses the map - pages it, renders it, has its query parameters pointing
  // at the map's bound strings - pins it for the request. Remove and the expiry purge unlink it
  // and drop the store's reference; the last reference frees it.
  int                  refCount;

  //
  // An AUTOMATIC map is the broker's own - created for a query paginated past its first page (roadmap
  // § 13). Its lifetime slides: every page served from it moves expiresAt to now + lifetimeNs. When the
  // maps' memory (ldEntityMapMaxBytes) is spent, the automatic map used longest ago gives way first; a
  // map a client asked for (entityMap=true, POST /entityMaps) is never evicted - it only expires.
  //
  bool                 automatic;
  uint64_t             lifetimeNs;     // the sliding lifetime of an automatic map
  uint64_t             lastUsed;       // epoch nanoseconds: created, or a page last served from it
  int64_t              bytes;          // what the map holds (an estimate, malloc overhead included)

  struct LdEntityMap*  next;           // linked list in store
} LdEntityMap;



// -----------------------------------------------------------------------------
//
// LdEntityMapStore - per-tenant store of entity maps
//
typedef struct LdEntityMapStore
{
  pthread_rwlock_t  lock;          // walks and lookups: rd - link / unlink: wr (taken inside)
  LdEntityMap*  head;
  LdEntityMap*  tail;
} LdEntityMapStore;

#endif  // CORNGSILD_LDENTITYMAP_H_
