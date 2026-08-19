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
#include <string.h>                                   // strcmp, strchr

#include "kjson/KjNode.h"                             // KjNode
#include "kjson/kjBuilder.h"                          // kjArray, kjObject, kjString, kjInteger, kjChildAdd
#include "kjson/kjLookup.h"                           // kjLookup
#include "kjson/kjParse.h"                            // kjParse
#include "corRest/CorRestState.h"                       // corRest
#include "corRest/CorRestVerb.h"                        // CorVerbGet

#include "corJsonld/corLdExpand.h"                      // corLdExpand
#include "corJsonld/corLdInit.h"                        // corLdCoreContext

#include "corNgsild/LdRegCache.h"                      // LdRegCache, LdRegCacheItem
#include "corNgsild/ldRegCache.h"                      // ldRegOpSupported
#include "corNgsild/ldDistOp.h"                        // ldDistOpSendReceive, ldDistOpCsrWouldLoop
#include "corNgsild/CorNgsild.h"                        // corNgsild (hops)
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
static void stringArrayAddUnique(KjNode* arr, const char* s)
{
  for (KjNode* p = arr->value.firstChildP; p != NULL; p = p->next)
    if (p->type == KjString && strcmp(p->value.s, s) == 0)
      return;
  kjChildAdd(arr, kjString(corRest.kjsonP, NULL, s));
}



static KjNode* typeEntryEnsure(KjNode* agg, const char* typeIri, bool details)
{
  for (KjNode* e = agg->value.firstChildP; e != NULL; e = e->next)
  {
    KjNode* iriP = kjLookup(e, "typeIri");
    if (iriP != NULL && iriP->type == KjString && strcmp(iriP->value.s, typeIri) == 0)
      return e;
  }

  KjNode* e = kjObject(corRest.kjsonP, NULL);
  kjChildAdd(e, kjString(corRest.kjsonP, "typeIri", typeIri));
  kjChildAdd(e, kjArray(corRest.kjsonP, "attrs"));
  if (details)
  {
    kjChildAdd(e, kjObject(corRest.kjsonP,  "attrTypes"));
    kjChildAdd(e, kjInteger(corRest.kjsonP, "entityCount", 0));
  }
  kjChildAdd(agg, e);
  return e;
}



static KjNode* attrEntryEnsure(KjNode* agg, const char* attrIri, bool details)
{
  for (KjNode* e = agg->value.firstChildP; e != NULL; e = e->next)
  {
    KjNode* iriP = kjLookup(e, "attrIri");
    if (iriP != NULL && iriP->type == KjString && strcmp(iriP->value.s, attrIri) == 0)
      return e;
  }

  KjNode* e = kjObject(corRest.kjsonP, NULL);
  kjChildAdd(e, kjString(corRest.kjsonP, "attrIri", attrIri));
  if (details)
  {
    kjChildAdd(e, kjArray(corRest.kjsonP,  "typeNames"));
    kjChildAdd(e, kjArray(corRest.kjsonP,  "attrTypes"));
    kjChildAdd(e, kjInteger(corRest.kjsonP, "attrCount", 0));
  }
  kjChildAdd(agg, e);
  return e;
}



static void addAttrType(KjNode* typeEntry, const char* attrName, const char* at)
{
  KjNode* attrTypesObj = kjLookup(typeEntry, "attrTypes");
  if (attrTypesObj == NULL) return;

  KjNode* atArr = kjLookup(attrTypesObj, attrName);
  if (atArr == NULL)
  {
    atArr = kjArray(corRest.kjsonP, attrName);
    kjChildAdd(attrTypesObj, atArr);
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
static KjNode* forwardGet(LdRegCacheItem* csr, const char* path, bool details, int hops,
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

  KjNode* treeP = kjParse(corRest.kjsonP, respBody);
  if (treeP != NULL)
    ldStripAtContext(treeP);
  return treeP;
}



// -----------------------------------------------------------------------------
//
// mergeEntityTypeArray - merge EntityType[] response into agg
//
static void mergeEntityTypeArray(KjNode* agg, KjNode* respP, bool details)
{
  if (respP == NULL || respP->type != KjArray) return;

  for (KjNode* et = respP->value.firstChildP; et != NULL; et = et->next)
  {
    KjNode* idP = kjLookup(et, "id");
    if (idP == NULL || idP->type != KjString) continue;

    KjNode* te = typeEntryEnsure(agg, idP->value.s, details);
    KjNode* attrs = kjLookup(te, "attrs");

    KjNode* anArr = kjLookup(et, "attributeNames");
    if (anArr != NULL && anArr->type == KjArray)
    {
      for (KjNode* an = anArr->value.firstChildP; an != NULL; an = an->next)
      {
        if (an->type != KjString) continue;
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
static void mergeEntityTypeInfo(KjNode* agg, KjNode* respP)
{
  if (respP == NULL || respP->type != KjObject) return;

  KjNode* idP = kjLookup(respP, "id");
  if (idP == NULL || idP->type != KjString) return;

  KjNode* te = typeEntryEnsure(agg, idP->value.s, true);

  KjNode* countP = kjLookup(respP, "entityCount");
  if (countP != NULL && countP->type == KjInt)
  {
    KjNode* teCount = kjLookup(te, "entityCount");
    if (teCount != NULL) teCount->value.i += countP->value.i;
  }

  KjNode* attrs = kjLookup(te, "attrs");

  KjNode* adArr = kjLookup(respP, "attributeDetails");
  if (adArr != NULL && adArr->type == KjArray)
  {
    for (KjNode* ad = adArr->value.firstChildP; ad != NULL; ad = ad->next)
    {
      if (ad->type != KjObject) continue;
      KjNode* adIdP = kjLookup(ad, "id");
      if (adIdP == NULL || adIdP->type != KjString) continue;

      stringArrayAddUnique(attrs, adIdP->value.s);

      KjNode* atArr = kjLookup(ad, "attributeTypes");
      if (atArr != NULL && atArr->type == KjArray)
      {
        for (KjNode* at = atArr->value.firstChildP; at != NULL; at = at->next)
          if (at->type == KjString)
            addAttrType(te, adIdP->value.s, at->value.s);
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// mergeAttributeArray - merge Attribute[] response into agg
//
static void mergeAttributeArray(KjNode* agg, KjNode* respP, bool details)
{
  if (respP == NULL || respP->type != KjArray) return;

  for (KjNode* at = respP->value.firstChildP; at != NULL; at = at->next)
  {
    KjNode* idP = kjLookup(at, "id");
    if (idP == NULL || idP->type != KjString) continue;

    KjNode* ae = attrEntryEnsure(agg, idP->value.s, details);
    if (!details) continue;

    KjNode* tnArr = kjLookup(at, "typeNames");
    KjNode* typeNamesAgg = kjLookup(ae, "typeNames");
    if (tnArr != NULL && tnArr->type == KjArray && typeNamesAgg != NULL)
    {
      for (KjNode* tn = tnArr->value.firstChildP; tn != NULL; tn = tn->next)
        if (tn->type == KjString)
          stringArrayAddUnique(typeNamesAgg, expandShort(tn->value.s));
    }
  }
}



// -----------------------------------------------------------------------------
//
// mergeAttributeInfo - merge a single Attribute (§ 5.2.28) into agg
//
static void mergeAttributeInfo(KjNode* agg, KjNode* respP)
{
  if (respP == NULL || respP->type != KjObject) return;

  KjNode* idP = kjLookup(respP, "id");
  if (idP == NULL || idP->type != KjString) return;

  KjNode* ae = attrEntryEnsure(agg, idP->value.s, true);

  KjNode* countP = kjLookup(respP, "attributeCount");
  if (countP != NULL && countP->type == KjInt)
  {
    KjNode* aeCount = kjLookup(ae, "attrCount");
    if (aeCount != NULL) aeCount->value.i += countP->value.i;
  }

  KjNode* atArr = kjLookup(respP, "attributeTypes");
  KjNode* attrTypesAgg = kjLookup(ae, "attrTypes");
  if (atArr != NULL && atArr->type == KjArray && attrTypesAgg != NULL)
  {
    for (KjNode* at = atArr->value.firstChildP; at != NULL; at = at->next)
      if (at->type == KjString)
        stringArrayAddUnique(attrTypesAgg, at->value.s);
  }

  KjNode* tnArr = kjLookup(respP, "typeNames");
  KjNode* typeNamesAgg = kjLookup(ae, "typeNames");
  if (tnArr != NULL && tnArr->type == KjArray && typeNamesAgg != NULL)
  {
    for (KjNode* tn = tnArr->value.firstChildP; tn != NULL; tn = tn->next)
      if (tn->type == KjString)
        stringArrayAddUnique(typeNamesAgg, expandShort(tn->value.s));
  }
}



// -----------------------------------------------------------------------------
//
// ldDiscoveryForwardTypes -
//
void ldDiscoveryForwardTypes(KjNode* agg, LdRegCache* cacheP, bool details, const char* ownAlias)
{
  if (!ldDiscoveryShouldForward()) return;
  if (cacheP == NULL) return;

  int hops = remainingHops();

  for (LdRegCacheItem* it = cacheP->itemList; it != NULL; it = it->next)
  {
    if (it->endpoint == NULL)                           continue;
    if (ldDistOpCsrWouldLoop(it, ownAlias))             continue;

    // Subordinate CSR must support the op — default op set is federationOps
    // which includes retrieveEntityTypes (both list and details forms fold
    // into "retrieveEntityTypes" / "retrieveEntityTypeDetails" — we go with
    // the details one since we always request ?details=true below).
    if (!ldRegOpSupported(it, LdOpRetrieveEntityTypeDetails)) continue;

    KjNode* respP = forwardGet(it, "/ngsi-ld/v1/types", true, hops, ownAlias);
    mergeEntityTypeArray(agg, respP, details);
  }
}



// -----------------------------------------------------------------------------
//
// ldDiscoveryForwardType -
//
void ldDiscoveryForwardType(KjNode* agg, LdRegCache* cacheP, const char* typeIri,
                            const char* typeShort, const char* ownAlias)
{
  if (!ldDiscoveryShouldForward()) return;
  if (cacheP == NULL) return;

  int hops = remainingHops();

  char path[512];
  snprintf(path, sizeof(path), "/ngsi-ld/v1/types/%s",
           (typeShort != NULL) ? typeShort : typeIri);

  for (LdRegCacheItem* it = cacheP->itemList; it != NULL; it = it->next)
  {
    if (it->endpoint == NULL)                         continue;
    if (ldDistOpCsrWouldLoop(it, ownAlias))           continue;
    if (!ldRegOpSupported(it, LdOpRetrieveEntityTypeInfo)) continue;

    KjNode* respP = forwardGet(it, path, false, hops, ownAlias);
    mergeEntityTypeInfo(agg, respP);
  }
}



// -----------------------------------------------------------------------------
//
// ldDiscoveryForwardAttrs -
//
void ldDiscoveryForwardAttrs(KjNode* agg, LdRegCache* cacheP, bool details, const char* ownAlias)
{
  if (!ldDiscoveryShouldForward()) return;
  if (cacheP == NULL) return;

  int hops = remainingHops();

  for (LdRegCacheItem* it = cacheP->itemList; it != NULL; it = it->next)
  {
    if (it->endpoint == NULL)                           continue;
    if (ldDistOpCsrWouldLoop(it, ownAlias))             continue;
    if (!ldRegOpSupported(it, LdOpRetrieveAttrTypeDetails)) continue;

    KjNode* respP = forwardGet(it, "/ngsi-ld/v1/attributes", true, hops, ownAlias);
    mergeAttributeArray(agg, respP, details);
  }
}



// -----------------------------------------------------------------------------
//
// ldDiscoveryForwardAttr -
//
void ldDiscoveryForwardAttr(KjNode* agg, LdRegCache* cacheP, const char* attrIri,
                            const char* attrShort, const char* ownAlias)
{
  if (!ldDiscoveryShouldForward()) return;
  if (cacheP == NULL) return;

  int hops = remainingHops();

  char path[512];
  snprintf(path, sizeof(path), "/ngsi-ld/v1/attributes/%s",
           (attrShort != NULL) ? attrShort : attrIri);

  for (LdRegCacheItem* it = cacheP->itemList; it != NULL; it = it->next)
  {
    if (it->endpoint == NULL)                         continue;
    if (ldDistOpCsrWouldLoop(it, ownAlias))           continue;
    if (!ldRegOpSupported(it, LdOpRetrieveAttrTypeInfo)) continue;

    KjNode* respP = forwardGet(it, path, false, hops, ownAlias);
    mergeAttributeInfo(agg, respP);
  }
}
