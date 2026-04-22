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
#include <regex.h>                                     // regexec
#include <stdio.h>                                     // snprintf
#include <string.h>                                    // strcmp, strlen
#include <time.h>                                      // clock_gettime

#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjBuilder.h"                           // kjObject, kjString, kjArray, kjChildAdd
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjRenderSize.h"                        // kjFastRenderSize
#include "kjson/kjRender.h"                            // kjFastRender

#include "swRest/SwRestState.h"                        // swRest
#include "swRest/swRestClient.h"                       // SwRestClientRequest, etc.
#include "swJsonld/swldCompactTree.h"                  // swldCompactTree
#include "swJsonld/swldInit.h"                         // swldCoreContext
#include "swJsonld/SwldContext.h"                      // SwldContext

#include "swNgsild/LdSubCache.h"                       // LdSubCacheItem, LdSubEntitySelector
#include "swNgsild/LdRegCache.h"                       // LdRegCache, LdRegCacheItem, LdRegInfo, LdRegEntityInfo
#include "swNgsild/swNgsild.h"                         // swNgsild (for tenant access via opaque)
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
// sendCsourceNotification - build and POST the notification
//
static void sendCsourceNotification(LdSubCacheItem* subItemP,
                                    LdRegCacheItem** matchV, int matchN,
                                    const char* triggerReason)
{
  if (subItemP->endpointUri == NULL || matchN == 0)
    return;

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
    if (matchV[i]->regTree != NULL)
      kjChildAdd(dataArray, kjClone(swRest.kjsonP, matchV[i]->regTree));
  }
  kjChildAdd(notification, dataArray);

  swldCompactTree(notification);

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

  swRestClientRequestBody(&req, body, strlen(body));
  swRestClientRequestTimeout(&req, 5000, 10000);
  swRestClientSend(&req, &resp);

  subItemP->timesSent++;
  subItemP->lastNotification = swRest.requestStartTime;

  if (resp.statusCode >= 200 && resp.statusCode < 300)
    subItemP->lastSuccess = swRest.requestStartTime;
  else
  {
    subItemP->timesFailed++;
    subItemP->lastFailure = swRest.requestStartTime;
  }
}



// -----------------------------------------------------------------------------
//
// ldCsrSubInitialNotify -
//
void ldCsrSubInitialNotify(LdRegCache* regCacheP, LdSubCacheItem* subItemP)
{
  if (regCacheP == NULL || subItemP == NULL || subItemP->endpointUri == NULL)
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

  // § 5.11.7: send initial notification even if data is empty? Spec says
  // "on initial subscription ... the Context Source Registration(s) that
  // match". If zero match, don't POST (no payload content).
  if (n == 0)
    return;

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
