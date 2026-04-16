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
#include "kjson/kjBuilder.h"                        // kjChildRemove
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

  if (isLdJson && atCtx == NULL)
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
  // For application/json: inject @context from Link header or default user context
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

    // Inject @context string node into the tree so swldExpandTree picks it up
    if (contextUrl != NULL)
    {
      KjNode* ctxNode = kjString(swRest.kjsonP, "@context", contextUrl);
      kjChildAdd(swRest.in.requestTree, ctxNode);
      swNgsild.userContextUrl = contextUrl;
    }
  }

  swNgsild.contextP = swldExpandTree(swRest.in.requestTree, &swRest.kalloc);

  // If a user context URL was provided but expansion fell back to core context, the download failed
  if (swNgsild.userContextUrl != NULL)
  {
    SwldContext* coreP = swldCoreContext();

    if (swNgsild.contextP == NULL || swNgsild.contextP == coreP)
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

  if (isEntityPayload)
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

  //

  //
  // GeoJSON representation: Accept: application/geo+json
  //
  bool acceptGeoJson = (swRest.in.accept != NULL && strstr(swRest.in.accept, "application/geo+json") != NULL);
  if (acceptGeoJson && treeP != NULL)
  {
    ldToGeoJson(&swRest.out.responseTree, swNgsild.geometryProperty, swRest.kjsonP);
    treeP = swRest.out.responseTree;
    swRest.out.contentType = "application/geo+json";
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
