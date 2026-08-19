#ifndef CORNGSILD_LD_DISCOVERY_FORWARD_H_
#define CORNGSILD_LD_DISCOVERY_FORWARD_H_
//
// FILE            ldDiscoveryForward.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>
#include "kjson/KjNode.h"
#include "corNgsild/LdRegCache.h"

//
// ldDiscoveryShouldForward - true if the current request's hop budget
// still allows a subordinate hop.
//
extern bool ldDiscoveryShouldForward(void);


//
// Forward GET /types (resp. /attributes) to every CSR supporting the
// matching retrieve op and merge each response into the aggregation.
// Requests ?details=true from subordinates so the response carries
// full IRIs; the caller's own details / list shaping is decided later.
//
extern void ldDiscoveryForwardTypes(KjNode* agg, LdRegCache* cacheP, bool details, const char* ownAlias);
extern void ldDiscoveryForwardAttrs(KjNode* agg, LdRegCache* cacheP, bool details, const char* ownAlias);


//
// Forward GET /types/{type} (resp. /attributes/{attrId}). `typeShort`
// (resp. `attrShort`) is the short name to use on the outgoing URL;
// `typeIri` / `attrIri` is retained only for logging.
//
extern void ldDiscoveryForwardType(KjNode* agg, LdRegCache* cacheP,
                                   const char* typeIri, const char* typeShort,
                                   const char* ownAlias);
extern void ldDiscoveryForwardAttr(KjNode* agg, LdRegCache* cacheP,
                                   const char* attrIri, const char* attrShort,
                                   const char* ownAlias);

#endif
