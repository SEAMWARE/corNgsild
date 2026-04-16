#ifndef SWNGSILD_LDCSOURCEALIAS_H_
#define SWNGSILD_LDCSOURCEALIAS_H_

//
// FILE            ldCsourceAlias.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Per-tenant Context Source Alias derivation + Via-header loop detection.
// NGSI-LD § 5.7.5 (ContextSourceIdentity) requires the alias to identify
// a specific Tenant within a registered Context Source — the same broker
// process serving two tenants must surface two different aliases so that
// loop detection in the multi-tenancy use case is sound.
//
#include <stdbool.h>                                 // bool

#include "kalloc/KAlloc.h"                           // KAlloc
#include "swRest/SwRestKeyValue.h"                   // SwRestKeyValue



// -----------------------------------------------------------------------------
//
// ldCsourceAliasForTenant - per-tenant alias derived from ldCsourceAliasBase
//
// Returns ldCsourceAliasBase for the default tenant (NULL or empty), or
// "<base>:<tenant>" for a named tenant. Allocates inside kaP for the
// concatenated form. Returns NULL if ldCsourceAliasBase is unset.
//
extern const char* ldCsourceAliasForTenant(const char* tenant, KAlloc* kaP);



// -----------------------------------------------------------------------------
//
// ldViaHasAlias - true if any incoming Via header contains 'alias'
//
// Walks all "Via" entries (case-insensitive header match), splits on commas,
// and matches the alias against each entry's pseudonym token. Standard form:
//   Via: 1.1 alias-or-host[:port] [(comment)]
// Token-precise: skips the protocol token, captures the received-by token
// up to the next whitespace, ',' or '(', then exact-equals against alias.
// Substring matching would false-positive across tenant suffixes
// ("cb-alias" inside "cb-alias:tA").
//
extern bool ldViaHasAlias(SwRestKeyValue* httpHeaderV, int httpHeaderCount, const char* alias);

#endif  // SWNGSILD_LDCSOURCEALIAS_H_
