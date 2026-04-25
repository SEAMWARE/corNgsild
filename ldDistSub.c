//
// FILE            ldDistSub.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Distributed-subscription fan-out — NGSI-LD § 5.8.1.4.
//
#include <stdio.h>                                     // snprintf
#include <stdlib.h>                                    // malloc, calloc
#include <string.h>                                    // strlen, strcmp, strdup, memcpy

#include "ktrace/kTrace.h"                             // KT_W
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjson.h"                               // Kjson
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjBuilder.h"                           // kjObject, kjArray, kjString, kjInteger, kjChildAdd, kjChildRemove
#include "kjson/kjRender.h"                            // kjFastRender
#include "kjson/kjRenderSize.h"                        // kjFastRenderSize

#include "swRest/SwRestState.h"                        // swRest
#include "swRest/SwRestVerb.h"                         // SwVerbPost, SwVerbDelete, SwVerbPatch
#include "swRest/SwRestKeyValue.h"                     // SwRestKeyValue
#include "swJsonld/swldInit.h"                         // SWLD_CORE_CONTEXT_URL

#include "swNgsild/SwNgsild.h"                         // ldBrokerHttpEndpoint
#include "swNgsild/LdSubCache.h"                       // LdSubCacheItem, LdSubSubordinate
#include "swNgsild/LdRegCache.h"                       // LdRegCache, LdRegCacheItem
#include "swNgsild/ldRegCache.h"                       // ldRegOpSupported, ldRegCacheItemLookup
#include "swNgsild/LdOp.h"                             // LdOpCreateSubscription
#include "swNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "swNgsild/ldDistOp.h"                         // ldDistOpSendReceive, ldDistOpCsrWouldLoop
#include "swNgsild/ldDistSub.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// ldDistSubSubordinatesFragment - build {_subordinates, _subordinateRunNo}
//
KjNode* ldDistSubSubordinatesFragment(LdSubCacheItem* itemP, Kjson* kjsonP)
{
  if (itemP == NULL)
    return NULL;

  KjNode* fragP = kjObject(kjsonP, NULL);
  KjNode* arr   = kjArray(kjsonP, "_subordinates");

  for (LdSubSubordinate* sub = itemP->subordinateP; sub != NULL; sub = sub->next)
  {
    KjNode* entry = kjObject(kjsonP, NULL);
    if (sub->remoteSubId != NULL) kjChildAdd(entry, kjString(kjsonP, "id",    sub->remoteSubId));
    if (sub->regId       != NULL) kjChildAdd(entry, kjString(kjsonP, "regId", sub->regId));
    kjChildAdd(entry, kjInteger(kjsonP, "runNo", (long long) sub->runNo));
    kjChildAdd(arr, entry);
  }

  kjChildAdd(fragP, arr);
  kjChildAdd(fragP, kjInteger(kjsonP, "_subordinateRunNo", (long long) itemP->subordinateRunNo));

  return fragP;
}



// -----------------------------------------------------------------------------
//
// regHasMatchingType - any reg.information[].entities[].type equals one of itemP's types?
//
// Both sides are expanded IRIs; string-equality is sufficient. itemP without
// any typed entitySelector returns false (orion-ld's punt — translating a
// "match any type" sub to a remote requires per-CSR type narrowing, which is
// step 7 of the dist-sub plan).
//
static bool regHasMatchingType(LdSubCacheItem* itemP, LdRegCacheItem* regP)
{
  for (LdSubEntitySelector* esP = itemP->entitySelectors; esP != NULL; esP = esP->next)
  {
    if (esP->type == NULL)
      continue;

    for (LdRegInfo* infoP = regP->infoV; infoP != NULL; infoP = infoP->next)
    {
      for (LdRegEntityInfo* eiP = infoP->entityInfoV; eiP != NULL; eiP = eiP->next)
      {
        if (eiP->type != NULL && strcmp(eiP->type, esP->type) == 0)
          return true;
      }
    }
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// narrowEntities - intersect parent.entities with reg.information.entities
//
// The derived sub on a CSR should match only the slice the CSR actually
// claims, not the parent's full filter. Walks every (parent.selector ×
// reg.entityInfo) pair where types match and emits a narrowed entry per
// the rules below. If the resulting array is empty, returns NULL —
// caller skips the fanout.
//
// id-narrowing rules (per pair where types match):
//   * both have explicit id     → emit only if ids equal (else disjoint)
//   * parent has id, reg type   → emit parent's id (parent already narrower)
//   * reg has id, parent type   → emit reg's id   (reg is narrower)
//   * neither has id            → emit type only
//
// idPattern is treated as wildcard for now (no narrowing based on it).
//
static KjNode* narrowEntities(LdSubCacheItem* itemP, LdRegCacheItem* regP, Kjson* kjsonP)
{
  KjNode* arr = kjArray(kjsonP, "entities");

  for (LdSubEntitySelector* esP = itemP->entitySelectors; esP != NULL; esP = esP->next)
  {
    if (esP->type == NULL)
      continue;

    for (LdRegInfo* infoP = regP->infoV; infoP != NULL; infoP = infoP->next)
    {
      for (LdRegEntityInfo* eiP = infoP->entityInfoV; eiP != NULL; eiP = eiP->next)
      {
        if (eiP->type == NULL || strcmp(eiP->type, esP->type) != 0)
          continue;

        const char* derivedId = NULL;
        if (esP->id != NULL && eiP->id != NULL)
        {
          if (strcmp(esP->id, eiP->id) != 0) continue;     // disjoint slice
          derivedId = esP->id;
        }
        else if (esP->id != NULL) derivedId = esP->id;
        else if (eiP->id != NULL) derivedId = eiP->id;

        KjNode* entry = kjObject(kjsonP, NULL);
        kjChildAdd(entry, kjString(kjsonP, "type", esP->type));
        if (derivedId != NULL)
          kjChildAdd(entry, kjString(kjsonP, "id", derivedId));

        kjChildAdd(arr, entry);
      }
    }
  }

  return (arr->value.firstChildP != NULL) ? arr : NULL;
}



// -----------------------------------------------------------------------------
//
// derivedSubBody - build the JSON-LD body for the derived subscription
//
// kjClones the local sub tree, strips broker-internal fields ("status",
// "jsonldContext"), rewrites "id" and "notification.endpoint.uri",
// replaces entities[] with the precomputed narrowed array, and prepends
// "@context" so the remote re-expands cleanly.
//
static char* derivedSubBody(LdSubCacheItem* itemP,
                            KjNode*         narrowedEntities,
                            const char*     remoteSubId,
                            const char*     callbackUri,
                            int*            bodyLenP)
{
  KjNode* clone = kjClone(swRest.kjsonP, itemP->subTree);
  if (clone == NULL)
    return NULL;

  // Strip internal fields that should not flow to the remote.
  KjNode* statusP = kjLookup(clone, LD_VOCAB_STATUS);
  if (statusP != NULL)
    kjChildRemove(clone, statusP);

  KjNode* jcP = kjLookup(clone, "jsonldContext");
  if (jcP != NULL)
    kjChildRemove(clone, jcP);

  KjNode* sysIdP = kjLookup(clone, "_id");
  if (sysIdP != NULL)
    kjChildRemove(clone, sysIdP);

  // Rewrite id
  KjNode* idP = kjLookup(clone, "id");
  if (idP != NULL && idP->type == KjString)
    idP->value.s = (char*) remoteSubId;
  else
  {
    KjNode* newIdP = kjString(swRest.kjsonP, "id", remoteSubId);
    kjChildAdd(clone, newIdP);
  }

  // Rewrite notification.endpoint.uri so the remote calls back to us.
  KjNode* notifP    = kjLookup(clone, LD_VOCAB_NOTIFICATION);
  KjNode* endpointP = (notifP != NULL) ? kjLookup(notifP, LD_VOCAB_ENDPOINT) : NULL;
  KjNode* uriP      = (endpointP != NULL) ? kjLookup(endpointP, LD_VOCAB_URI) : NULL;
  if (uriP != NULL && uriP->type == KjString)
    uriP->value.s = (char*) callbackUri;

  // Filter narrowing: replace parent's entities[] with the parent×reg
  // intersection. Pre-validated non-empty by the caller.
  if (narrowedEntities != NULL)
  {
    KjNode* oldEntities = kjLookup(clone, LD_VOCAB_ENTITIES);
    if (oldEntities != NULL)
      kjChildRemove(clone, oldEntities);
    kjChildAdd(clone, narrowedEntities);
  }

  // Add @context so the receiving broker JSON-LD-parses correctly.
  if (kjLookup(clone, "@context") == NULL)
  {
    const char* ctxUrl = (itemP->contextUrl != NULL) ? itemP->contextUrl : SWLD_CORE_CONTEXT_URL;
    kjChildAdd(clone, kjString(swRest.kjsonP, "@context", ctxUrl));
  }

  int   sz  = kjFastRenderSize(clone) + 1;
  char* buf = (char*) kaAlloc(&swRest.kalloc, sz);
  kjFastRender(clone, buf);

  if (bodyLenP != NULL)
    *bodyLenP = (int) strlen(buf);

  return buf;
}



// -----------------------------------------------------------------------------
//
// subordinateAppend - append a (remoteSubId, regId, runNo) entry on itemP
//
static void subordinateAppend(LdSubCacheItem* itemP,
                              const char*     remoteSubId,
                              const char*     regId,
                              int             runNo)
{
  LdSubSubordinate* nodeP = (LdSubSubordinate*) calloc(1, sizeof(LdSubSubordinate));
  nodeP->remoteSubId = (remoteSubId != NULL) ? strdup(remoteSubId) : NULL;
  nodeP->regId       = (regId       != NULL) ? strdup(regId)       : NULL;
  nodeP->runNo       = runNo;
  nodeP->next        = NULL;

  if (itemP->subordinateP == NULL)
  {
    itemP->subordinateP = nodeP;
    return;
  }
  LdSubSubordinate* tail = itemP->subordinateP;
  while (tail->next != NULL) tail = tail->next;
  tail->next = nodeP;
}



// -----------------------------------------------------------------------------
//
// fanoutToReg - POST a derived sub for itemP to a single CSR
//
// Caller is expected to have validated (regP supports CreateSubscription,
// no Via loop, no pre-existing subordinate for this regId). Returns true
// on 201; false otherwise — including the case where parent×reg entity
// intersection is empty (no overlap to forward).
//
static bool fanoutToReg(LdSubCacheItem* itemP, LdRegCacheItem* regP, const char* ownAlias)
{
  if (itemP == NULL || regP == NULL || regP->endpoint == NULL || ldBrokerHttpEndpoint == NULL)
    return false;
  if (itemP->subId == NULL)
    return false;

  // Compute the narrowed entity filter up-front. Empty intersection =
  // no slice to subscribe to on this CSR — bail before bumping runNo.
  KjNode* narrowed = narrowEntities(itemP, regP, swRest.kjsonP);
  if (narrowed == NULL)
    return false;

  size_t parentLen = strlen(itemP->subId);
  size_t epLen     = strlen(ldBrokerHttpEndpoint);

  int runNo = ++itemP->subordinateRunNo;

  // Derived sub id: "<parent>:<runNo>"
  char* derivedId = (char*) kaAlloc(&swRest.kalloc, parentLen + 16);
  snprintf(derivedId, parentLen + 16, "%s:%d", itemP->subId, runNo);

  // Callback URI: "<our-endpoint>/ngsi-ld/ex/v1/notifications/<parent-sub-id>"
  static const char* notifPath = "/ngsi-ld/ex/v1/notifications/";
  int notifPathLen = (int) strlen(notifPath);
  char* callback = (char*) kaAlloc(&swRest.kalloc, epLen + notifPathLen + parentLen + 1);
  char* cp = callback;
  memcpy(cp, ldBrokerHttpEndpoint, epLen); cp += epLen;
  memcpy(cp, notifPath, notifPathLen);     cp += notifPathLen;
  memcpy(cp, itemP->subId, parentLen + 1);

  int   bodyLen = 0;
  char* body    = derivedSubBody(itemP, narrowed, derivedId, callback, &bodyLen);
  if (body == NULL)
    return false;

  // Remote URL: "<csr-endpoint>/ngsi-ld/v1/subscriptions"
  static const char* subPath = "/ngsi-ld/v1/subscriptions";
  int subPathLen = (int) strlen(subPath);
  int regEpLen   = (int) strlen(regP->endpoint);
  char* url = (char*) kaAlloc(&swRest.kalloc, regEpLen + subPathLen + 1);
  memcpy(url, regP->endpoint, regEpLen);
  memcpy(url + regEpLen, subPath, subPathLen + 1);

  const char* errorDetail = NULL;

  int status = ldDistOpSendReceive(regP, SwVerbPost, url, body, bodyLen, ownAlias,
                                   &errorDetail, NULL, NULL);

  if (status != 201)
  {
    KT_W("dist-sub fanout: CSR %s rejected derived sub for parent %s (status %d)",
         (regP->regId != NULL) ? regP->regId : "?", itemP->subId, status);
    return false;
  }

  subordinateAppend(itemP, derivedId, regP->regId, runNo);
  return true;
}



// -----------------------------------------------------------------------------
//
// ldDistSubFanout -
//
int ldDistSubFanout(LdSubCacheItem*      itemP,
                    LdRegCache*          regCacheP,
                    const char*          ownAlias,
                    LdDistSubPersistFunc persistFunc,
                    void*                persistUserData)
{
  if (itemP == NULL || regCacheP == NULL || ldBrokerHttpEndpoint == NULL)
    return 0;
  if (itemP->subId == NULL || itemP->entitySelectors == NULL)
    return 0;

  int forwarded = 0;

  for (LdRegCacheItem* regP = regCacheP->itemList; regP != NULL; regP = regP->next)
  {
    if (regP->endpoint == NULL)                                continue;
    if (!ldRegOpSupported(regP, LdOpCreateSubscription))       continue;
    if (ldDistOpCsrWouldLoop(regP, ownAlias))                  continue;
    if (!regHasMatchingType(itemP, regP))                      continue;

    if (fanoutToReg(itemP, regP, ownAlias))
      forwarded++;
  }

  if (forwarded > 0 && persistFunc != NULL)
    persistFunc(itemP, persistUserData);

  return forwarded;
}



// -----------------------------------------------------------------------------
//
// ldDistSubCascadeDelete -
//
int ldDistSubCascadeDelete(LdSubCacheItem* itemP, LdRegCache* regCacheP, const char* ownAlias)
{
  if (itemP == NULL || regCacheP == NULL)
    return 0;
  if (itemP->subordinateP == NULL)
    return 0;

  static const char* subPath = "/ngsi-ld/v1/subscriptions/";
  int                subPathLen = (int) strlen(subPath);
  int                deleted    = 0;

  for (LdSubSubordinate* sub = itemP->subordinateP; sub != NULL; sub = sub->next)
  {
    if (sub->regId == NULL || sub->remoteSubId == NULL)
      continue;

    LdRegCacheItem* regP = ldRegCacheItemLookup(regCacheP, sub->regId);
    if (regP == NULL || regP->endpoint == NULL)
    {
      KT_W("dist-sub cascade DELETE: CSR %s for parent %s no longer in cache — skip",
           sub->regId, (itemP->subId != NULL) ? itemP->subId : "?");
      continue;
    }

    int   regEpLen = (int) strlen(regP->endpoint);
    int   idLen    = (int) strlen(sub->remoteSubId);
    char* url      = (char*) kaAlloc(&swRest.kalloc, regEpLen + subPathLen + idLen + 1);
    char* cp       = url;
    memcpy(cp, regP->endpoint, regEpLen); cp += regEpLen;
    memcpy(cp, subPath,        subPathLen); cp += subPathLen;
    memcpy(cp, sub->remoteSubId, idLen + 1);

    const char* errorDetail = NULL;
    int status = ldDistOpSendReceive(regP, SwVerbDelete, url, NULL, 0, ownAlias,
                                     &errorDetail, NULL, NULL);

    if (status >= 200 && status < 300)
      deleted++;
    else
      KT_W("dist-sub cascade DELETE: CSR %s remote sub %s for parent %s status=%d (%s)",
           sub->regId, sub->remoteSubId,
           (itemP->subId != NULL) ? itemP->subId : "?",
           status, (errorDetail != NULL) ? errorDetail : "");
  }

  return deleted;
}



// -----------------------------------------------------------------------------
//
// patchBody - serialize a PATCH fragment with @context attached
//
// The cached/expanded fragment may carry broker-internal additions
// (status, jsonldContext) that don't belong on the wire — strip them
// before render, then prepend @context for the receiver.
//
static char* patchBody(LdSubCacheItem* itemP, KjNode* fragmentP, int* bodyLenP)
{
  KjNode* clone = kjClone(swRest.kjsonP, fragmentP);
  if (clone == NULL)
    return NULL;

  KjNode* statusP = kjLookup(clone, LD_VOCAB_STATUS);
  if (statusP != NULL) kjChildRemove(clone, statusP);

  KjNode* jcP = kjLookup(clone, "jsonldContext");
  if (jcP != NULL) kjChildRemove(clone, jcP);

  if (kjLookup(clone, "@context") == NULL)
  {
    const char* ctxUrl = (itemP->contextUrl != NULL) ? itemP->contextUrl : SWLD_CORE_CONTEXT_URL;
    kjChildAdd(clone, kjString(swRest.kjsonP, "@context", ctxUrl));
  }

  int   sz  = kjFastRenderSize(clone) + 1;
  char* buf = (char*) kaAlloc(&swRest.kalloc, sz);
  kjFastRender(clone, buf);

  if (bodyLenP != NULL)
    *bodyLenP = (int) strlen(buf);

  return buf;
}



// -----------------------------------------------------------------------------
//
// ldDistSubCascadePatch -
//
int ldDistSubCascadePatch(LdSubCacheItem* itemP,
                          KjNode*         fragmentP,
                          LdRegCache*     regCacheP,
                          const char*     ownAlias)
{
  if (itemP == NULL || regCacheP == NULL || fragmentP == NULL)
    return 0;
  if (itemP->subordinateP == NULL)
    return 0;

  int   bodyLen = 0;
  char* body    = patchBody(itemP, fragmentP, &bodyLen);
  if (body == NULL)
    return 0;

  static const char* subPath = "/ngsi-ld/v1/subscriptions/";
  int                subPathLen = (int) strlen(subPath);
  int                patched    = 0;

  for (LdSubSubordinate* sub = itemP->subordinateP; sub != NULL; sub = sub->next)
  {
    if (sub->regId == NULL || sub->remoteSubId == NULL)
      continue;

    LdRegCacheItem* regP = ldRegCacheItemLookup(regCacheP, sub->regId);
    if (regP == NULL || regP->endpoint == NULL)
    {
      KT_W("dist-sub cascade PATCH: CSR %s for parent %s no longer in cache — skip",
           sub->regId, (itemP->subId != NULL) ? itemP->subId : "?");
      continue;
    }

    int   regEpLen = (int) strlen(regP->endpoint);
    int   idLen    = (int) strlen(sub->remoteSubId);
    char* url      = (char*) kaAlloc(&swRest.kalloc, regEpLen + subPathLen + idLen + 1);
    char* cp       = url;
    memcpy(cp, regP->endpoint, regEpLen); cp += regEpLen;
    memcpy(cp, subPath,        subPathLen); cp += subPathLen;
    memcpy(cp, sub->remoteSubId, idLen + 1);

    const char* errorDetail = NULL;
    int status = ldDistOpSendReceive(regP, SwVerbPatch, url, body, bodyLen, ownAlias,
                                     &errorDetail, NULL, NULL);

    if (status >= 200 && status < 300)
      patched++;
    else
      KT_W("dist-sub cascade PATCH: CSR %s remote sub %s for parent %s status=%d (%s)",
           sub->regId, sub->remoteSubId,
           (itemP->subId != NULL) ? itemP->subId : "?",
           status, (errorDetail != NULL) ? errorDetail : "");
  }

  return patched;
}



// -----------------------------------------------------------------------------
//
// hasSubordinateForReg - true iff itemP already has a subordinate at regId
//
static bool hasSubordinateForReg(LdSubCacheItem* itemP, const char* regId)
{
  if (regId == NULL)
    return false;

  for (LdSubSubordinate* sub = itemP->subordinateP; sub != NULL; sub = sub->next)
  {
    if (sub->regId != NULL && strcmp(sub->regId, regId) == 0)
      return true;
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// ldDistSubOnRegCreate -
//
int ldDistSubOnRegCreate(LdSubCache*          subCacheP,
                         LdRegCacheItem*      regItemP,
                         const char*          ownAlias,
                         LdDistSubPersistFunc persistFunc,
                         void*                persistUserData)
{
  if (subCacheP == NULL || regItemP == NULL || regItemP->endpoint == NULL)
    return 0;
  if (!ldRegOpSupported(regItemP, LdOpCreateSubscription))
    return 0;
  if (ldDistOpCsrWouldLoop(regItemP, ownAlias))
    return 0;

  int forwarded = 0;

  for (LdSubCacheItem* itemP = subCacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->subId == NULL || itemP->entitySelectors == NULL)
      continue;
    if (!regHasMatchingType(itemP, regItemP))
      continue;
    if (hasSubordinateForReg(itemP, regItemP->regId))
      continue;

    if (fanoutToReg(itemP, regItemP, ownAlias))
    {
      forwarded++;
      if (persistFunc != NULL)
        persistFunc(itemP, persistUserData);
    }
  }

  return forwarded;
}



// -----------------------------------------------------------------------------
//
// ldDistSubOnRegDelete -
//
// Walk subCacheP; for each entity-sub, unlink every subordinate whose
// regId matches the CSR being deleted, sending DELETE on the remote
// best-effort. Caller invokes BEFORE removing the reg from regCacheP
// so endpoint / Via fields are still live.
//
int ldDistSubOnRegDelete(LdSubCache*          subCacheP,
                         LdRegCacheItem*      regItemP,
                         const char*          ownAlias,
                         LdDistSubPersistFunc persistFunc,
                         void*                persistUserData)
{
  if (subCacheP == NULL || regItemP == NULL || regItemP->regId == NULL)
    return 0;

  static const char* subPath    = "/ngsi-ld/v1/subscriptions/";
  int                subPathLen = (int) strlen(subPath);
  const char*        regId      = regItemP->regId;
  int                cleaned    = 0;

  for (LdSubCacheItem* itemP = subCacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    LdSubSubordinate** prevP   = &itemP->subordinateP;
    LdSubSubordinate*  sub     = itemP->subordinateP;
    bool               touched = false;

    while (sub != NULL)
    {
      if (sub->regId == NULL || strcmp(sub->regId, regId) != 0)
      {
        prevP = &sub->next;
        sub   = sub->next;
        continue;
      }

      // Best-effort remote DELETE while we still have the reg item.
      if (regItemP->endpoint != NULL && sub->remoteSubId != NULL)
      {
        int   regEpLen = (int) strlen(regItemP->endpoint);
        int   idLen    = (int) strlen(sub->remoteSubId);
        char* url      = (char*) kaAlloc(&swRest.kalloc, regEpLen + subPathLen + idLen + 1);
        char* cp       = url;
        memcpy(cp, regItemP->endpoint, regEpLen); cp += regEpLen;
        memcpy(cp, subPath,            subPathLen); cp += subPathLen;
        memcpy(cp, sub->remoteSubId, idLen + 1);

        const char* errorDetail = NULL;
        int status = ldDistOpSendReceive(regItemP, SwVerbDelete, url, NULL, 0, ownAlias,
                                         &errorDetail, NULL, NULL);
        if (status < 200 || status >= 300)
          KT_W("dist-sub on-reg-delete: CSR %s remote sub %s for parent %s status=%d (%s)",
               regId, sub->remoteSubId,
               (itemP->subId != NULL) ? itemP->subId : "?",
               status, (errorDetail != NULL) ? errorDetail : "");
      }

      // Unlink + free
      LdSubSubordinate* gone = sub;
      *prevP = sub->next;
      sub    = sub->next;
      free(gone->remoteSubId);
      free(gone->regId);
      free(gone);
      cleaned++;
      touched = true;
    }

    if (touched && persistFunc != NULL)
      persistFunc(itemP, persistUserData);
  }

  return cleaned;
}
