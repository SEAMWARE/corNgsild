//
// FILE            ldHooks.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <string.h>                                      // strcmp, strncasecmp, memset

#include "kalloc/kaAlloc.h"                             // kaAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjLookup.h"                             // kjLookup
#include "kjson/kjBuilder.h"                        // kjChildRemove, kjChildAdd
#include "kjson/kjNodeDecouple.h"                   // kjNodeDecouple
#include "swRest/swRest.h"                             // swRest
#include "swJsonld/swldInit.h"                             // swldCoreContext
#include "swJsonld/swldExpandTree.h"                       // swldExpandTree
#include "swJsonld/swldCompactTree.h"                      // swldCompactTree

#include "swNgsild/LdProblem.h"                          // LD_ERROR_*
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/SwNgsild.h"                           // swNgsild, ldParamHook
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldEntityToApi.h"                      // ldEntityToApi
#include "swNgsild/ldPickOmit.h"                         // ldPickOmit
#include "swNgsild/ldToGeoJson.h"                        // ldToGeoJson
#include "swNgsild/ldStripSysAttrs.h"                    // ldStripSysAttrs
#include "swNgsild/ldLangReduce.h"                       // ldLangReduce
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

  if (isLdJson && isArrayBody)
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
  else if (isLdJson && atCtx == NULL)
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

  //
  // For application/json: inject @context from Link header or default user context.
  // Skip array bodies — JSON arrays can't carry @context at the root.
  //
  if (!isLdJson && swRest.in.requestTree != NULL && !isArrayBody)
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

    // Inject @context string node into the tree so swldExpandTree picks it up
    if (contextUrl != NULL)
    {
      KjNode* ctxNode = kjString(swRest.kjsonP, "@context", contextUrl);
      kjChildAdd(swRest.in.requestTree, ctxNode);
      swNgsild.userContextUrl = contextUrl;
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

      if (isUpdate && typeP != NULL)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Read-Only Field", "'type' cannot be modified");
        swNgsild.contextError = true;
        return;
      }
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

  swNgsild.contextP = swldExpandTree(swRest.in.requestTree, &swRest.kalloc);

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
      ldError(503, LD_ERROR_LD_CONTEXT_NOT_AVAILABLE, "Context Not Available",
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
  bool isAttrFragmentUrl = (swRest.in.urlPath != NULL
                            && strstr(swRest.in.urlPath, "/attrs/") != NULL);

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
  }

  //
  // GeoJSON representation: Accept: application/geo+json
  // Runs BEFORE compaction — geometryProperty is an expanded IRI and the
  // tree still has expanded attr names at this point.
  //
  bool acceptGeoJson = (swRest.in.accept != NULL && strstr(swRest.in.accept, "application/geo+json") != NULL);
  if (acceptGeoJson && treeP != NULL)
  {
    ldToGeoJson(&swRest.out.responseTree, swNgsild.geometryProperty, swRest.kjsonP);
    treeP = swRest.out.responseTree;
    swRest.out.contentType = "application/geo+json";
  }

  swldCompactTree(swRest.out.responseTree);

  // Apply lang reduction after compaction (languageMap keys are BCP47 tags, not JSON-LD terms)
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

  // @context in response: either in body (ld+json) or via Link header (json)
  //
  if (treeP == NULL)
    return;

  SwldContext* ctxP   = (swNgsild.contextP != NULL) ? swNgsild.contextP : swldCoreContext();
  const char*  ctxUrl = (ctxP != NULL) ? ctxP->url : NULL;
  bool         acceptLdJson = (swRest.in.accept != NULL && strstr(swRest.in.accept, "application/ld+json") != NULL);

  if (acceptLdJson && ctxUrl != NULL)
  {
    // Inject @context into response body and set Content-Type
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

    swRest.out.contentType = "application/ld+json";
  }
  else if (ctxUrl != NULL)
  {
    // Add Link header for application/json responses
    const char* suffix  = ">; rel=\"http://www.w3.org/ns/json-ld#context\"; type=\"application/ld+json\"";
    int         linkLen = 1 + strlen(ctxUrl) + strlen(suffix) + 1;
    char*       linkBuf = kaAlloc(&swRest.kalloc, linkLen);

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
