//
// FILE            ldHooks.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <string.h>                                      // strcmp, strncasecmp, memset

#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjLookup.h"                             // kjLookup
#include "kjson/kjBuilder.h"                        // kjChildRemove
#include "swRest/swRest.h"                             // swRest
#include "swJsonld/swldExpandTree.h"                       // swldExpandTree
#include "swJsonld/swldCompactTree.h"                      // swldCompactTree

#include "swNgsild/LdProblem.h"                          // LD_ERROR_*
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/SwNgsild.h"                           // swNgsild, ldParamHook
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldEntityToApi.h"                      // ldEntityToApi
#include "swNgsild/ldPickOmit.h"                         // ldPickOmit
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

  swNgsild.contextP = swldExpandTree(swRest.in.requestTree, &swRest.kalloc);
  ldNormalizeInput(swRest.in.requestTree, &swRest.kalloc);
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
