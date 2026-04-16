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
  char**                 operationsV;        // NULL-term op-names (NULL = federationOps default)

  // Borrowed pointers into regTree
  char*                  endpoint;           // dereferenceable URI of the context source
  char*                  csourceAlias;       // loop-detection pseudonym (NULL if absent)

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
