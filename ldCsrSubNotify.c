//
// FILE            ldCsrSubNotify.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// CSR-subscription matcher (§ 5.12) + CsourceNotification (§ 5.3.2)
// sender. First cut covers the initial-on-subscribe notification
// required by § 5.11.2.4 / § 5.11.7. Event-driven notifications on
// CSR create/update/delete are a follow-up.
//
// Matching scope (v1):
//   - entities[] selectors:  type, id, idPattern
//   - attr list:             watchedAttributes ∪ notification.attributes
//                            matched against propertyNames / relationshipNames
//   - per § 5.12: at least one LdRegInfo must satisfy BOTH entity-side
//     AND attr-side within that same RegistrationInfo.
//
// Deferred (documented as 501/no-op):
//   - q (over CSR user-Properties per § 4.9)
//   - geoQ, scopeQ, temporalQ, csf, lang
//
#include <stdlib.h>                                    // realloc
#include <regex.h>                                     // regexec
#include <stdio.h>                                     // snprintf
#include <string.h>                                    // strcmp, strlen
#include <time.h>                                      // clock_gettime

#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjBuilder.h"                           // kjObject, kjString, kjArray, kjChildAdd
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjRenderSize.h"                        // kjFastRenderSize
#include "kjson/kjRender.h"                            // kjFastRender

#include "swRest/SwRestState.h"                        // swRest
#include "swRest/swRestClient.h"                       // SwRestClientRequest, etc.
#include "swJsonld/swldCompactTree.h"                  // swldCompactTree, swldCompactTreeWith
#include "swJsonld/swldDownload.h"                     // swldContextFromUrl
#include "swJsonld/swldInit.h"                         // swldCoreContext
#include "swJsonld/SwldContext.h"                      // SwldContext

#include "swNgsild/LdSubCache.h"                       // LdSubCacheItem, LdSubEntitySelector
#include "swNgsild/LdRegCache.h"                       // LdRegCache, LdRegCacheItem, LdRegInfo, LdRegEntityInfo
#include "swNgsild/ldPeriodicLoop.h"                   // ldPeriodicLoopRegister
#include "swNgsild/swNgsild.h"                         // swNgsild (for tenant access via opaque)
#include "swNgsild/ldNotifyStatsHook.h"                // ldNotifyStatsHookInvoke
#include "swNgsild/ldRequestSubstitute.h"              // ldRequestSubstitute
#include "swNgsild/ldCsrSubNotify.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// strvContains - NULL-terminated string array contains needle?
//
static bool strvContains(char** v, const char* needle)
{
  if (v == NULL || needle == NULL) return false;
  for (int i = 0; v[i] != NULL; i++)
    if (strcmp(v[i], needle) == 0) return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// entitySelectorMatchesEntityInfo - one sub-side selector vs one reg-side entity-info
//
// Per § 5.12:
//   - type: if selector has type, EntityInfo type must equal it.
//   - id:   if selector has id, EntityInfo id must be absent (reg covers all) OR equal.
//   - idPattern: if selector has idPattern, EntityInfo id must match the regex
//                OR the EntityInfo carries no id (reg covers all).
//
static bool entitySelectorMatchesEntityInfo(LdSubEntitySelector* sel, LdRegEntityInfo* ei)
{
  if (sel->type != NULL)
  {
    if (ei->type == NULL || strcmp(sel->type, ei->type) != 0)
      return false;
  }

  if (sel->id != NULL)
  {
    if (ei->id != NULL && strcmp(sel->id, ei->id) != 0)
      return false;
  }

  if (sel->idPatternList != NULL && ei->id != NULL)
  {
    LdSubIdPattern* pat = sel->idPatternList;
    bool any = false;
    for (; pat != NULL; pat = pat->next)
    {
      if (regexec(&pat->regex, ei->id, 0, NULL, 0) == 0)
      {
        any = true;
        break;
      }
    }
    if (!any)
      return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// subEntitySideMatchesInfo - sub's entitySelectors vs reg's EntityInfo list
//
// Sub has no selectors       → entity-side trivially matches (any entity).
// Reg has no entityInfoV     → reg covers all entity types; entity-side matches.
// Otherwise                  → ANY (sub selector × reg entityInfo) pair must match.
//
static bool subEntitySideMatchesInfo(LdSubCacheItem* subItemP, LdRegInfo* info)
{
  if (subItemP->entitySelectors == NULL)
    return true;
  if (info->entityInfoV == NULL)
    return true;

  for (LdSubEntitySelector* sel = subItemP->entitySelectors; sel != NULL; sel = sel->next)
  {
    for (LdRegEntityInfo* ei = info->entityInfoV; ei != NULL; ei = ei->next)
    {
      if (entitySelectorMatchesEntityInfo(sel, ei))
        return true;
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// subAttrSideMatchesInfo - sub's attr list vs reg's property/relationship names
//
// Sub's attr list = watchedAttributes ∪ notification.attributes.
// Empty sub attr list   → all attrs match (per § 5.12).
// Reg has no attr names → reg covers all attrs (within its entity scope).
// Otherwise             → ANY name in sub's list must appear in reg's lists.
//
static bool subAttrSideMatchesInfo(LdSubCacheItem* subItemP, LdRegInfo* info)
{
  bool subHasWatched = (subItemP->watchedAttrsV != NULL && subItemP->watchedAttrsV[0] != NULL);
  bool subHasNotif   = (subItemP->notifAttrsV   != NULL && subItemP->notifAttrsV[0]   != NULL);

  if (!subHasWatched && !subHasNotif)
    return true;

  bool regHasAttrs = (info->propertyNamesV != NULL || info->relationshipNamesV != NULL);
  if (!regHasAttrs)
    return true;

  char** lists[2] = { subItemP->watchedAttrsV, subItemP->notifAttrsV };
  for (int li = 0; li < 2; li++)
  {
    char** v = lists[li];
    if (v == NULL) continue;
    for (int i = 0; v[i] != NULL; i++)
    {
      if (strvContains(info->propertyNamesV,     v[i])) return true;
      if (strvContains(info->relationshipNamesV, v[i])) return true;
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// subMatchesReg - § 5.12: at least one LdRegInfo must satisfy both sides
//
static bool subMatchesReg(LdSubCacheItem* subItemP, LdRegCacheItem* regItemP)
{
  if (regItemP->infoV == NULL)
    return false;

  for (LdRegInfo* info = regItemP->infoV; info != NULL; info = info->next)
  {
    if (subEntitySideMatchesInfo(subItemP, info) &&
        subAttrSideMatchesInfo(subItemP, info))
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// isoNow - ISO8601 timestamp of the current moment (UTC, ms precision)
//
static void isoNow(char* buf, int bufSize)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  struct tm tmBuf;
  gmtime_r(&ts.tv_sec, &tmBuf);

  snprintf(buf, bufSize, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
           tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
           tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, ts.tv_nsec / 1000000);
}



// -----------------------------------------------------------------------------
//
// notifIdGenerate - one-shot URN for the notification identifier
//
static const char* notifIdGenerate(void)
{
  static int counter = 0;
  static char buf[128];
  snprintf(buf, sizeof(buf), "urn:ngsi-ld:Notification:%lx:%04x",
           (long) time(NULL), ++counter & 0xFFFF);
  return buf;
}



// -----------------------------------------------------------------------------
//
// csourceNotificationBuild - build the notification tree
//
// Runs while the matched LdRegCacheItems are guaranteed alive (the same
// request may delete them before a deferred send fires, so the data
// array is cloned here, not at send time).
//
static KjNode* csourceNotificationBuild(LdSubCacheItem* subItemP,
                                        LdRegCacheItem** matchV, int matchN,
                                        const char* triggerReason)
{
  char isoTimeBuf[64];
  isoNow(isoTimeBuf, sizeof(isoTimeBuf));

  KjNode* notification = kjObject(NULL, NULL);
  kjChildAdd(notification, kjString(NULL, "id",             (char*) notifIdGenerate()));
  kjChildAdd(notification, kjString(NULL, "type",           "ContextSourceNotification"));
  kjChildAdd(notification, kjString(NULL, "subscriptionId", subItemP->subId));
  kjChildAdd(notification, kjString(NULL, "notifiedAt",     isoTimeBuf));
  kjChildAdd(notification, kjString(NULL, "triggerReason",  (char*) triggerReason));

  KjNode* dataArray = kjArray(NULL, "data");
  for (int i = 0; i < matchN; i++)
  {
    if (matchV[i]->regTree == NULL) continue;

    KjNode* regClone = kjClone(swRest.kjsonP, matchV[i]->regTree);

    // § 5.11.7 — "implementations should return context source
    // registration information relevant for the subscription, in
    // particular only matching RegistrationInfo elements." Walk the
    // cloned tree's "information" array in lockstep with the pre-
    // parsed LdRegInfo list and drop the entries whose entityInfo
    // doesn't match the subscription's entity selectors. The
    // surviving array is what the subscriber actually cares about.
    if (subItemP->entitySelectors != NULL)
    {
      KjNode* infoArrP = kjLookup(regClone, "information");
      if (infoArrP != NULL && infoArrP->type == KjArray)
      {
        KjNode*    jsonInfo = infoArrP->value.firstChildP;
        LdRegInfo* regInfo  = matchV[i]->infoV;
        while (jsonInfo != NULL && regInfo != NULL)
        {
          KjNode*    jsonNext = jsonInfo->next;
          LdRegInfo* regNext  = regInfo->next;
          if (!subEntitySideMatchesInfo(subItemP, regInfo))
            kjChildRemove(infoArrP, jsonInfo);
          jsonInfo = jsonNext;
          regInfo  = regNext;
        }
      }
    }

    kjChildAdd(dataArray, regClone);
  }
  kjChildAdd(notification, dataArray);

  return notification;
}



// -----------------------------------------------------------------------------
//
// csourceNotificationPost - compact and POST a built notification
//
static void csourceNotificationPost(LdSubCacheItem* subItemP, KjNode* notification)
{
  if (subItemP->endpointUri == NULL || notification == NULL)
    return;

  // Compact with the subscription's @context (parallel to the entity-
  // sub path in ldSubscriptionNotify / ldPernotLoop): if the sub
  // declares a context URL the broker resolves it and uses its term
  // mappings — otherwise notification attribute keys ship expanded
  // IRIs that subscribers can't index by short name (047_03_01).
  {
    SwldContext* notifCtx = NULL;
    if (subItemP->contextUrl != NULL)
      notifCtx = swldContextFromUrl(subItemP->contextUrl, &swRest.kalloc);
    if (notifCtx != NULL)
      swldCompactTreeWith(notification, notifCtx);
    else
      swldCompactTree(notification);
  }

  int   bodySize = kjFastRenderSize(notification) + 1;
  char* body     = (char*) kaAlloc(&swRest.kalloc, bodySize);
  kjFastRender(notification, body);

  SwRestClientRequest  req;
  SwRestClientResponse resp;

  swRestClientRequestInit(&req, SwVerbPost, subItemP->endpointUri, NULL);
  swRestClientRequestHeader(&req, "Content-Type", "application/json");

  const char* ctxUrl = subItemP->contextUrl;
  if (ctxUrl == NULL)
  {
    SwldContext* coreP = swldCoreContext();
    if (coreP != NULL) ctxUrl = coreP->url;
  }
  if (ctxUrl != NULL)
  {
    char linkBuf[512];
    snprintf(linkBuf, sizeof(linkBuf),
             "<%s>; rel=\"http://www.w3.org/ns/json-ld#context\"; type=\"application/ld+json\"",
             ctxUrl);
    swRestClientRequestHeader(&req, "Link", linkBuf);
  }

  // § 5.2.15 endpoint.receiverInfo — emit each {key,value} as a request header
  if (subItemP->receiverInfo != NULL && subItemP->receiverInfo->type == KjArray)
  {
    for (KjNode* kvP = subItemP->receiverInfo->value.firstChildP; kvP != NULL; kvP = kvP->next)
    {
      if (kvP->type != KjObject) continue;
      KjNode* kP = kjLookup(kvP, "key");
      KjNode* vP = kjLookup(kvP, "value");
      if (kP != NULL && kP->type == KjString && vP != NULL && vP->type == KjString)
      {
        const char* hv = ldRequestSubstitute(kP->value.s, vP->value.s);
        if (hv != NULL)
          swRestClientRequestHeader(&req, kP->value.s, hv);
      }
    }
  }

  swRestClientRequestBody(&req, body, strlen(body));
  // § 5.2.15 endpoint.timeout — per-sub override; default 10s
  int reqTmoMs = (subItemP->timeoutMs > 0) ? subItemP->timeoutMs : 10000;
  swRestClientRequestTimeout(&req, 5000, reqTmoMs);
  swRestClientSend(&req, &resp);

  subItemP->timesSent++;
  subItemP->lastNotification = swRest.requestStartTime;

  bool ok = (resp.statusCode >= 200 && resp.statusCode < 300);
  if (ok)
  {
    subItemP->lastSuccess = swRest.requestStartTime;
    // A later successful delivery clears a previous failure state
    if (subItemP->status != NULL && strcmp(subItemP->status, "failed") == 0)
      subItemP->status = (char*) "active";
  }
  else
  {
    subItemP->timesFailed++;
    subItemP->lastFailure = swRest.requestStartTime;
    // § 12.4.7: "If the notification is not sent successfully ...
    // Update the subscription status to 'failed'"
    subItemP->status = (char*) "failed";
  }

  ldNotifyStatsHookInvoke(true /*csrSub*/, ok);
}



// -----------------------------------------------------------------------------
//
// Deferred csource-notification queue (per thread)
//
// A csource notification fired from inside a request handler would go on
// the wire BEFORE the request's own response — a subscriber implemented as
// "send request, then service the notification" (the ETSI suite's robot
// client, for one) can then never answer in time and every delivery times
// out. Queue the sends here and let the post-response hook flush them —
// same MHD thread-per-connection assumption (and request-arena lifetime)
// as ldNotifyDefer.c. The periodic tick runs on its own thread with no
// post-response hook and builds + posts directly.
//
typedef struct CsrSubPending
{
  LdSubCacheItem*  subItemP;
  KjNode*          notification;   // built at enqueue time — see csourceNotificationBuild
} CsrSubPending;

static __thread CsrSubPending* csrPendingV   = NULL;
static __thread int            csrPendingN   = 0;
static __thread int            csrPendingCap = 0;

static void sendCsourceNotification(LdSubCacheItem* subItemP,
                                    LdRegCacheItem** matchV, int matchN,
                                    const char* triggerReason)
{
  if (subItemP->endpointUri == NULL)
    return;

  if (csrPendingN >= csrPendingCap)
  {
    int newCap = (csrPendingCap == 0) ? 8 : csrPendingCap * 2;
    CsrSubPending* newV = (CsrSubPending*) realloc(csrPendingV, newCap * sizeof(CsrSubPending));
    if (newV == NULL)
      return;
    csrPendingV   = newV;
    csrPendingCap = newCap;
  }

  // The matched cache items may be deleted later in this same request
  // (a CSR delete fans out, THEN removes the item) — build the
  // notification tree NOW, defer only the POST.
  csrPendingV[csrPendingN].subItemP     = subItemP;
  csrPendingV[csrPendingN].notification = csourceNotificationBuild(subItemP, matchV, matchN, triggerReason);
  csrPendingN++;
}



// -----------------------------------------------------------------------------
//
// ldCsrSubDispatchPending - flush the deferred csource notifications
//
// Called from the broker's post-response hook, after the response has
// been handed to the HTTP layer. The request arena is still alive.
//
void ldCsrSubDispatchPending(void)
{
  for (int i = 0; i < csrPendingN; i++)
    csourceNotificationPost(csrPendingV[i].subItemP, csrPendingV[i].notification);
  csrPendingN = 0;
}



// -----------------------------------------------------------------------------
//
// ldCsrSubInitialNotify -
//
void ldCsrSubInitialNotify(LdRegCache* regCacheP, LdSubCacheItem* subItemP)
{
  if (regCacheP == NULL || subItemP == NULL || subItemP->endpointUri == NULL)
    return;

  // § 12.4.7 sits on top of the § 10.5.7 lifecycle rules: a subscription
  // created paused (isActive=false) or already expired gets NO initial
  // notification.
  if (subItemP->status != NULL &&
      (strcmp(subItemP->status, "paused")  == 0 ||
       strcmp(subItemP->status, "expired") == 0))
    return;

  if (subItemP->expiresAt > 0 && swRest.requestStartTime > subItemP->expiresAt)
    return;

  //
  // Collect matching CSRs. Use the request allocator — the buffer is
  // only needed for the duration of this call.
  //
  int cap = 16;
  LdRegCacheItem** matchV = (LdRegCacheItem**) kaAlloc(&swRest.kalloc, cap * sizeof(LdRegCacheItem*));
  int n = 0;

  for (LdRegCacheItem* regItemP = regCacheP->itemList; regItemP != NULL; regItemP = regItemP->next)
  {
    if (!subMatchesReg(subItemP, regItemP))
      continue;

    if (n == cap)
    {
      int newCap = cap * 2;
      LdRegCacheItem** nv = (LdRegCacheItem**) kaAlloc(&swRest.kalloc, newCap * sizeof(LdRegCacheItem*));
      for (int i = 0; i < n; i++) nv[i] = matchV[i];
      matchV = nv;
      cap    = newCap;
    }
    matchV[n++] = regItemP;
  }

  // § 12.4.7: the csource notification "shall be sent on initial
  // subscription" — unconditionally; with zero matching CSRs the
  // notification simply carries an empty data array.
  sendCsourceNotification(subItemP, matchV, n, "newlyMatching");
}



// -----------------------------------------------------------------------------
//
// fanOutForOneReg - shared helper for on-create / on-delete
//
static void fanOutForOneReg(LdSubCache* regSubCacheP, LdRegCacheItem* regItemP, const char* triggerReason)
{
  if (regSubCacheP == NULL || regItemP == NULL)
    return;

  for (LdSubCacheItem* subItemP = regSubCacheP->itemList; subItemP != NULL; subItemP = subItemP->next)
  {
    // Skip inactive / expired subs
    if (subItemP->status != NULL &&
        (strcmp(subItemP->status, "paused")  == 0 ||
         strcmp(subItemP->status, "expired") == 0))
      continue;

    if (subItemP->expiresAt > 0 && swRest.requestStartTime > subItemP->expiresAt)
      continue;

    if (!subMatchesReg(subItemP, regItemP))
      continue;

    LdRegCacheItem* oneItem[1] = { regItemP };
    sendCsourceNotification(subItemP, oneItem, 1, triggerReason);
  }
}



// -----------------------------------------------------------------------------
//
// ldCsrSubOnRegCreate -
//
void ldCsrSubOnRegCreate(LdSubCache* regSubCacheP, LdRegCacheItem* regItemP)
{
  fanOutForOneReg(regSubCacheP, regItemP, "newlyMatching");
}



// -----------------------------------------------------------------------------
//
// ldCsrSubOnRegDelete -
//
void ldCsrSubOnRegDelete(LdSubCache* regSubCacheP, LdRegCacheItem* regItemP)
{
  fanOutForOneReg(regSubCacheP, regItemP, "noLongerMatching");
}



// -----------------------------------------------------------------------------
//
// subEligibleForNotify - paused / expired filter
//
static bool subEligibleForNotify(LdSubCacheItem* subItemP)
{
  if (subItemP->status != NULL &&
      (strcmp(subItemP->status, "paused")  == 0 ||
       strcmp(subItemP->status, "expired") == 0))
    return false;

  if (subItemP->expiresAt > 0 && swRest.requestStartTime > subItemP->expiresAt)
    return false;

  return true;
}



// -----------------------------------------------------------------------------
//
// ldCsrSubMatchingSubIds -
//
char** ldCsrSubMatchingSubIds(LdSubCache* regSubCacheP, LdRegCacheItem* regItemP, KAlloc* allocP)
{
  if (regSubCacheP == NULL || regItemP == NULL)
    return NULL;

  // Pass 1: count
  int count = 0;
  for (LdSubCacheItem* s = regSubCacheP->itemList; s != NULL; s = s->next)
  {
    if (!subEligibleForNotify(s))        continue;
    if (!subMatchesReg(s, regItemP))     continue;
    count++;
  }
  if (count == 0)
    return NULL;

  char** v = (char**) kaAlloc(allocP, (count + 1) * sizeof(char*));
  int ix = 0;
  for (LdSubCacheItem* s = regSubCacheP->itemList; s != NULL && ix < count; s = s->next)
  {
    if (!subEligibleForNotify(s))        continue;
    if (!subMatchesReg(s, regItemP))     continue;
    v[ix++] = s->subId;
  }
  v[ix] = NULL;
  return v;
}



// -----------------------------------------------------------------------------
//
// wasInSet - linear search in NULL-terminated char**
//
static bool wasInSet(char** set, const char* subId)
{
  if (set == NULL || subId == NULL) return false;
  for (int i = 0; set[i] != NULL; i++)
    if (strcmp(set[i], subId) == 0) return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// ldCsrSubOnRegUpdate -
//
void ldCsrSubOnRegUpdate(LdSubCache* regSubCacheP,
                         LdRegCacheItem* regItemAfterP,
                         char** wasMatchingIds)
{
  if (regSubCacheP == NULL || regItemAfterP == NULL)
    return;

  LdRegCacheItem* oneItem[1] = { regItemAfterP };

  for (LdSubCacheItem* subItemP = regSubCacheP->itemList; subItemP != NULL; subItemP = subItemP->next)
  {
    if (!subEligibleForNotify(subItemP))
      continue;

    bool wasMatch = wasInSet(wasMatchingIds, subItemP->subId);
    bool nowMatch = subMatchesReg(subItemP, regItemAfterP);

    const char* triggerReason = NULL;
    if (wasMatch && nowMatch)       triggerReason = "updated";
    else if (!wasMatch && nowMatch) triggerReason = "newlyMatching";
    else if (wasMatch && !nowMatch) triggerReason = "noLongerMatching";
    else                            continue;

    sendCsourceNotification(subItemP, oneItem, 1, triggerReason);
  }
}



// -----------------------------------------------------------------------------
//
// ldCsrSubPeriodicTick - § 5.11.7 periodic CSR-Subscription notifications.
//
// Registered with the periodic-dispatch engine. Walks the CSR-Sub cache;
// for each item with timeInterval > 0 whose lastNotification + interval
// has elapsed, collects the currently-matching CSRs and POSTs a single
// CsourceNotification with triggerReason="updated".
//
// `ctx` is the LdSubCache* (regSubCacheP) passed at registration time;
// the regCacheP is read out of the same tenant via the cached `tenantP`
// field of the cache wrapper. We pass it through cacheP->regCacheP which
// the registration site sets for us (see ldCsrSubPeriodicLoopRegister).
//
// `kaP` is per-tick scratch from the engine (already reset before this
// callback fires).
//
typedef struct
{
  LdSubCache*  regSubCache;
  LdRegCache*  regCache;
} CsrSubTickCtx;


static CsrSubTickCtx tickCtxStorage;


static void csrSubPeriodicTick(void* ctx, uint64_t now, KAlloc* kaP)
{
  (void) kaP;  // sendCsourceNotification reaches into swRest.kalloc directly

  CsrSubTickCtx* tcP = (CsrSubTickCtx*) ctx;
  if (tcP == NULL || tcP->regSubCache == NULL || tcP->regCache == NULL) return;

  for (LdSubCacheItem* subItemP = tcP->regSubCache->itemList;
       subItemP != NULL;
       subItemP = subItemP->next)
  {
    // Periodic-only path: skip change-driven CSR-subs.
    if (subItemP->timeInterval <= 0) continue;

    // Status / expiration gating: skip paused / expired. A "failed"
    // sub keeps notifying — delivery failure is not a lifecycle stop.
    if (subItemP->status != NULL &&
        (strcmp(subItemP->status, "paused")  == 0 ||
         strcmp(subItemP->status, "expired") == 0))
      continue;
    if (subItemP->expiresAt > 0 && subItemP->expiresAt <= now)
      continue;
    if (subItemP->endpointUri == NULL)
      continue;

    uint64_t intervalNs = (uint64_t) subItemP->timeInterval * 1000000000ULL;
    if (subItemP->lastNotification + intervalNs > now)
      continue;

    // Collect currently-matching CSRs.
    int cap = 16;
    LdRegCacheItem** matchV = (LdRegCacheItem**) kaAlloc(&swRest.kalloc, cap * sizeof(LdRegCacheItem*));
    int n = 0;
    for (LdRegCacheItem* regItemP = tcP->regCache->itemList; regItemP != NULL; regItemP = regItemP->next)
    {
      if (!subMatchesReg(subItemP, regItemP)) continue;
      if (n == cap)
      {
        int newCap = cap * 2;
        LdRegCacheItem** nv = (LdRegCacheItem**) kaAlloc(&swRest.kalloc, newCap * sizeof(LdRegCacheItem*));
        for (int i = 0; i < n; i++) nv[i] = matchV[i];
        matchV = nv;
        cap    = newCap;
      }
      matchV[n++] = regItemP;
    }

    // § 12.4.7: periodic notifications go out when the interval has
    // elapsed "regardless of any changes to the set of Context Source
    // Registrations" — zero matches → empty data array. Direct send:
    // this thread has no post-response hook to flush a deferral.
    csourceNotificationPost(subItemP, csourceNotificationBuild(subItemP, matchV, n, "updated"));
  }
}



void ldCsrSubPeriodicLoopRegister(LdSubCache* regSubCacheP, LdRegCache* regCacheP)
{
  // Single-tenant for now (broker-wide tenant0). Multi-tenant requires
  // either one registration per tenant or a shared lookup keyed on
  // subItem->tenantP — defer until multi-tenant CSR-subs are exercised.
  tickCtxStorage.regSubCache = regSubCacheP;
  tickCtxStorage.regCache    = regCacheP;
  ldPeriodicLoopRegister(csrSubPeriodicTick, &tickCtxStorage);
}
