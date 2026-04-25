#ifndef SWNGSILD_LDREGCACHE_H_
#define SWNGSILD_LDREGCACHE_H_

//
// FILE            LdRegCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Per-tenant Context Source Registration cache (NGSI-LD § 5.9 / § 5.10).
//
// Mirrors the LdSubCache shape: a linked list of LdRegCacheItem, one per
// registration, each holding the raw KjNode tree (used for rendering on
// GET) plus pre-parsed shortcuts used to match registrations against
// incoming Discovery (§ 5.10.2) and — later — distributed-dispatch
// requests (§ 4.3.6).
//
// The raw KjNode tree is in @context-expanded form (as stored in DB).
// Pre-parsed fields exist to avoid re-parsing on every request.
//
// First cut covers CRUD and Discovery. Mode-specific conflict checks
// (exclusive / redirect / auxiliary, § 5.9.2) are done by walking the
// itemList. The dispatcher itself (5.5.13, 5.7.x) is a separate later
// feature — this cache is the data substrate it will consume.
//
#include <regex.h>                                     // regex_t
#include <stdbool.h>                                   // bool
#include <stdint.h>                                    // uint64_t

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                              // KjNode

#include "swNgsild/LdOp.h"                             // LdOp



// -----------------------------------------------------------------------------
//
// LdRegMode - distributed-operation modes (§ 4.3.6 / § 5.2.9 mode field)
//
typedef enum LdRegMode
{
  LdRegModeInclusive = 0,    // default — broker holds local data + distributes
  LdRegModeExclusive,        // single external location, no overlap
  LdRegModeRedirect,         // external location, multiple overlaps allowed
  LdRegModeAuxiliary         // external supplements local data; retrieve-only ops
} LdRegMode;



// -----------------------------------------------------------------------------
//
// LdRegIdPattern - compiled regex for entity ID pattern matching
//
typedef struct LdRegIdPattern
{
  regex_t                regex;
  const char*            source;  // raw pattern string, borrowed from regTree
  struct LdRegIdPattern* next;
} LdRegIdPattern;



// -----------------------------------------------------------------------------
//
// LdRegEntityInfo - pre-parsed EntityInfo (§ 5.2.8)
//
// The entityInfoV linked list comes from one RegistrationInfo.entities[]
// (§ 5.2.10).  Each entry has a mandatory `type` (multi-type allowed via
// JSON array — flattened into multiple LdRegEntityInfo entries here),
// plus optional `id` (URI) or `idPattern` (regex).
//
typedef struct LdRegEntityInfo
{
  char*                       type;          // expanded IRI (borrowed from regTree)
  char*                       id;            // exact URI (borrowed; NULL if absent)
  LdRegIdPattern*             idPatternList; // compiled idPatterns (NULL if absent)
  struct LdRegEntityInfo*     next;
} LdRegEntityInfo;



// -----------------------------------------------------------------------------
//
// LdRegInfo - pre-parsed RegistrationInfo (§ 5.2.10)
//
// One LdRegInfo per element of the registration's information[] array.
// At least one of entityInfoV / propertyNamesV / relationshipNamesV
// is required by spec.
//
typedef struct LdRegInfo
{
  LdRegEntityInfo*       entityInfoV;        // linked list of EntityInfo entries
  char**                 propertyNamesV;     // NULL-term expanded IRIs (NULL = none)
  char**                 relationshipNamesV; // NULL-term expanded IRIs (NULL = none)
  struct LdRegInfo*      next;
} LdRegInfo;



// -----------------------------------------------------------------------------
//
// LdRegCacheItem - single cached Context Source Registration
//
typedef struct LdRegCacheItem
{
  char*                  regId;              // registration ID (malloc'd copy)
  KjNode*                regTree;            // full registration tree, expanded URIs
                                             // (kjClone'd, malloc allocator)

  // Pre-parsed matching shortcuts
  LdRegInfo*             infoV;              // linked list of RegistrationInfo entries
  LdRegMode              mode;               // inclusive / exclusive / redirect / auxiliary
  uint64_t               operationsMask;     // OR of supported LdOp bits (§ 4.20 Table 4.20-1/2).
                                             // When the registration omits operations[] the
                                             // parser pre-seeds LD_OP_GROUP_FEDERATION so match
                                             // time is a single AND with no defaulting.

  // Borrowed pointers into regTree
  char*                  endpoint;           // dereferenceable URI of the context source
  char*                  csourceAlias;       // loop-detection pseudonym (NULL if absent) —
                                             // points to regTree when client-supplied,
                                             // or to probedAlias when learned via probe.
  char*                  probedAlias;        // strdup'd alias from /info/sourceIdentity
                                             // probe; owned by the item (free on release).
                                             // NULL if not probed or probe failed.
  char*                  tenant;             // § 5.2.9 tenant rewrite — forwarded requests get
                                             // NGSILD-Tenant: <tenant> (NULL = keep sender's tenant)

  // contextSourceInfo (§ 5.2.22) — arbitrary KV pairs added to every
  // forwarded request's HTTP headers. Use cases: API keys, auth
  // tokens, Accept overrides. Interleaved key/value pairs in a
  // NULL-terminated char* array (kv[0]=key, kv[1]=val, kv[2]=key, ...).
  char**                 contextSourceInfoKV;

  // scope (§ 5.2.9) — scopes (or scope patterns, § 4.18) this Context
  // Source covers. NULL-terminated string array. NULL means wildcard
  // (CSR matches regardless of entity scope).
  char**                 scopeV;

  // Geo coverage (§ 5.2.9). KjNode pointers into regTree, borrowed. All
  // three are full GeoJSON Geometry objects with "type" and "coordinates".
  // Match-time filtering is performed in the dispatch loop via the
  // registered db.geoMatchFunc (shared plugin GEOS) — swNgsild has no
  // direct GEOS dependency, it routes through that function pointer.
  KjNode*                locationP;           // where the source has Entities
  KjNode*                observationSpaceP;   // union of observationSpaces (§ 4.7)
  KjNode*                operationSpaceP;     // union of operationSpaces (§ 4.7)

  // management.timeout (§ 5.2.34) — max ms before a forwarded request is
  // assumed failed. 0 = plugin default (no per-CSR override).
  int                    timeoutMs;

  // Expiration
  uint64_t               expiresAt;          // epoch nanoseconds (0 = never)

  // Distributed-op counters (mutable — updated when dispatch is wired up)
  int                    timesSent;
  int                    timesFailed;
  uint64_t               lastSuccess;        // epoch nanoseconds
  uint64_t               lastFailure;        // epoch nanoseconds

  struct LdRegCacheItem* next;               // linked list chain
} LdRegCacheItem;



// -----------------------------------------------------------------------------
//
// LdRegCache - per-tenant Context Source Registration cache
//
typedef struct LdRegCache
{
  LdRegCacheItem*        itemList;           // linked list head
  LdRegCacheItem*        last;               // tail (O(1) append)
  KAlloc                 alloc;              // persistent allocator for parsed shortcuts
  char                   allocBuf[1024];     // initial allocation buffer
} LdRegCache;

#endif  // SWNGSILD_LDREGCACHE_H_
