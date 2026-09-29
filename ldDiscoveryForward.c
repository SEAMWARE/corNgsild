//
// FILE            ldDiscoveryForward.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Discovery § 5.7.11 mode 3: forward /types, /types/{type}, /attributes,
// /attributes/{attrId} to every CSR that supports the matching retrieve
// op (which is part of federationOps — the default op set). Parse each
// response and merge it into the aggregation already carrying local
// and CSR-declared data.
//
// Hop limit: propagated via ?hops=<N-1>. The federation tree traversal
// stops when the hop counter reaches 0. Absent → sensible default.
//
// Forwarding is always made with ?details=true so responses are in the
// richer form (EntityType[] / Attribute[]) carrying full IRIs that we
// can merge directly. The caller's own list vs. details shape is
// decided later in the service routine.
//

#include <stdio.h>                                    // snprintf
#include <stdlib.h>                                   // realloc
#include <string.h>                                   // strcmp, strchr

#include "corTree/CorNode.h"                          // CorNode
#include "corTree/corTreeBuilder.h"                   // corTreeArray, corTreeObject, corTreeString, corTreeInteger, corTreeChildAdd
#include "corTree/corTreeLookup.h"                    // corTreeLookup
#include "corJson/corJsonParse.h"                     // corJsonParse
#include "corRest/CorRestState.h"                       // corRest
#include "corRest/CorRestVerb.h"                        // CorVerbGet

#include "corJsonld/corLdExpand.h"                      // corLdExpand
#include "corJsonld/corLdInit.h"                        // corLdCoreContext

#include "corNgsild/LdRegCache.h"                      // LdRegCache, LdRegCacheItem
#include "corNgsild/ldRegCache.h"                      // ldRegOpSupported, ldRegCacheRdLock, ldRegCacheItemPin, ldRegCacheMatchRelease
#include "corNgsild/ldDistOp.h"                        // ldDistOpSendReceive, ldDistOpCsrWouldLoop
#include "corNgsild/CorNgsild.h"                       // corNgsild (hops)
#include "corNgsild/ldStripAtContext.h"                // ldStripAtContext
#include "corNgsild/ldDiscoveryForward.h"              // Own interface



// -----------------------------------------------------------------------------
//
// LD_DISCOVERY_HOPS_DEFAULT -
//
// Used when the inbound request has no ?hops=... param. 8 is roomy enough
// for plausible federation depths without risking a runaway traversal.
//
#define LD_DISCOVERY_HOPS_DEFAULT 8



// -----------------------------------------------------------------------------
//
// remainingHops -
//
// The hop value to put on the outgoing URL. If the inbound had hops=N,
// outgoing gets N-1. Absent → default. Floors at 0.
//
static int remainingHops(void)
{
  int n = corNgsild.hopsSet ? corNgsild.hops : LD_DISCOVERY_HOPS_DEFAULT;
  return (n > 0) ? n - 1 : 0;
}



// -----------------------------------------------------------------------------
//
// shouldForward - true if any forwarding can still happen
//
bool ldDiscoveryShouldForward(void)
{
  // Forwarding /types and /attributes to registered Context Sources is a
  // distributed operation like any other — off unless --distributed says so.
  if (ldDistributed == false)
    return false;

  int n = corNgsild.hopsSet ? corNgsild.hops : LD_DISCOVERY_HOPS_DEFAULT;
  return (n > 0);
}



// -----------------------------------------------------------------------------
//
// Small aggregation helpers — mirror the ones in ldDiscovery.c. Kept
// local to avoid exposing them; the augment path and the forward path
// share the same aggregation shape.
//
static void stringArrayAddUnique(CorNode* arr, const char* s)
{
  for (CorNode* p = arr->value.head; p != NULL; p = p->next)
    if (p->type == CorString && strcmp(p->value.s, s) == 0)
      return;
  corTreeChildAdd(arr, corTreeString(corRest.kallocP, NULL, s));
}



static CorNode* typeEntryEnsure(CorNode* agg, const char* typeIri, bool details)
{
  for (CorNode* e = agg->value.head; e != NULL; e = e->next)
  {
    CorNode* iriP = corTreeLookup(e, "typeIri");
    if (iriP != NULL && iriP->type == CorString && strcmp(iriP->value.s, typeIri) == 0)
      return e;
  }

  CorNode* e = corTreeObject(corRest.kallocP, NULL);
  corTreeChildAdd(e, corTreeString(corRest.kallocP, "typeIri", typeIri));
  corTreeChildAdd(e, corTreeArray(corRest.kallocP, "attrs"));
  if (details)
  {
    corTreeChildAdd(e, corTreeObject(corRest.kallocP, "attrTypes"));
    corTreeChildAdd(e, corTreeInteger(corRest.kallocP, "entityCount", 0));
  }
  corTreeChildAdd(agg, e);
  return e;
}



static CorNode* attrEntryEnsure(CorNode* agg, const char* attrIri, bool details)
{
  for (CorNode* e = agg->value.head; e != NULL; e = e->next)
  {
    CorNode* iriP = corTreeLookup(e, "attrIri");
    if (iriP != NULL && iriP->type == CorString && strcmp(iriP->value.s, attrIri) == 0)
      return e;
  }

  CorNode* e = corTreeObject(corRest.kallocP, NULL);
  corTreeChildAdd(e, corTreeString(corRest.kallocP, "attrIri", attrIri));
  if (details)
  {
    corTreeChildAdd(e, corTreeArray(corRest.kallocP, "typeNames"));
    corTreeChildAdd(e, corTreeArray(corRest.kallocP, "attrTypes"));
    corTreeChildAdd(e, corTreeInteger(corRest.kallocP, "attrCount", 0));
  }
  corTreeChildAdd(agg, e);
  return e;
}



static void addAttrType(CorNode* typeEntry, const char* attrName, const char* at)
{
  CorNode* attrTypesObj = corTreeLookup(typeEntry, "attrTypes");
  if (attrTypesObj == NULL) return;

  CorNode* atArr = corTreeLookup(attrTypesObj, attrName);
  if (atArr == NULL)
  {
    atArr = corTreeArray(corRest.kallocP, attrName);
    corTreeChildAdd(attrTypesObj, atArr);
  }
  stringArrayAddUnique(atArr, at);
}



// -----------------------------------------------------------------------------
//
// expandShort - short name → full IRI via our own core @context
//
// Upstream responses default to the NGSI-LD core @context, so expanding
// short names here against core gives the right IRI most of the time.
// A future refinement can parse the Link header of the response and
// use the advertised @context.
//
static const char* expandShort(const char* name)
{
  const char* iri = corLdExpand(corLdCoreContext(), name, &corRest.kalloc, NULL, NULL);
  return (iri != NULL) ? iri : name;
}



// -----------------------------------------------------------------------------
//
// forwardGet - send a GET to one CSR, parse the response JSON
//
// Returns NULL on any failure (status not 2xx, empty body, non-JSON).
//
static CorNode* forwardGet(LdRegCacheItem* csr, const char* path, bool details, int hops,
                           const char* ownAlias)
{
  char url[1024];
  const char* sep  = "?";
  int  n = snprintf(url, sizeof(url), "%s%s", csr->endpoint, path);
  if (n < 0 || n >= (int) sizeof(url)) return NULL;

  if (details)
  {
    snprintf(url + strlen(url), sizeof(url) - strlen(url), "%sdetails=true", sep);
    sep = "&";
  }
  snprintf(url + strlen(url), sizeof(url) - strlen(url), "%shops=%d", sep, hops);

  const char* upErr      = NULL;
  char*       respBody   = NULL;
  int         respBodyLen = 0;
  int         status = ldDistOpSendReceive(csr, CorVerbGet, url, NULL, 0,
                                           ownAlias, &upErr, &respBody, &respBodyLen);
  (void) upErr;
  (void) respBodyLen;

  if (status < 200 || status >= 300 || respBody == NULL)
    return NULL;

  CorNode* treeP = corJsonParse(corRest.corJsonP, respBody);
  if (treeP != NULL)
    ldStripAtContext(treeP);
  return treeP;
}



// -----------------------------------------------------------------------------
//
// mergeEntityTypeArray - merge EntityType[] response into agg
//
static void mergeEntityTypeArray(CorNode* agg, CorNode* respP, bool details)
{
  if (respP == NULL || respP->type != CorArray) return;

  for (CorNode* et = respP->value.head; et != NULL; et = et->next)
  {
    CorNode* idP = corTreeLookup(et, "id");
    if (idP == NULL || idP->type != CorString) continue;

    CorNode* te = typeEntryEnsure(agg, idP->value.s, details);
    CorNode* attrs = corTreeLookup(te, "attrs");

    CorNode* anArr = corTreeLookup(et, "attributeNames");
    if (anArr != NULL && anArr->type == CorArray)
    {
      for (CorNode* an = anArr->value.head; an != NULL; an = an->next)
      {
        if (an->type != CorString) continue;
        const char* iri = expandShort(an->value.s);
        stringArrayAddUnique(attrs, iri);
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// mergeEntityTypeInfo - merge a single EntityTypeInfo (§ 5.2.26) into agg
//
static void mergeEntityTypeInfo(CorNode* agg, CorNode* respP)
{
  if (respP == NULL || respP->type != CorObject) return;

  CorNode* idP = corTreeLookup(respP, "id");
  if (idP == NULL || idP->type != CorString) return;

  CorNode* te = typeEntryEnsure(agg, idP->value.s, true);

  CorNode* countP = corTreeLookup(respP, "entityCount");
  if (countP != NULL && countP->type == CorInt)
  {
    CorNode* teCount = corTreeLookup(te, "entityCount");
    if (teCount != NULL) teCount->value.i += countP->value.i;
  }

  CorNode* attrs = corTreeLookup(te, "attrs");

  CorNode* adArr = corTreeLookup(respP, "attributeDetails");
  if (adArr != NULL && adArr->type == CorArray)
  {
    for (CorNode* ad = adArr->value.head; ad != NULL; ad = ad->next)
    {
      if (ad->type != CorObject) continue;
      CorNode* adIdP = corTreeLookup(ad, "id");
      if (adIdP == NULL || adIdP->type != CorString) continue;

      stringArrayAddUnique(attrs, adIdP->value.s);

      CorNode* atArr = corTreeLookup(ad, "attributeTypes");
      if (atArr != NULL && atArr->type == CorArray)
      {
        for (CorNode* at = atArr->value.head; at != NULL; at = at->next)
          if (at->type == CorString)
            addAttrType(te, adIdP->value.s, at->value.s);
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// mergeAttributeArray - merge Attribute[] response into agg
//
static void mergeAttributeArray(CorNode* agg, CorNode* respP, bool details)
{
  if (respP == NULL || respP->type != CorArray) return;

  for (CorNode* at = respP->value.head; at != NULL; at = at->next)
  {
    CorNode* idP = corTreeLookup(at, "id");
    if (idP == NULL || idP->type != CorString) continue;

    CorNode* ae = attrEntryEnsure(agg, idP->value.s, details);
    if (!details) continue;

    CorNode* tnArr = corTreeLookup(at, "typeNames");
    CorNode* typeNamesAgg = corTreeLookup(ae, "typeNames");
    if (tnArr != NULL && tnArr->type == CorArray && typeNamesAgg != NULL)
    {
      for (CorNode* tn = tnArr->value.head; tn != NULL; tn = tn->next)
        if (tn->type == CorString)
          stringArrayAddUnique(typeNamesAgg, expandShort(tn->value.s));
    }
  }
}



// -----------------------------------------------------------------------------
//
// mergeAttributeInfo - merge a single Attribute (§ 5.2.28) into agg
//
static void mergeAttributeInfo(CorNode* agg, CorNode* respP)
{
  if (respP == NULL || respP->type != CorObject) return;

  CorNode* idP = corTreeLookup(respP, "id");
  if (idP == NULL || idP->type != CorString) return;

  CorNode* ae = attrEntryEnsure(agg, idP->value.s, true);

  CorNode* countP = corTreeLookup(respP, "attributeCount");
  if (countP != NULL && countP->type == CorInt)
  {
    CorNode* aeCount = corTreeLookup(ae, "attrCount");
    if (aeCount != NULL) aeCount->value.i += countP->value.i;
  }

  CorNode* atArr = corTreeLookup(respP, "attributeTypes");
  CorNode* attrTypesAgg = corTreeLookup(ae, "attrTypes");
  if (atArr != NULL && atArr->type == CorArray && attrTypesAgg != NULL)
  {
    for (CorNode* at = atArr->value.head; at != NULL; at = at->next)
      if (at->type == CorString)
        stringArrayAddUnique(attrTypesAgg, at->value.s);
  }

  CorNode* tnArr = corTreeLookup(respP, "typeNames");
  CorNode* typeNamesAgg = corTreeLookup(ae, "typeNames");
  if (tnArr != NULL && tnArr->type == CorArray && typeNamesAgg != NULL)
  {
    for (CorNode* tn = tnArr->value.head; tn != NULL; tn = tn->next)
      if (tn->type == CorString)
        stringArrayAddUnique(typeNamesAgg, expandShort(tn->value.s));
  }
}



// -----------------------------------------------------------------------------
//
// regsPinned - every registration with an endpoint, PINNED, in a malloc'd array
//
// The forwarders below send a GET to each registration's endpoint - network I/O, seconds of it
// with a slow source - and they walked the cache with no lock while doing it, so a registration
// DELETE meanwhile freed the item mid-forward. Now the walk is under the rdlock and only
// collects; the forwarding runs unlocked on pinned items, and ldRegCacheMatchRelease unpins
// them and frees the array. NULL (and *nP 0) when there is nothing to forward to.
//
static LdRegCacheItem** regsPinned(LdRegCache* cacheP, int* nP)
{
  int              n   = 0;
  int              cap = 0;
  LdRegCacheItem** v   = NULL;

  ldRegCacheRdLock(cacheP);
  for (LdRegCacheItem* it = cacheP->itemList; it != NULL; it = it->next)
  {
    if (it->endpoint == NULL)
      continue;

    if (n == cap)
    {
      int              newCap = (cap == 0) ? 8 : 2 * cap;
      LdRegCacheItem** newV   = (LdRegCacheItem**) realloc(v, newCap * sizeof(LdRegCacheItem*));

      if (newV == NULL)
        break;                  // forward to the ones collected so far

      v   = newV;
      cap = newCap;
    }

    ldRegCacheItemPin(it);
    v[n++] = it;
  }
  ldRegCacheUnlock(cacheP);

  *nP = n;
  return v;
}



// -----------------------------------------------------------------------------
//
// ldDiscoveryForwardTypes -
//
void ldDiscoveryForwardTypes(CorNode* agg, LdRegCache* cacheP, bool details, const char* ownAlias)
{
  if (!ldDiscoveryShouldForward()) return;
  if (cacheP == NULL) return;

  int hops = remainingHops();

  int              regN = 0;
  LdRegCacheItem** regV = regsPinned(cacheP, &regN);

  for (int r = 0; r < regN; r++)
  {
    LdRegCacheItem* it = regV[r];

    if (it->endpoint == NULL)                           continue;
    if (ldDistOpCsrWouldLoop(it, ownAlias))             continue;

    // Subordinate CSR must support the op — default op set is federationOps
    // which includes retrieveEntityTypes (both list and details forms fold
    // into "retrieveEntityTypes" / "retrieveEntityTypeDetails" — we go with
    // the details one since we always request ?details=true below).
    if (!ldRegOpSupported(it, LdOpRetrieveEntityTypeDetails)) continue;

    CorNode* respP = forwardGet(it, "/ngsi-ld/v1/types", true, hops, ownAlias);
    mergeEntityTypeArray(agg, respP, details);
  }

  ldRegCacheMatchRelease(regV, regN);
}



// -----------------------------------------------------------------------------
//
// ldDiscoveryForwardType -
//
void ldDiscoveryForwardType(CorNode* agg, LdRegCache* cacheP, const char* typeIri,
                            const char* typeShort, const char* ownAlias)
{
  if (!ldDiscoveryShouldForward()) return;
  if (cacheP == NULL) return;

  int hops = remainingHops();

  char path[512];
  snprintf(path, sizeof(path), "/ngsi-ld/v1/types/%s",
           (typeShort != NULL) ? typeShort : typeIri);

  int              regN = 0;
  LdRegCacheItem** regV = regsPinned(cacheP, &regN);

  for (int r = 0; r < regN; r++)
  {
    LdRegCacheItem* it = regV[r];

    if (it->endpoint == NULL)                         continue;
    if (ldDistOpCsrWouldLoop(it, ownAlias))           continue;
    if (!ldRegOpSupported(it, LdOpRetrieveEntityTypeInfo)) continue;

    CorNode* respP = forwardGet(it, path, false, hops, ownAlias);
    mergeEntityTypeInfo(agg, respP);
  }

  ldRegCacheMatchRelease(regV, regN);
}



// -----------------------------------------------------------------------------
//
// ldDiscoveryForwardAttrs -
//
void ldDiscoveryForwardAttrs(CorNode* agg, LdRegCache* cacheP, bool details, const char* ownAlias)
{
  if (!ldDiscoveryShouldForward()) return;
  if (cacheP == NULL) return;

  int hops = remainingHops();

  int              regN = 0;
  LdRegCacheItem** regV = regsPinned(cacheP, &regN);

  for (int r = 0; r < regN; r++)
  {
    LdRegCacheItem* it = regV[r];

    if (it->endpoint == NULL)                           continue;
    if (ldDistOpCsrWouldLoop(it, ownAlias))             continue;
    if (!ldRegOpSupported(it, LdOpRetrieveAttrTypeDetails)) continue;

    CorNode* respP = forwardGet(it, "/ngsi-ld/v1/attributes", true, hops, ownAlias);
    mergeAttributeArray(agg, respP, details);
  }

  ldRegCacheMatchRelease(regV, regN);
}



// -----------------------------------------------------------------------------
//
// ldDiscoveryForwardAttr -
//
void ldDiscoveryForwardAttr(CorNode* agg, LdRegCache* cacheP, const char* attrIri,
                            const char* attrShort, const char* ownAlias)
{
  if (!ldDiscoveryShouldForward()) return;
  if (cacheP == NULL) return;

  int hops = remainingHops();

  char path[512];
  snprintf(path, sizeof(path), "/ngsi-ld/v1/attributes/%s",
           (attrShort != NULL) ? attrShort : attrIri);

  int              regN = 0;
  LdRegCacheItem** regV = regsPinned(cacheP, &regN);

  for (int r = 0; r < regN; r++)
  {
    LdRegCacheItem* it = regV[r];

    if (it->endpoint == NULL)                         continue;
    if (ldDistOpCsrWouldLoop(it, ownAlias))           continue;
    if (!ldRegOpSupported(it, LdOpRetrieveAttrTypeInfo)) continue;

    CorNode* respP = forwardGet(it, path, false, hops, ownAlias);
    mergeAttributeInfo(agg, respP);
  }

  ldRegCacheMatchRelease(regV, regN);
}
