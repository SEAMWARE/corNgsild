//
// FILE            ldHooks.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdio.h>                                       // snprintf
#include <string.h>                                      // strcmp, strncasecmp, memset

#include "kalloc/kaAlloc.h"                             // kaAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjLookup.h"                             // kjLookup
#include "kjson/kjBuilder.h"                        // kjChildRemove, kjChildAdd
#include "kjson/kjNodeDecouple.h"                   // kjNodeDecouple
#include "swRest/swRest.h"                             // swRest
#include "swRest/SwRestService.h"                      // SwRestService.ldOp
#include "swJsonld/swldInit.h"                             // swldCoreContext
#include "swJsonld/swldExpandTree.h"                       // swldExpandTree
#include "swJsonld/swldCompactTree.h"                      // swldCompactTree, swldCompactTreeWith
#include "swJsonld/swldDownload.h"                         // swldContextFromUrl

#include "swNgsild/LdProblem.h"                          // LD_ERROR_*
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/LdOp.h"                               // LdOpRetrieveEntity, LdOpQueryEntities
#include "swNgsild/SwNgsild.h"                           // swNgsild, ldParamHook
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldEntityToApi.h"                      // ldEntityToApi
#include "swNgsild/ldNameContentCheck.h"                 // ldCheckNamesAndContent
#include "swNgsild/ldPickOmit.h"                         // ldPickOmit
#include "swNgsild/ldToTemporalValues.h"                 // ldToTemporalValues
#include "swNgsild/ldToAggregatedValues.h"               // ldToAggregatedValues, ldIso8601DurationToNs
#include "swNgsild/ldToGeoJson.h"                        // ldToGeoJson
#include "swNgsild/ldStripSysAttrs.h"                    // ldStripSysAttrs
#include "swNgsild/ldLangReduce.h"                       // ldLangReduce
#include "swNgsild/ldAcceptParse.h"                      // ldAcceptParse, LdAcceptType
#include "swNgsild/ldRender.h"                           // ldToSimplified, ldToConcise
#include "swNgsild/LdNormalizeInput.h"                    // ldNormalizeInput
#include "swNgsild/ldHooks.h"                            // Own interface



// -----------------------------------------------------------------------------
//
// ldRequestStartHook - reset per-request NGSI-LD state
//
static void ldRequestStartHook(void)
{
  memset(&swNgsild, 0, sizeof(swNgsild));
  swNgsild.limit = 20;
}



// -----------------------------------------------------------------------------
//
// preExpandCheckCsrEntityTypes - reject empty-string `type` in CSR
// information[].entities[] BEFORE swldExpandTree runs.
//
// JSON-LD @vocab expansion turns "" into the vocab prefix IRI, which
// then sails past the post-expansion validator's empty-string check.
// Catching this here, on the raw tree, avoids that whole loop.
//
// On reject: ldError raised + swNgsild.contextError set; caller
// returns immediately. Returns true if a rejection was raised.
//
static bool preExpandCheckCsrEntityTypes(KjNode* bodyP)
{
  if (bodyP == NULL || bodyP->type != KjObject)
    return false;

  KjNode* infoP = kjLookup(bodyP, "information");
  if (infoP == NULL || infoP->type != KjArray)
    return false;

  for (KjNode* infoElP = infoP->value.firstChildP; infoElP != NULL; infoElP = infoElP->next)
  {
    if (infoElP->type != KjObject)
      continue;

    KjNode* entitiesP = kjLookup(infoElP, "entities");
    if (entitiesP == NULL || entitiesP->type != KjArray)
      continue;

    for (KjNode* entP = entitiesP->value.firstChildP; entP != NULL; entP = entP->next)
    {
      if (entP->type != KjObject)
        continue;

      KjNode* typeP = kjLookup(entP, "type");
      if (typeP == NULL)
        continue;

      if (typeP->type == KjString)
      {
        if (typeP->value.s[0] == 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                  "'entities[].type' must not be empty");
          return true;
        }
      }
      else if (typeP->type == KjArray)
      {
        for (KjNode* elemP = typeP->value.firstChildP; elemP != NULL; elemP = elemP->next)
        {
          if (elemP->type == KjString && elemP->value.s[0] == 0)
          {
            ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                    "'entities[].type' array items must be non-empty strings");
            return true;
          }
        }
      }
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// ldFindEmbeddedAtContext - scan tree for any embedded @context child
//
// Called AFTER swldExpandTree has already stripped the @context from the
// permitted positions (root of an object body, first-level of each array
// element). Anything left with the name "@context" is embedded and must
// be rejected per § 4.5.1 / § 5.5.7.
//
// Returns the offending node, or NULL if none found.
//
static KjNode* ldFindEmbeddedAtContext(KjNode* nodeP)
{
  if (nodeP == NULL)
    return NULL;

  if (nodeP->type == KjObject)
  {
    for (KjNode* c = nodeP->value.firstChildP; c != NULL; c = c->next)
    {
      if (c->name != NULL && strcmp(c->name, "@context") == 0)
        return c;
      KjNode* inner = ldFindEmbeddedAtContext(c);
      if (inner != NULL)
        return inner;
    }
  }
  else if (nodeP->type == KjArray)
  {
    for (KjNode* c = nodeP->value.firstChildP; c != NULL; c = c->next)
    {
      KjNode* inner = ldFindEmbeddedAtContext(c);
      if (inner != NULL)
        return inner;
    }
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// ldParseHook - validate @context and expand incoming JSON-LD payload
//
static void ldParseHook(void)
{
  //
  // POST /jsonldContexts body is itself a JSON-LD Context document (for
  // Hosted) or a { "url": ... } wrapper (for Cached). It is NOT an NGSI-LD
  // payload and must not be expanded. The service routine will read the
  // body as-is.
  //
  if ((swRest.in.urlPath != NULL) &&
      (strncmp(swRest.in.urlPath, "/ngsi-ld/v1/jsonldContexts", 26) == 0))
  {
    return;
  }

  KjNode* atCtx    = kjLookup(swRest.in.requestTree, "@context");
  char*   ct       = swRest.in.contentType;
  bool    isLdJson = (ct != NULL && strncasecmp(ct, "application/ld+json", 19) == 0);

  //
  // Array bodies (batch ops § 5.6.7 / 5.6.8 / 5.6.9 / 5.6.10 / 5.6.20)
  // can't carry a root @context — JSON arrays have no keys. For
  // Content-Type `application/ld+json` the @context then lives on each
  // element; a missing @context on ANY element is BadRequestData. See
  // spec-doubts #15 — the ETSI text doesn't spell this out, but it
  // matches established NGSI-LD listing behaviour.
  //
  bool isArrayBody = (swRest.in.requestTree != NULL &&
                      swRest.in.requestTree->type == KjArray);

  // Per-element check policy:
  //   non-batch  → whole request 400 (one missing @context = whole body bad)
  //   batch      → per-entity error in batchPreErrors, batch handler folds
  //                them into its errors[] (§ 6.14.3.1 multi-status response)
  uint64_t ldOp = (swRest.serviceP != NULL) ? swRest.serviceP->ldOp : 0;
  bool     isBatchOp = (ldOp & LD_OP_GROUP_BATCH) != 0;

  if (isLdJson && isArrayBody && !isBatchOp)
  {
    for (KjNode* elemP = swRest.in.requestTree->value.firstChildP; elemP != NULL; elemP = elemP->next)
    {
      if (elemP->type != KjObject)
        continue;
      if (kjLookup(elemP, "@context") == NULL)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Missing @context",
                "@context is mandatory on every element of an application/ld+json array body");
        swNgsild.contextError = true;
        return;
      }
    }
  }
  else if (isLdJson && isArrayBody && isBatchOp)
  {
    // Walk the array, mark elements missing @context as pre-rejected so the
    // batch handler emits a per-entity BatchEntityError for them. Rejected
    // elements are removed from the tree so expansion ignores them.
    KjNode* prev = NULL;
    KjNode* elemP = swRest.in.requestTree->value.firstChildP;
    while (elemP != NULL)
    {
      KjNode* nextP = elemP->next;
      if (elemP->type == KjObject && kjLookup(elemP, "@context") == NULL)
      {
        if (swNgsild.batchPreErrors == NULL)
          swNgsild.batchPreErrors = kjArray(swRest.kjsonP, NULL);

        const char* eid = "";
        KjNode* idP = kjLookup(elemP, "id");
        if (idP != NULL && idP->type == KjString) eid = idP->value.s;

        KjNode* entry = kjObject(swRest.kjsonP, NULL);
        kjChildAdd(entry, kjString(swRest.kjsonP, "entityId", eid));
        KjNode* errObj = kjObject(swRest.kjsonP, "error");
        kjChildAdd(errObj, kjString (swRest.kjsonP, "type",   LD_ERROR_BAD_REQUEST_DATA));
        kjChildAdd(errObj, kjString (swRest.kjsonP, "title",  "Missing @context"));
        kjChildAdd(errObj, kjInteger(swRest.kjsonP, "status", 400));
        kjChildAdd(errObj, kjString (swRest.kjsonP, "detail", "@context is mandatory on every element of an application/ld+json array body"));
        kjChildAdd(entry, errObj);
        kjChildAdd(swNgsild.batchPreErrors, entry);

        // Splice elemP out of the tree.
        kjNodeDecouple(swRest.in.requestTree, elemP, prev);
      }
      else
      {
        prev = elemP;
      }
      elemP = nextP;
    }
  }
  else if (isLdJson && atCtx == NULL && !isArrayBody)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Missing @context",
            "@context is mandatory for Content-Type application/ld+json");
    swNgsild.contextError = true;
    return;
  }

  if (isLdJson)
  {
    for (int i = 0; i < swRest.in.httpHeaderCount; i++)
    {
      if (strcasecmp(swRest.in.httpHeaderV[i].key, "Link") == 0 &&
          strstr(swRest.in.httpHeaderV[i].value, "json-ld#context") != NULL)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Conflicting @context",
                "both @context in body and Link header not allowed with application/ld+json");
        swNgsild.contextError = true;
        return;
      }
    }
  }

  if (!isLdJson && atCtx != NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Unexpected @context",
            "@context in body not allowed for Content-Type application/json");
    swNgsild.contextError = true;
    return;
  }

  // § 6.3.5 — same rule on the batch path: an array element carrying
  // an in-body @context with Content-Type application/json is per-
  // entity bad request (003_06_01). Mirror the ld+json batch branch.
  if (!isLdJson && isArrayBody && isBatchOp)
  {
    KjNode* prev = NULL;
    KjNode* elemP = swRest.in.requestTree->value.firstChildP;
    while (elemP != NULL)
    {
      KjNode* nextP = elemP->next;
      if (elemP->type == KjObject && kjLookup(elemP, "@context") != NULL)
      {
        if (swNgsild.batchPreErrors == NULL)
          swNgsild.batchPreErrors = kjArray(swRest.kjsonP, NULL);

        const char* eid = "";
        KjNode* idP = kjLookup(elemP, "id");
        if (idP != NULL && idP->type == KjString) eid = idP->value.s;

        KjNode* entry = kjObject(swRest.kjsonP, NULL);
        kjChildAdd(entry, kjString(swRest.kjsonP, "entityId", eid));
        KjNode* errObj = kjObject(swRest.kjsonP, "error");
        kjChildAdd(errObj, kjString(swRest.kjsonP, "type",   LD_ERROR_BAD_REQUEST_DATA));
        kjChildAdd(errObj, kjString(swRest.kjsonP, "title",  "Unexpected @context"));
        kjChildAdd(errObj, kjString(swRest.kjsonP, "detail", "@context in body not allowed for Content-Type application/json"));
        kjChildAdd(entry, errObj);
        kjChildAdd(swNgsild.batchPreErrors, entry);

        kjNodeDecouple(swRest.in.requestTree, elemP, prev);
      }
      else
      {
        prev = elemP;
      }
      elemP = nextP;
    }
  }

  //
  // For application/json: resolve @context from Link header or default user
  // context. For a single-object body the URL is injected as an @context
  // child of the tree root — swldExpandTree picks it up. For an array body
  // (batch op) JSON has no root to attach it to, so set swNgsild.contextP
  // directly; swldExpandTree uses that as the per-element fallback when an
  // element carries no @context of its own (003_04_01).
  //
  if (!isLdJson && swRest.in.requestTree != NULL)
  {
    const char* contextUrl = NULL;

    // Check Link header for context URL
    for (int i = 0; i < swRest.in.httpHeaderCount; i++)
    {
      if (strcasecmp(swRest.in.httpHeaderV[i].key, "Link") == 0 &&
          strstr(swRest.in.httpHeaderV[i].value, "json-ld#context") != NULL)
      {
        // Extract URL from: <URL>; rel="..."; type="..."
        char* v = swRest.in.httpHeaderV[i].value;

        if (v[0] == '<')
        {
          char* end = strchr(v + 1, '>');

          if (end != NULL)
          {
            int   len = end - (v + 1);
            char* url = kaAlloc(&swRest.kalloc, len + 1);

            memcpy(url, v + 1, len);
            url[len] = '\0';
            contextUrl = url;
          }
        }
        break;
      }
    }

    // Fall back to default user context
    if (contextUrl == NULL && ldDefaultContextUrl != NULL)
      contextUrl = ldDefaultContextUrl;

    if (contextUrl != NULL)
    {
      swNgsild.userContextUrl = contextUrl;

      if (!isArrayBody)
      {
        // Single-object body: inject @context so swldExpandTree picks it up.
        KjNode* ctxNode = kjString(swRest.kjsonP, "@context", contextUrl);
        kjChildAdd(swRest.in.requestTree, ctxNode);
      }
      else
      {
        // Array body (batch op): can't inject at the root. Pre-resolve so
        // swldExpandTree uses it as the per-element fallback.
        swNgsild.contextP = swldContextFromUrl(contextUrl, &swRest.kalloc);
      }
    }
  }

  //
  // Fixed-type records — Subscription / ContextSourceRegistration /
  // ContextSourceSubscription — carry a `type` whose value is a JSON-LD
  // -mandated constant. Decouple it (tracking the previous sibling) so
  // expansion can't rebind its value through the user @context, then
  // relink it at its original position after expansion. Service-routine
  // validators don't check `type` for these URLs because this hook has
  // already done it. DB plugins strip `type` separately at their insert
  // call sites so it's not stored.
  //
  const char* recordTypeValue = NULL;
  const char* recordLabel     = NULL;
  KjNode*     typeP           = NULL;
  KjNode*     typePrevP       = NULL;
  if (swRest.in.urlPath != NULL && swRest.in.requestTree != NULL && swRest.in.requestTree->type == KjObject)
  {
    const char* p = swRest.in.urlPath;
    if      (strncmp(p, "/ngsi-ld/v1/subscriptions",         25) == 0) { recordTypeValue = "Subscription";              recordLabel = "Subscription"; }
    else if (strncmp(p, "/ngsi-ld/v1/csourceRegistrations",  32) == 0) { recordTypeValue = "ContextSourceRegistration"; recordLabel = "Registration"; }
    else if (strncmp(p, "/ngsi-ld/v1/csourceSubscriptions",  32) == 0) { recordTypeValue = "Subscription";              recordLabel = "Subscription"; }

    if (recordTypeValue != NULL)
    {
      KjNode* prev = NULL;
      for (KjNode* c = swRest.in.requestTree->value.firstChildP; c != NULL; c = c->next)
      {
        if (c->name != NULL && strcmp(c->name, "type") == 0) { typeP = c; typePrevP = prev; break; }
        prev = c;
      }
      bool isCreate = (swRest.in.verb == SwVerbPost);
      bool isUpdate = (swRest.in.verb == SwVerbPatch);

      // Update / PATCH: the spec doesn't forbid the body from carrying
      // type — only forbids changing it. ETSI 029_05_*/06_*/07_*/08_*/
      // 09_*/10_* PATCH /subscriptions/{id} bodies include
      // `"type": "Subscription"` for completeness; rejecting that as
      // read-only-violation would block all subscription updates. The
      // mismatch check below (line ~336) catches the actual "tried to
      // change type" case.
      (void) isUpdate;
      if (isCreate && typeP == NULL)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Missing Type", "%s 'type' is mandatory for create", recordLabel);
        swNgsild.contextError = true;
        return;
      }
      if (typeP != NULL)
      {
        if (typeP->type != KjString || strcmp(typeP->value.s, recordTypeValue) != 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Type", "%s 'type' must be '%s'", recordLabel, recordTypeValue);
          swNgsild.contextError = true;
          return;
        }
        kjNodeDecouple(swRest.in.requestTree, typeP, typePrevP);
      }
    }
  }

  // Pre-expansion empty-string check for CSR entities[].type — @vocab
  // expansion would otherwise launder "" into a bare-prefix IRI and slip
  // past the post-expansion validator.
  if (recordTypeValue != NULL && strcmp(recordTypeValue, "ContextSourceRegistration") == 0)
  {
    if (preExpandCheckCsrEntityTypes(swRest.in.requestTree))
    {
      swNgsild.contextError = true;
      return;
    }
  }

  // § 4.6.2 / § 4.6.4 — name + content validation on the raw, pre-
  // expansion entity tree. Only for entity payloads — Subscription /
  // CSR carry idPattern regexes and other strings that legitimately
  // include spec-forbidden chars.
  {
    // urlPath is post-wildcard-stripping; the service pattern is the only
    // reliable way to spot attribute-fragment routes ("/ngsi-ld/v1/entities/*/attrs/*").
    bool isAttrFragmentRoute = (swRest.serviceP != NULL
                                && swRest.serviceP->url != NULL
                                && strstr(swRest.serviceP->url, "/attrs/") != NULL);
    bool isEntityPayloadPath = (swRest.in.urlPath != NULL
                                && strncmp(swRest.in.urlPath, "/ngsi-ld/v1/entities", 20) == 0
                                && !isAttrFragmentRoute);
    bool isEntityBatchPath   = (swRest.in.urlPath != NULL
                                && strncmp(swRest.in.urlPath, "/ngsi-ld/v1/entityOperations/", 29) == 0);
    if (isEntityPayloadPath || isEntityBatchPath)
    {
      if (!ldCheckNamesAndContent(swRest.in.requestTree))
      {
        swNgsild.contextError = true;
        return;
      }
    }
  }

  // § 5.2.2 / § 6.3.4 — pre-resolve every URL referenced by an in-body
  // @context. swldExpandTree silently falls back to the broker's core
  // context when a download fails; without this check that produces a
  // (wrongly-)successful 201 with the entity stored under the default
  // context. ETSI 043_01_* expects 504 LdContextNotAvailable for an
  // unreachable @context URL — pre-fetching gives us the offending URL
  // so we can fail fast with a precise error.
  if (atCtx != NULL)
  {
    const char* offendingUrl = NULL;
    KjNode* itemArr[1] = { atCtx };
    int     arrCount   = 1;
    if (atCtx->type == KjArray) { itemArr[0] = atCtx; }   // walk children
    for (int ai = 0; ai < arrCount && offendingUrl == NULL; ai++)
    {
      KjNode* node = itemArr[ai];
      if (node->type == KjString)
      {
        if (swldContextFromUrl(node->value.s, &swRest.kalloc) == NULL)
          offendingUrl = node->value.s;
      }
      else if (node->type == KjArray)
      {
        for (KjNode* c = node->value.firstChildP; c != NULL; c = c->next)
        {
          if (c->type == KjString && swldContextFromUrl(c->value.s, &swRest.kalloc) == NULL)
          { offendingUrl = c->value.s; break; }
        }
      }
      // KjObject (inline @context) needs no fetch.
    }
    if (offendingUrl != NULL)
    {
      ldError(504, LD_ERROR_LD_CONTEXT_NOT_AVAILABLE, "Context Not Available",
              "unable to retrieve @context from '%s'", offendingUrl);
      swNgsild.contextError = true;
      return;
    }
  }

  // § 6.3.4 — same check for batch array bodies (043_01_04). Each
  // element carries its own @context; an unreachable URL on any
  // element becomes a per-entity error in the batch response. The
  // batch handler then folds batchPreErrors into errors[] and the
  // overall status is 207.
  if (isArrayBody && isBatchOp)
  {
    KjNode* prev = NULL;
    KjNode* elemP = swRest.in.requestTree->value.firstChildP;
    while (elemP != NULL)
    {
      KjNode* nextP = elemP->next;
      if (elemP->type == KjObject)
      {
        KjNode* elemCtx = kjLookup(elemP, "@context");
        const char* badUrl = NULL;
        if (elemCtx != NULL && elemCtx->type == KjString)
        {
          if (swldContextFromUrl(elemCtx->value.s, &swRest.kalloc) == NULL)
            badUrl = elemCtx->value.s;
        }
        else if (elemCtx != NULL && elemCtx->type == KjArray)
        {
          for (KjNode* c = elemCtx->value.firstChildP; c != NULL; c = c->next)
          {
            if (c->type == KjString && swldContextFromUrl(c->value.s, &swRest.kalloc) == NULL)
            { badUrl = c->value.s; break; }
          }
        }
        if (badUrl != NULL)
        {
          if (swNgsild.batchPreErrors == NULL)
            swNgsild.batchPreErrors = kjArray(swRest.kjsonP, NULL);

          const char* eid = "";
          KjNode* idP = kjLookup(elemP, "id");
          if (idP != NULL && idP->type == KjString) eid = idP->value.s;

          KjNode* entry = kjObject(swRest.kjsonP, NULL);
          kjChildAdd(entry, kjString(swRest.kjsonP, "entityId", eid));
          KjNode* errObj = kjObject(swRest.kjsonP, "error");
          kjChildAdd(errObj, kjString(swRest.kjsonP, "type",   LD_ERROR_LD_CONTEXT_NOT_AVAILABLE));
          kjChildAdd(errObj, kjString(swRest.kjsonP, "title",  "Context Not Available"));
          char detail[512];
          snprintf(detail, sizeof(detail), "unable to retrieve @context from '%s'", badUrl);
          kjChildAdd(errObj, kjString(swRest.kjsonP, "detail", detail));
          kjChildAdd(entry, errObj);
          kjChildAdd(swNgsild.batchPreErrors, entry);

          kjNodeDecouple(swRest.in.requestTree, elemP, prev);
        }
        else
        {
          prev = elemP;
        }
      }
      else
      {
        prev = elemP;
      }
      elemP = nextP;
    }
  }

  // Capture the raw in-body @context node BEFORE swldExpandTree strips
  // it. § 5.5.5 / § 5.8.1.4: a Subscription with an inline-array or
  // inline-object @context becomes an ImplicitlyCreated @context whose
  // served URL is the subscription's `jsonldContext`. postSubscriptions
  // reads userContextBody to do that auto-population. A bare-string
  // @context already carries its own URL, so we don't need to capture
  // anything for that case.
  if (atCtx != NULL && (atCtx->type == KjArray || atCtx->type == KjObject))
    swNgsild.userContextBody = atCtx;

  // § 4.17 — for Subscription bodies, an entities[].type may carry a
  // type-selection expression like "(Building|Tower)". JSON-LD's
  // @vocab fallback would otherwise rewrite it to
  // "<vocab>(Building|Tower)" — a polluted IRI that no §4.17 parser
  // can recover (046_16_01). Decouple every entities[].type whose
  // value contains a §4.17 operator BEFORE expansion so JSON-LD
  // never sees it, then reattach after. Same pattern the top-level
  // `type` field uses above.
  struct {
    KjNode* parentP;
    KjNode* prevP;
    KjNode* typeP;
  } typeExprNodesV[16];
  int typeExprNodeN = 0;
  if (recordTypeValue != NULL && strcmp(recordTypeValue, "Subscription") == 0)
  {
    KjNode* entitiesP = kjLookup(swRest.in.requestTree, "entities");
    if (entitiesP != NULL && entitiesP->type == KjArray)
    {
      for (KjNode* selP = entitiesP->value.firstChildP;
           selP != NULL && typeExprNodeN < (int)(sizeof(typeExprNodesV)/sizeof(typeExprNodesV[0]));
           selP = selP->next)
      {
        if (selP->type != KjObject) continue;
        KjNode* prev = NULL;
        KjNode* tP   = NULL;
        for (KjNode* c = selP->value.firstChildP; c != NULL; c = c->next)
        {
          if (c->name != NULL && strcmp(c->name, "type") == 0) { tP = c; break; }
          prev = c;
        }
        if (tP == NULL || tP->type != KjString || tP->value.s == NULL) continue;
        bool hasOp = false;
        for (const char* p = tP->value.s; *p != 0 && !hasOp; p++)
          if (*p == '(' || *p == ')' || *p == '|' || *p == '&' || *p == ',') hasOp = true;
        if (!hasOp) continue;
        typeExprNodesV[typeExprNodeN].parentP = selP;
        typeExprNodesV[typeExprNodeN].prevP   = prev;
        typeExprNodesV[typeExprNodeN].typeP   = tP;
        typeExprNodeN++;
        kjNodeDecouple(selP, tP, prev);
      }
    }
  }

  // ldUrlParams.c has already set swNgsild.contextP from the Link header
  // (or to the core context if no Link). swldExpandTree uses that as the
  // base context; an in-body @context overrides it for the body subtree
  // and becomes the new effective context (returned and chained back
  // into swNgsild.contextP).
  swNgsild.contextP = swldExpandTree(swRest.in.requestTree, swNgsild.contextP, &swRest.kalloc);

  // Reattach the decoupled type-selection expressions at their
  // original positions inside each selector object.
  for (int i = 0; i < typeExprNodeN; i++)
  {
    KjNode* parentP = typeExprNodesV[i].parentP;
    KjNode* prevP   = typeExprNodesV[i].prevP;
    KjNode* tP      = typeExprNodesV[i].typeP;
    if (prevP == NULL)
    {
      tP->next = parentP->value.firstChildP;
      parentP->value.firstChildP = tP;
      if (parentP->lastChild == NULL) parentP->lastChild = tP;
    }
    else
    {
      tP->next = prevP->next;
      prevP->next = tP;
      if (prevP == parentP->lastChild) parentP->lastChild = tP;
    }
  }

  //
  // Relink `type` at its original position (right after typePrevP, or at
  // the head if there was no previous sibling). Tree returns to caller
  // with `type` back in place — service routines, cache, and render see
  // a normal tree.
  //
  if (typeP != NULL)
  {
    if (typePrevP == NULL)
    {
      typeP->next = swRest.in.requestTree->value.firstChildP;
      swRest.in.requestTree->value.firstChildP = typeP;
      if (swRest.in.requestTree->lastChild == NULL) swRest.in.requestTree->lastChild = typeP;
    }
    else
    {
      typeP->next = typePrevP->next;
      typePrevP->next = typeP;
      if (typePrevP == swRest.in.requestTree->lastChild) swRest.in.requestTree->lastChild = typeP;
    }
  }

  //
  // § 4.5.1: "Attributes shall not contain any embedded @context."
  // § 5.5.7: user @context shall not be embedded into NGSI-LD Attributes and
  // shall not contain JSON-LD Scoped Contexts — they could rebind Core
  // terms (type, value, observedAt, ...) and corrupt the data model.
  //
  // swldExpandTree has already removed the PERMITTED @contexts (root of an
  // object body, first-level of each array element). If anything named
  // @context remains anywhere in the tree it's embedded → 400.
  //
  if (swRest.in.requestTree != NULL)
  {
    KjNode* offender = ldFindEmbeddedAtContext(swRest.in.requestTree);
    if (offender != NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Embedded @context",
              "@context is only allowed at the top of an entity (root for an object body, "
              "first-level of each element for a batch array) — embedding it inside an "
              "attribute, sub-attribute, or value is forbidden by § 4.5.1 / § 5.5.7");
      swNgsild.contextError = true;
      return;
    }
  }

  // If a user context URL was provided but expansion fell back to core context, the download failed.
  // Exception: when the user-provided URL IS the core context URL (e.g. inter-broker
  // notifications carrying the core context Link), resolving to coreP is correct, not a failure.
  if (swNgsild.userContextUrl != NULL)
  {
    SwldContext* coreP = swldCoreContext();
    bool userUrlIsCore = (coreP != NULL && coreP->url != NULL
                          && strcmp(swNgsild.userContextUrl, coreP->url) == 0);

    if ((swNgsild.contextP == NULL || swNgsild.contextP == coreP) && !userUrlIsCore)
    {
      ldError(504, LD_ERROR_LD_CONTEXT_NOT_AVAILABLE, "Context Not Available",
              "unable to retrieve @context from '%s'", swNgsild.userContextUrl);
      swNgsild.contextError = true;
      return;
    }
  }

  //
  // Normalize input: convert simplified/concise NGSI-LD to normalized format.
  // Only applies to entity payloads — subscription and registration payloads
  // are passed through as-is.
  //
  bool isEntityPayload = (swRest.in.urlPath != NULL
                          && strncmp(swRest.in.urlPath, "/ngsi-ld/v1/entities", 20) == 0);

  //
  // PATCH/PUT/DELETE on /entities/{id}/attrs/{attrId} carry an attribute
  // fragment, not an entity fragment — so treating the top level as an
  // entity (and wrapping its scalar children as simplified attributes)
  // would corrupt the payload.
  //
  // swRest.in.urlPath has had its wildcard suffix stripped by the router
  // (e.g. "/ngsi-ld/v1/entities/urn:V/attrs/isParked" → "/ngsi-ld/v1/entities/urn:V"),
  // so checking it for "/attrs/" misses every attribute-fragment route.
  // Use the matched service's pattern URL — that one keeps the full shape
  // including wildcards, e.g. "/ngsi-ld/v1/entities/*/attrs/*".
  bool isAttrFragmentUrl = (swRest.serviceP != NULL
                            && swRest.serviceP->url != NULL
                            && strstr(swRest.serviceP->url, "/attrs/") != NULL);

  if (isEntityPayload && !isAttrFragmentUrl)
  {
    // PATCH /entities/{id} is Merge Entity (§ 5.6.17): a plain-object attribute
    // fragment without type/value is a partial update carrying sub-attributes,
    // not a simplified Property whose value happens to be an object.
    bool mergeMode = (swRest.in.verb == SwVerbPatch);
    ldNormalizeInput(swRest.in.requestTree, &swRest.kalloc, mergeMode);
  }
}



// -----------------------------------------------------------------------------
//
// filterDatasetId - keep only attribute instances matching requested datasetIds
//
// Operates on storage-format entity tree (before ldEntityToApi).
// Each attribute is an object whose children are keyed by datasetId
// (e.g. "@none", "urn:x").  We remove children not in datasetIdV.
// If all children are removed, remove the attribute from the entity.
//
static void filterDatasetId(KjNode* entityP, char** datasetIdV)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  KjNode* childP = entityP->value.firstChildP;

  while (childP != NULL)
  {
    KjNode* nextP = childP->next;

    // Skip entity-level keywords (id, type, @context, scope, timestamps)
    if (childP->type != KjObject || childP->name == NULL
        || strcmp(childP->name, "id")   == 0 || strcmp(childP->name, "@id")   == 0
        || strcmp(childP->name, "type") == 0 || strcmp(childP->name, "@type") == 0
        || strcmp(childP->name, "@context") == 0
        || strcmp(childP->name, LD_VOCAB_SCOPE)       == 0
        || strcmp(childP->name, LD_VOCAB_CREATED_AT)  == 0
        || strcmp(childP->name, LD_VOCAB_MODIFIED_AT) == 0)
    {
      childP = nextP;
      continue;
    }

    // childP is a dataset-keyed attribute wrapper — filter its children
    KjNode* instP = childP->value.firstChildP;

    while (instP != NULL)
    {
      KjNode* instNextP = instP->next;
      bool    keep      = false;

      for (int i = 0; datasetIdV[i] != NULL; i++)
      {
        if (strcmp(instP->name, datasetIdV[i]) == 0)
        {
          keep = true;
          break;
        }
      }

      if (!keep)
        kjChildRemove(childP, instP);

      instP = instNextP;
    }

    // If no instances left, remove the attribute entirely
    if (childP->value.firstChildP == NULL)
      kjChildRemove(entityP, childP);

    childP = nextP;
  }
}



// -----------------------------------------------------------------------------
//
// ldRenderHook - compact outgoing JSON-LD payload
//
static void ldRenderHook(void)
{
  // § 6.3.4 — Accept negotiation runs FIRST, before any body work.
  // It must trump an earlier 4xx (e.g. retrieving a non-existent
  // entity with Accept: application/xml answers 406, not 404), so the
  // check sits in the renderHook (called even on error paths) and
  // overrides whatever problemType the service routine set.
  {
    LdAcceptType acceptType    = ldAcceptParse(swRest.in.accept);
    uint64_t     ldOp          = (swRest.serviceP != NULL) ? swRest.serviceP->ldOp : 0;
    bool         entityReadOp  = (ldOp & (LdOpRetrieveEntity | LdOpQueryEntities | LdOpBatchQuery)) != 0;

    if (acceptType == LdAcceptNone ||
        (acceptType == LdAcceptGeoJson && !entityReadOp))
    {
      ldError(406, LD_ERROR_INVALID_REQUEST, "Not Acceptable",
              "supported response media types: application/json, application/ld+json%s",
              entityReadOp ? ", application/geo+json" : "");
      return;
    }
  }

  // Bail out of body formatting on error — the response is a
  // ProblemDetails JSON object built by swRest from problemType /
  // problemTitle / problemDetail; nothing for us to compact.
  if (swRest.out.problemType != NULL)
    return;

  KjNode* treeP = swRest.out.responseTree;

  //
  // Entity-specific transforms (skip for rawResponse, e.g. subscription responses)
  //
  if (!swNgsild.rawResponse)
  {
    // Apply datasetId filtering on storage-format tree (before ldEntityToApi)
    if (swNgsild.datasetIdV != NULL)
    {
      if (treeP != NULL && treeP->type == KjArray)
      {
        for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
          filterDatasetId(itemP, swNgsild.datasetIdV);
      }
      else
      {
        filterDatasetId(treeP, swNgsild.datasetIdV);
      }
    }

    // Convert storage format to API format
    if (treeP != NULL && treeP->type == KjArray)
    {
      for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
        ldEntityToApi(itemP, &swRest.kalloc);
    }
    else
    {
      ldEntityToApi(treeP, &swRest.kalloc);
    }

    if (swNgsild.sysAttrs == false)
      ldStripSysAttrs(swRest.out.responseTree);

    // Apply lang reduction BEFORE format simplification — the simplification
    // strips the LanguageProperty wrapper, leaving the languageMap dict.
    // Reducing afterwards would have nothing to reduce.
    if (swNgsild.lang != NULL)
    {
      if (treeP != NULL && treeP->type == KjArray)
      {
        for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
          ldLangReduce(itemP, swNgsild.lang, &swRest.kalloc);
      }
      else
      {
        ldLangReduce(treeP, swNgsild.lang, &swRest.kalloc);
      }
    }

    // Apply representation format (simplified/concise/normalized)
    if (swNgsild.format == LdFormatSimplified || swNgsild.format == LdFormatConcise)
    {
      void (*formatFn)(KjNode*, KAlloc*) = (swNgsild.format == LdFormatSimplified) ? (void(*)(KjNode*, KAlloc*)) ldToSimplified : (void(*)(KjNode*, KAlloc*)) ldToConcise;

      if (treeP != NULL && treeP->type == KjArray)
      {
        for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
          formatFn(itemP, &swRest.kalloc);
      }
      else
      {
        formatFn(treeP, &swRest.kalloc);
      }
    }

    // § 4.5.8: simplified temporal representation. Runs after ldEntityToApi
    // (which is a no-op on temporal trees — attrs are KjArrays, skipped) so
    // the resulting object-shaped attrs aren't re-mangled by ldEntityToApi.
    if (swNgsild.format == LdFormatTemporalValues)
      ldToTemporalValues(treeP, swNgsild.timeproperty, swRest.kjsonP, &swRest.kalloc);

    // § 4.5.20: aggregated temporal representation. Numeric Property only
    // for now. Same renderHook position as temporalValues — runs after
    // ldEntityToApi so the bucketed-object attrs aren't re-mangled.
    if (swNgsild.format == LdFormatAggregatedValues &&
        swNgsild.aggrMethodsV != NULL && swNgsild.aggrPeriodDuration != NULL)
    {
      uint64_t periodNs = ldIso8601DurationToNs(swNgsild.aggrPeriodDuration);
      uint64_t startNs  = swNgsild.timeAtNs;       // 0 → ldToAggregatedValues uses earliest sample seen
      uint64_t endNs    = swNgsild.endTimeAtNs;    // 0 → ldToAggregatedValues uses latest sample seen
      if (periodNs > 0)
        ldToAggregatedValues(treeP, swNgsild.aggrMethodsV, periodNs, startNs, endNs,
                             swNgsild.timeproperty, swRest.kjsonP, &swRest.kalloc);
    }
  }

  // § 6.3.4 Accept negotiation already ran at the top of this hook —
  // here we only need the GeoJSON branch decision for the format
  // path (the unacceptable-Accept case has already returned).
  LdAcceptType acceptType    = ldAcceptParse(swRest.in.accept);
  bool         acceptGeoJson = (acceptType == LdAcceptGeoJson);
  if (acceptGeoJson && treeP != NULL)
  {
    ldToGeoJson(&swRest.out.responseTree, swNgsild.geometryProperty, swRest.kjsonP);
    treeP = swRest.out.responseTree;
    swRest.out.contentType = "application/geo+json";
  }

  // Resolve the response context. Honors (in order):
  //   - body @context already parsed into swNgsild.contextP (ld+json POSTs),
  //   - Link header URL captured into swNgsild.userContextUrl
  //     (json POSTs/PATCHes — captured by ldParseHook when a body is present),
  //   - Link header parsed here (json GETs / DELETEs — no body, so the
  //     parse hook never ran the URL-capture branch),
  //   - core context (last-resort).
  // The same context is used both to compact the response and to advertise
  // it via Link / inline @context further below.
  SwldContext* respCtxP = NULL;
  if (swNgsild.contextP != NULL)
  {
    respCtxP = swNgsild.contextP;
    if (respCtxP->url == NULL && respCtxP->isArray && respCtxP->contexts == 1
        && respCtxP->contextV != NULL && respCtxP->contextV[0] != NULL)
      respCtxP = respCtxP->contextV[0];
  }
  if (respCtxP == NULL || respCtxP->url == NULL)
  {
    const char* linkUrl = swNgsild.userContextUrl;
    if (linkUrl == NULL)
    {
      for (int i = 0; i < swRest.in.httpHeaderCount; i++)
      {
        if (strcasecmp(swRest.in.httpHeaderV[i].key, "Link") == 0 &&
            strstr(swRest.in.httpHeaderV[i].value, "json-ld#context") != NULL)
        {
          char* v = swRest.in.httpHeaderV[i].value;
          if (v[0] == '<')
          {
            char* end = strchr(v + 1, '>');
            if (end != NULL)
            {
              int   len = end - (v + 1);
              char* url = kaAlloc(&swRest.kalloc, len + 1);
              memcpy(url, v + 1, len);
              url[len] = '\0';
              linkUrl = url;
            }
          }
          break;
        }
      }
    }
    if (linkUrl != NULL)
      respCtxP = swldContextFromUrl(linkUrl, &swRest.kalloc);
  }
  if (respCtxP == NULL)
    respCtxP = swldCoreContext();

  if (respCtxP != NULL)
    swldCompactTreeWith(swRest.out.responseTree, respCtxP);
  else
    swldCompactTree(swRest.out.responseTree);

  // (lang reduction now happens earlier — before format simplification)

  // @context in response: either in body (ld+json) or via Link header (json)
  //
  if (treeP == NULL)
    return;

  SwldContext* ctxP   = respCtxP;
  const char*  ctxUrl = (ctxP != NULL) ? ctxP->url : NULL;
  bool         acceptLdJson = (acceptType == LdAcceptLdJson);

  // application/ld+json: @context lives in the body. Content-Type set to
  // application/ld+json.
  //
  // application/geo+json: per § 5.7.1.4 / § 6.3.7, when the Prefer header
  // is omitted or set to body=ld+json (the default), the Feature(Collection)
  // shall ALSO carry @context in the body in addition to the Link header.
  // We follow the default; an explicit Prefer body=json would suppress the
  // body @context (not yet wired — TODO).
  //
  // application/json: @context only via Link header (§ 6.3.5).
  bool injectCtxIntoBody = (acceptLdJson || acceptGeoJson);

  if (injectCtxIntoBody && ctxUrl != NULL)
  {
    if (treeP->type == KjArray)
    {
      for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
      {
        if (itemP->type == KjObject)
        {
          KjNode* ctxNode = kjString(swRest.kjsonP, "@context", ctxUrl);
          kjChildAdd(itemP, ctxNode);
        }
      }
    }
    else if (treeP->type == KjObject)
    {
      KjNode* ctxNode = kjString(swRest.kjsonP, "@context", ctxUrl);
      kjChildAdd(treeP, ctxNode);
    }

    if (acceptLdJson)
      swRest.out.contentType = "application/ld+json";
    // (geo+json content-type was already set above when ldToGeoJson ran)
  }

  if (ctxUrl != NULL && !acceptLdJson)
  {
    // Advertise the context via Link header for application/json
    // responses. § 6.3.5: with application/ld+json the @context is
    // already inline in the body — adding a Link header in that case
    // is duplicative and trips conformance tests that split the Link
    // header by comma to count pagination relations (031_02_*,
    // 041_03_*, 046_14_01).
    static const char suffix[] = ">; rel=\"http://www.w3.org/ns/json-ld#context\"; type=\"application/ld+json\"";
    int               linkLen  = 1 + strlen(ctxUrl) + (sizeof(suffix) - 1) + 1;
    char*             linkBuf  = kaAlloc(&swRest.kalloc, linkLen);

    strcpy(linkBuf, "<");
    strcat(linkBuf, ctxUrl);
    strcat(linkBuf, suffix);

    SwRestKeyValue* hV = swRest.out.headerV;
    int ix = swRest.out.headerCount;
    hV[ix].key   = "Link";
    hV[ix].value = linkBuf;
    swRest.out.headerCount = ix + 1;
  }
}



// -----------------------------------------------------------------------------
//
// ldHooksRegister - register all NGSI-LD hooks with swRest
//
void ldHooksRegister(void)
{
  swRestSetRequestStartHook(ldRequestStartHook);
  swRestSetPayloadParseHook(ldParseHook);
  swRestSetPayloadRenderHook(ldRenderHook);
  swRestSetParamHook(ldParamHook);
}
