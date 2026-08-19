//
// FILE            ldCheckRegistration.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Validation of CSourceRegistration payloads (NGSI-LD § 5.2.9 / § 5.9.2).
//
// Mode-specific conflict checks (exclusive/redirect overlap, "entity already
// exists locally with any registered attribute" — § 5.9.2) need access to
// the registration cache and the entity store, so they live in the service
// routines, not here.
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint64_t
#include <string.h>                                      // strcmp
#include <regex.h>                                       // regcomp, regfree

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjLookup.h"                             // kjLookup
#include "kjson/kjBuilder.h"                            // kjChildAdd, kjChildRemove

#include "corRest/CorRestState.h"                          // corRest (requestStartTime)

#include "corNgsild/LdOp.h"                               // LdOp
#include "corNgsild/LdCheck.h"                            // OBJECT_CHECK, ARRAY_CHECK, ...
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "corNgsild/ldCheckDateTime.h"                    // ldIsoToNanoseconds
#include "corNgsild/ldCheckUri.h"                         // ldCheckUri
#include "corNgsild/ldCheckGeo.h"                         // ldCheckGeo
#include "corNgsild/ldTypes.h"                            // ldOpToString
#include "corNgsild/ldError.h"                            // ldError
#include "corNgsild/ldCheckRegistration.h"                // Own interface
#include "corNgsild/ldTraceLevels.h"                      // LdTCheckReg



// -----------------------------------------------------------------------------
//
// checkEntityInfo - validate one EntityInfo object (§ 5.2.8)
//
// type is mandatory (string or string[]); id and idPattern are optional.
// id takes precedence over idPattern.
//
static bool checkEntityInfo(KjNode* entP)
{
  OBJECT_CHECK(entP, "Invalid Registration", "'information[].entities[]' items must be JSON objects");

  KjNode* typeP    = NULL;
  KjNode* idP      = NULL;
  KjNode* idPatP   = NULL;

  for (KjNode* fP = entP->value.firstChildP; fP != NULL; fP = fP->next)
  {
    if      (strcmp(fP->name, "type") == 0)                    typeP  = fP;
    else if (strcmp(fP->name, "id") == 0)                      idP    = fP;
    else if (strcmp(fP->name, LD_VOCAB_ID_PATTERN) == 0)       idPatP = fP;
    else
    {
      // EntityInfo (§ 5.2.8) is a closed set: type, id, idPattern.
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'entities[]' item has unknown member '%s' (allowed: type, id, idPattern)", fP->name);
      return false;
    }
  }

  MANDATORY_CHECK(typeP, "Invalid Registration", "'entities[].type' is mandatory");

  if (typeP->type == KjString)
  {
    if (typeP->value.s[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration", "'entities[].type' must not be empty");
      return false;
    }
  }
  else if (typeP->type == KjArray)
  {
    EMPTY_ARRAY_CHECK(typeP, "'entities[].type' array must not be empty");
    for (KjNode* tP = typeP->value.firstChildP; tP != NULL; tP = tP->next)
    {
      STRING_CHECK(tP, "Invalid Registration", "'entities[].type' array items must be strings");
    }
  }
  else
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration", "'entities[].type' must be a string or array of strings");
    return false;
  }

  if (idP != NULL)
  {
    STRING_CHECK(idP, "Invalid Registration", "'entities[].id' must be a URI string");
    URI_CHECK(idP->value.s);
  }

  if (idPatP != NULL)
  {
    STRING_CHECK(idPatP, "Invalid Registration", "'entities[].idPattern' must be a regex string");

    if (idPatP->value.s[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration", "'entities[].idPattern' must not be empty");
      return false;
    }

    // § 5.2.8 — idPattern is a "Regular expression as per IEEE 1003.2". Reject a
    // pattern that won't compile rather than store one that can never match.
    regex_t re;
    if (regcomp(&re, idPatP->value.s, REG_EXTENDED | REG_NOSUB) != 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'entities[].idPattern' is not a valid regular expression");
      return false;
    }
    regfree(&re);
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// checkStringArrayNonEmpty - non-empty array of non-empty strings
//
static bool checkStringArrayNonEmpty(KjNode* arrP, const char* fieldName)
{
  if (arrP->type != KjArray)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration", "'%s' must be an array of strings", fieldName);
    return false;
  }

  EMPTY_ARRAY_CHECK(arrP, "'information[].attributeNames' must not be empty");

  for (KjNode* sP = arrP->value.firstChildP; sP != NULL; sP = sP->next)
  {
    if (sP->type != KjString || sP->value.s[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration", "'%s' items must be non-empty strings", fieldName);
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// checkExclusiveStructure - § 9.3.3 structural rules for exclusive mode
//
// An exclusive registration "shall always relate to specific Attributes
// found on a single Entity" — so every information[] element must:
//   - have entities[], every entry carrying a specific `id` (no idPattern,
//     no type-only entries — those describe groups, which the spec
//     explicitly says are "not supported for exclusive registrations").
//   - have a non-empty attributeNames (no entity-wide claim).
//
// Caller has already run checkInformationArray AND regInfoMergeAttributeNames,
// so the array shape is known good, the deprecated propertyNames /
// relationshipNames have been folded into attributeNames, and `entities` (if
// present) is a non-empty array of objects with at least `type`.
//
static bool checkExclusiveStructure(KjNode* infoArrayP)
{
  for (KjNode* infoP = infoArrayP->value.firstChildP; infoP != NULL; infoP = infoP->next)
  {
    KjNode* entitiesP = kjLookup(infoP, LD_VOCAB_ENTITIES);
    KjNode* attrNamesP = kjLookup(infoP, "attributeNames");

    if (entitiesP == NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "exclusive Registration requires every 'information[]' element to carry 'entities[]' "
              "with a specific entity id (§ 9.3.3)");
      return false;
    }

    for (KjNode* entP = entitiesP->value.firstChildP; entP != NULL; entP = entP->next)
    {
      KjNode* idP    = kjLookup(entP, "id");
      KjNode* idPatP = kjLookup(entP, LD_VOCAB_ID_PATTERN);

      if (idP == NULL || idPatP != NULL)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "exclusive Registration 'entities[]' entry must specify a specific 'id' "
                "(idPattern or type-only group selectors are not supported, § 9.3.3)");
        return false;
      }
    }

    if (attrNamesP == NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "exclusive Registration 'information[]' element must declare 'attributeNames' — "
              "entity-wide exclusive claims are not supported (§ 9.3.3)");
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// checkInformationArray - validate the "information" array (§ 5.2.10)
//
// Each element must have at least one of: entities, attributeNames. The
// deprecated propertyNames / relationshipNames are still accepted (and later
// folded into attributeNames by regInfoMergeAttributeNames), but mixing them
// with attributeNames in the same element is an error. Empty sub-arrays are
// not allowed.
//
static bool checkInformationArray(KjNode* infoArrayP)
{
  ARRAY_CHECK(infoArrayP, "Invalid Registration", "'information' must be a JSON array");
  EMPTY_ARRAY_CHECK(infoArrayP, "'information' must have at least one element");

  for (KjNode* infoP = infoArrayP->value.firstChildP; infoP != NULL; infoP = infoP->next)
  {
    OBJECT_CHECK(infoP, "Invalid Registration", "'information' items must be JSON objects");

    KjNode* entitiesP  = NULL;
    KjNode* attrNamesP = NULL;
    KjNode* propsP     = NULL;
    KjNode* relsP      = NULL;

    for (KjNode* fP = infoP->value.firstChildP; fP != NULL; fP = fP->next)
    {
      if      (strcmp(fP->name, LD_VOCAB_ENTITIES)   == 0)  entitiesP  = fP;
      else if (strcmp(fP->name, "attributeNames")    == 0)  attrNamesP = fP;
      else if (strcmp(fP->name, "propertyNames")     == 0)  propsP     = fP;
      else if (strcmp(fP->name, "relationshipNames") == 0)  relsP      = fP;
      else
      {
        // RegistrationInfo (§ 5.2.10) is a closed set.
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "'information[]' element has unknown member '%s' "
                "(allowed: entities, attributeNames, propertyNames, relationshipNames)", fP->name);
        return false;
      }
    }

    if (attrNamesP != NULL && (propsP != NULL || relsP != NULL))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'information' element must use 'attributeNames' OR the deprecated "
              "'propertyNames'/'relationshipNames' pair, not both");
      return false;
    }

    if (entitiesP == NULL && attrNamesP == NULL && propsP == NULL && relsP == NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'information' element must have at least one of 'entities', 'attributeNames'");
      return false;
    }

    if (entitiesP != NULL)
    {
      ARRAY_CHECK(entitiesP, "Invalid Registration", "'information[].entities' must be an array");
      EMPTY_ARRAY_CHECK(entitiesP, "'information[].entities' must not be empty");

      for (KjNode* entP = entitiesP->value.firstChildP; entP != NULL; entP = entP->next)
      {
        if (checkEntityInfo(entP) == false)
          return false;
      }
    }

    if (attrNamesP != NULL && checkStringArrayNonEmpty(attrNamesP, "attributeNames")    == false)  return false;
    if (propsP     != NULL && checkStringArrayNonEmpty(propsP,     "propertyNames")     == false)  return false;
    if (relsP      != NULL && checkStringArrayNonEmpty(relsP,      "relationshipNames") == false)  return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// regInfoMergeAttributeNames - fold the deprecated propertyNames /
// relationshipNames into a single attributeNames per information[] element
//
// The property/relationship partition carries no information the broker uses
// (matching + forwarding treat the two identically), so on input the old pair
// is merged into one attributeNames array — the field the broker stores and
// renders. checkInformationArray has already rejected mixing the two forms and
// validated each as a non-empty string array, so at most one form is present
// here and both are well-formed.
//
static void regInfoMergeAttributeNames(KjNode* infoArrayP)
{
  for (KjNode* infoP = infoArrayP->value.firstChildP; infoP != NULL; infoP = infoP->next)
  {
    if (infoP->type != KjObject)
      continue;

    KjNode* propsP = kjLookup(infoP, "propertyNames");
    KjNode* relsP  = kjLookup(infoP, "relationshipNames");

    if (propsP == NULL && relsP == NULL)
      continue;   // already 'attributeNames' (or no attribute restriction at all)

    KjNode* mergedP = (propsP != NULL) ? propsP : relsP;
    mergedP->name = (char*) "attributeNames";

    // both forms present — append every relationshipNames item to attributeNames
    if (propsP != NULL && relsP != NULL)
    {
      KjNode* itemP = relsP->value.firstChildP;
      while (itemP != NULL)
      {
        KjNode* nextP = itemP->next;
        kjChildRemove(relsP, itemP);
        kjChildAdd(mergedP, itemP);
        itemP = nextP;
      }
      kjChildRemove(infoP, relsP);
    }
  }
}



// -----------------------------------------------------------------------------
//
// checkContextSourceInfo - validate optional KeyValuePair[] (§ 5.2.22 / § 4.3.6.6)
//
// Each entry must be {"key": "<string>", "value": "<string>"}. The well-known
// keys (accept, contentType, jsonldContext, ngsildConformance) have specific
// allowed values per § 4.3.6.6.
//
static bool checkContextSourceInfo(KjNode* arrP)
{
  ARRAY_CHECK(arrP, "Invalid Registration", "'contextSourceInfo' must be an array");

  for (KjNode* kvP = arrP->value.firstChildP; kvP != NULL; kvP = kvP->next)
  {
    OBJECT_CHECK(kvP, "Invalid Registration", "'contextSourceInfo' items must be {key,value} objects");

    KjNode* keyP   = NULL;
    KjNode* valueP = NULL;

    for (KjNode* fP = kvP->value.firstChildP; fP != NULL; fP = fP->next)
    {
      if      (strcmp(fP->name, "key")   == 0)  keyP   = fP;
      else if (strcmp(fP->name, "value") == 0)  valueP = fP;
      else
      {
        // KeyValuePair (§ 5.2.22) is a closed set: key, value.
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "'contextSourceInfo' item has unknown member '%s' (allowed: key, value)", fP->name);
        return false;
      }
    }

    MANDATORY_CHECK(keyP,   "Invalid Registration", "'contextSourceInfo' item missing 'key'");
    MANDATORY_CHECK(valueP, "Invalid Registration", "'contextSourceInfo' item missing 'value'");

    STRING_CHECK(keyP,   "Invalid Registration", "'contextSourceInfo.key' must be a string");
    STRING_CHECK(valueP, "Invalid Registration", "'contextSourceInfo.value' must be a string");

    // Well-known keys — § 4.3.6.6
    if (strcmp(keyP->value.s, "accept") == 0 || strcmp(keyP->value.s, "contentType") == 0)
    {
      if (strcmp(valueP->value.s, "application/json") != 0 &&
          strcmp(valueP->value.s, "application/ld+json") != 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "contextSourceInfo '%s' must be 'application/json' or 'application/ld+json'",
                keyP->value.s);
        return false;
      }
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// isIsoDuration - shape-check an ISO 8601 duration string
//
// Accepted shape:  P[nY][nM][nW][nD][T[nH][nM][nS]]
// At least one designator after P (or after T if no date part) is required.
// We don't enforce ordering of date components beyond P-prefix and T-split,
// nor numeric ranges — that's the renderer's job.
//
static bool isIsoDuration(const char* s)
{
  if (s == NULL || s[0] != 'P')
    return false;

  const char* p             = s + 1;
  bool        sawComponent  = false;
  bool        inTime        = false;

  while (*p != 0)
  {
    if (*p == 'T')
    {
      // 'T' must appear at most once and split date / time parts
      if (inTime) return false;
      inTime = true;
      p++;
      // 'T' alone (no time components after) is invalid
      if (*p == 0) return false;
      continue;
    }

    // Numeric run
    if (*p < '0' || *p > '9')
      return false;
    while (*p >= '0' && *p <= '9')
      p++;

    // Optional fraction (S only — but we don't enforce that, just shape)
    if (*p == '.' || *p == ',')
    {
      p++;
      if (*p < '0' || *p > '9') return false;
      while (*p >= '0' && *p <= '9')
        p++;
    }

    // Designator letter
    if (inTime)
    {
      if (*p != 'H' && *p != 'M' && *p != 'S') return false;
    }
    else
    {
      if (*p != 'Y' && *p != 'M' && *p != 'W' && *p != 'D') return false;
    }
    p++;
    sawComponent = true;
  }

  return sawComponent;
}



// -----------------------------------------------------------------------------
//
// checkTimeInterval - validate an observationInterval / managementInterval
//
// TimeInterval shape (§ 5.2.11):
//   { "startAt": <DateTime>, "endAt": <DateTime>? }
// startAt is mandatory; endAt is optional. When both present, startAt < endAt.
//
// 'complete' is false on an update FRAGMENT: a PATCH may touch endAt (or startAt)
// alone, the other member being supplied by the stored value, so startAt-presence
// and the ordering invariant are only enforced on a COMPLETE document (create or
// the post-merge re-validation). The fragment stage just well-forms the members
// actually provided.
//
static bool checkTimeInterval(KjNode* tiP, const char* fieldName, bool complete)
{
  if (tiP->type != KjObject)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
            "'%s' must be a JSON object", fieldName);
    return false;
  }

  KjNode* startP = NULL;
  KjNode* endP   = NULL;
  for (KjNode* fP = tiP->value.firstChildP; fP != NULL; fP = fP->next)
  {
    if      (strcmp(fP->name, "startAt") == 0) startP = fP;
    else if (strcmp(fP->name, "endAt")   == 0) endP   = fP;
  }

  if (complete && startP == NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
            "'%s.startAt' is mandatory", fieldName);
    return false;
  }

  if (startP != NULL)
  {
    if (startP->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'%s.startAt' must be a DateTime string", fieldName);
      return false;
    }
    if (!ldCheckDateTime(startP->value.s, NULL))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'%s.startAt' is not a valid ISO 8601 DateTime", fieldName);
      return false;
    }
  }

  if (endP != NULL)
  {
    if (endP->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'%s.endAt' must be a DateTime string", fieldName);
      return false;
    }
    if (!ldCheckDateTime(endP->value.s, NULL))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'%s.endAt' is not a valid ISO 8601 DateTime", fieldName);
      return false;
    }
    if (startP != NULL && ldIsoToNanoseconds(endP->value.s) <= ldIsoToNanoseconds(startP->value.s))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'%s.endAt' must be after '%s.startAt'", fieldName, fieldName);
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// checkScope - validate scope: string or array of strings
//
static bool checkScope(KjNode* scopeP)
{
  if (scopeP->type == KjString)
  {
    if (scopeP->value.s[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration", "'scope' must not be empty");
      return false;
    }
    return true;
  }
  if (scopeP->type == KjArray)
  {
    EMPTY_ARRAY_CHECK(scopeP, "'scope' array must not be empty");
    for (KjNode* sP = scopeP->value.firstChildP; sP != NULL; sP = sP->next)
    {
      if (sP->type != KjString || sP->value.s[0] == 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "'scope' array items must be non-empty strings");
        return false;
      }
    }
    return true;
  }
  ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
          "'scope' must be a string or array of strings");
  return false;
}



// -----------------------------------------------------------------------------
//
// checkManagement - validate the management sub-object (RegistrationManagementInfo)
//
// Optional members: cacheDuration (ISO 8601 duration), timeout (positive
// number), cooldown (positive number), localOnly (boolean).
//
static bool checkManagement(KjNode* mgmtP)
{
  if (mgmtP->type != KjObject)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
            "'management' must be a JSON object");
    return false;
  }

  for (KjNode* fP = mgmtP->value.firstChildP; fP != NULL; fP = fP->next)
  {
    if (strcmp(fP->name, "cacheDuration") == 0)
    {
      if (fP->type != KjString || !isIsoDuration(fP->value.s))
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "'management.cacheDuration' must be an ISO 8601 duration string");
        return false;
      }
    }
    else if (strcmp(fP->name, "timeout") == 0 || strcmp(fP->name, "cooldown") == 0)
    {
      double v = 0;
      if      (fP->type == KjInt)   v = (double) fP->value.i;
      else if (fP->type == KjFloat) v = fP->value.f;
      else
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "'management.%s' must be a positive number", fP->name);
        return false;
      }
      if (v <= 0)
      {
        // § 5.2.6.6.6 — timeout / cooldown must be "Greater than 0".
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "'management.%s' must be greater than 0", fP->name);
        return false;
      }
    }
    else if (strcmp(fP->name, "localOnly") == 0)
    {
      if (fP->type != KjBoolean)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "'management.localOnly' must be a boolean");
        return false;
      }
    }
    else
    {
      // RegistrationManagementInfo (§ 5.2.34) is a closed set.
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'management' has unknown member '%s' (allowed: cacheDuration, timeout, cooldown, localOnly)", fP->name);
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// checkDatasetIdArray - validate top-level datasetId (§ 5.2.9)
//
// Array of valid URIs; the literal "@none" is also accepted to denote the
// default Attribute instance (§ 4.5.5).
//
static bool checkDatasetIdArray(KjNode* dsP)
{
  ARRAY_CHECK(dsP, "Invalid Registration", "'datasetId' must be a JSON array");
  EMPTY_ARRAY_CHECK(dsP, "'datasetId' array must not be empty");

  for (KjNode* sP = dsP->value.firstChildP; sP != NULL; sP = sP->next)
  {
    STRING_CHECK(sP, "Invalid Registration", "'datasetId' items must be strings");

    if (strcmp(sP->value.s, "@none") == 0)
      continue;

    if (ldCheckUri(sP->value.s) == false)
      return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// isAuxiliaryAllowedOp - subset allowed for auxiliary mode (§ 5.9.2)
//
// auxiliary registrations are retrieve-only and may only declare:
//   - "retrieveOps" group, or
//   - the individual ops it expands to: retrieveEntity, queryEntity
//
static bool isAuxiliaryAllowedOp(const char* name)
{
  return (strcmp(name, "retrieveOps")    == 0 ||
          strcmp(name, "retrieveEntity") == 0 ||
          strcmp(name, "queryEntity")    == 0);
}



// -----------------------------------------------------------------------------
//
// isKnownRegistrationOp - any individual op or op-group named in § 4.20
//
// Tables 4.20-1 and 4.20-2 define the closed set of names that may appear
// in a CSourceRegistration's `operations` array. Names not in either table
// (incl. typos like "createEntityz" or batch sub-variants like "deleteAttr")
// shall be rejected.
//
// Some ops in Table 4.20-1 are not yet implemented in this broker but the
// validator still accepts them — declaring them in a registration is fine,
// even if a forwarded request to one of them will today fall through to
// the regular not-implemented path.
//
static bool isKnownRegistrationOp(const char* name)
{
  // Individual ops + groups already mapped in ldTypes.
  if (ldOpFromName(name)         != LdOpNone) return true;
  if (ldOpGroupMaskFromName(name) != 0)        return true;

  // Spec § 4.20 names not (yet) modelled as LdOp enum values.
  static const char* extra[] = {
    // EntityMap (§ 5.7.x)
    "retrieveEntityMap", "updateEntityMap", "deleteEntityMap", "createEntityMapQueryEntity",
    // Source identity (§ 5.11.x)
    "retrieveContextSourceIdentity",
    NULL
  };
  for (int i = 0; extra[i] != NULL; i++)
  {
    if (strcmp(name, extra[i]) == 0)
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// checkOperations - validate `operations` (§ 5.2.9 / § 4.20)
//
// For auxiliary mode the subset is restricted (retrieve-only). For other
// modes every item must be a name listed in Table 4.20-1 or Table 4.20-2.
//
static bool checkOperations(KjNode* opsP, const char* modeStr)
{
  ARRAY_CHECK(opsP, "Invalid Registration", "'operations' must be an array of strings");
  EMPTY_ARRAY_CHECK(opsP, "'operations' array must not be empty");

  bool isAux = (strcmp(modeStr, "auxiliary") == 0);

  for (KjNode* sP = opsP->value.firstChildP; sP != NULL; sP = sP->next)
  {
    STRING_CHECK(sP, "Invalid Registration", "'operations' items must be strings");

    if (isAux)
    {
      if (!isAuxiliaryAllowedOp(sP->value.s))
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "auxiliary registration 'operations' must be one of: retrieveOps, retrieveEntity, queryEntity (got '%s')",
                sP->value.s);
        return false;
      }
    }
    else
    {
      if (!isKnownRegistrationOp(sP->value.s))
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "'operations' contains unknown operation or operation-group name '%s' (see § 4.20)",
                sP->value.s);
        return false;
      }
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldCheckRegistration -
//
bool ldCheckRegistration(KjNode* regP, LdOp op, bool merged, KAlloc* faP)
{
  (void) faP;  // reserved for future use (e.g. allocating expanded names)

  OBJECT_CHECK(regP, "Invalid Registration", "Registration payload must be a JSON object");

  KLOG_T(LdTCheckReg, "Checking registration payload for op %s", ldOpToString(op));

  // § 5.2.6.5.3 — status, lastFailure, lastSuccess, timesFailed and timesSent are
  // server-owned read-only members; a create "shall ignore" them. Strip them on
  // the create path so a client-supplied value is neither persisted nor echoed
  // (a round-tripped GET result re-POSTed is the typical source). The merged
  // re-validation legitimately carries the stored values, so leave them there.
  if (op == LdOpCreateRegistration && !merged)
  {
    static const char* readOnly[] = { "status", "lastFailure", "lastSuccess", "timesFailed", "timesSent", "createdAt", "modifiedAt", NULL };
    for (int i = 0; readOnly[i] != NULL; i++)
    {
      KjNode* roP = kjLookup(regP, readOnly[i]);
      if (roP != NULL)
        kjChildRemove(regP, roP);
    }
  }

  KjNode* typeP                = NULL;
  KjNode* infoP                = NULL;
  KjNode* endpointP            = NULL;
  KjNode* modeP                = NULL;
  KjNode* expiresAtP           = NULL;
  KjNode* operationsP          = NULL;
  KjNode* contextSrcInfoP      = NULL;
  KjNode* descriptionP         = NULL;
  KjNode* registrationNameP    = NULL;
  KjNode* csourceAliasP        = NULL;
  KjNode* tenantP              = NULL;
  KjNode* scopeP               = NULL;
  KjNode* refreshRateP         = NULL;
  KjNode* observationIntervalP = NULL;
  KjNode* managementIntervalP  = NULL;
  KjNode* managementP          = NULL;
  KjNode* datasetIdP           = NULL;
  KjNode* locationP            = NULL;
  KjNode* observationSpaceP    = NULL;
  KjNode* operationSpaceP      = NULL;

  for (KjNode* childP = regP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    //
    // Delete-markers (TS 104-175 clause-8 / § 5.9.3). On the update path the
    // "urn:ngsi-ld:null" sentinel string means "delete this member": convert it
    // to an internal KjNull (which the DB plugin turns into a $unset) and skip
    // the per-field type validation below — the marker node persists in the
    // fragment for the plugin. Raw JSON null is never a valid value (JSON-LD
    // drops nulls). Mandatory members (information, endpoint) can't be deleted.
    // On create the sentinel is not allowed as a first-level value.
    //
    if (op == LdOpUpdateRegistration)
    {
      if (childP->type == KjNull)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
                "JSON null is not allowed in NGSI-LD; use the 'urn:ngsi-ld:null' delete-marker (field: '%s')", childP->name);
        return false;
      }
      if (childP->type == KjString && strcmp(childP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
      {
        if (strcmp(childP->name, LD_VOCAB_INFORMATION) == 0 || strcmp(childP->name, LD_VOCAB_ENDPOINT) == 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                  "'%s' is mandatory and cannot be deleted", childP->name);
          return false;
        }
        childP->type = KjNull;   // internal delete signal honoured by the DB plugin ($unset)
        continue;
      }
    }
    else if (childP->type == KjString && strcmp(childP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
              "'urn:ngsi-ld:null' is not allowed as a first-level value in Create Registration (field: '%s')", childP->name);
      return false;
    }

    if      (strcmp(childP->name, "type")                 == 0)  typeP                = childP;
    else if (strcmp(childP->name, LD_VOCAB_INFORMATION)   == 0)  infoP                = childP;
    else if (strcmp(childP->name, LD_VOCAB_ENDPOINT)      == 0)  endpointP            = childP;
    else if (strcmp(childP->name, LD_VOCAB_MODE)          == 0)  modeP                = childP;
    else if (strcmp(childP->name, LD_VOCAB_EXPIRES_AT)    == 0)  expiresAtP           = childP;
    else if (strcmp(childP->name, "operations")           == 0)  operationsP          = childP;
    else if (strcmp(childP->name, "contextSourceInfo")    == 0)  contextSrcInfoP      = childP;
    else if (strcmp(childP->name, "description")          == 0)  descriptionP         = childP;
    else if (strcmp(childP->name, "registrationName")     == 0)  registrationNameP    = childP;
    else if (strcmp(childP->name, "contextSourceAlias")   == 0)  csourceAliasP        = childP;
    else if (strcmp(childP->name, "tenant")               == 0)  tenantP              = childP;
    else if (strcmp(childP->name, LD_VOCAB_SCOPE)         == 0)  scopeP               = childP;
    else if (strcmp(childP->name, "refreshRate")          == 0)  refreshRateP         = childP;
    else if (strcmp(childP->name, "observationInterval")  == 0)  observationIntervalP = childP;
    else if (strcmp(childP->name, "managementInterval")   == 0)  managementIntervalP  = childP;
    else if (strcmp(childP->name, "management")           == 0)  managementP          = childP;
    else if (strcmp(childP->name, LD_VOCAB_DATASET_ID)    == 0)  datasetIdP           = childP;
    else if (strcmp(childP->name, "location")             == 0)  locationP            = childP;
    else if (strcmp(childP->name, "observationSpace")     == 0)  observationSpaceP    = childP;
    else if (strcmp(childP->name, "operationSpace")       == 0)  operationSpaceP      = childP;
  }

  // `type` is validated AND stripped by ldParseHook for /csourceRegistrations —
  // see corNgsild.fixedTypeRecord. Nothing to check here.
  (void) typeP;

  // information — required for create, validated when present
  if (op == LdOpCreateRegistration)
    MANDATORY_CHECK(infoP, "Missing Information", "Registration 'information' is mandatory for create");

  if (infoP != NULL && checkInformationArray(infoP) == false)
    return false;

  // Fold the deprecated propertyNames / relationshipNames into a single
  // attributeNames BEFORE storage, cache build and the exclusive-mode check —
  // so the DB doc, the reg cache and GET all see one type-agnostic list.
  if (infoP != NULL)
    regInfoMergeAttributeNames(infoP);

  // endpoint — required for create, must be a URI when present
  if (op == LdOpCreateRegistration)
    MANDATORY_CHECK(endpointP, "Missing Endpoint", "Registration 'endpoint' is mandatory for create");

  if (endpointP != NULL)
  {
    STRING_CHECK(endpointP, "Invalid Registration", "Registration 'endpoint' must be a URI string");
    URI_CHECK(endpointP->value.s);
  }

  // mode — must be one of the four allowed values
  const char* modeStr = "inclusive";
  if (modeP != NULL)
  {
    STRING_CHECK(modeP, "Invalid Registration", "Registration 'mode' must be a string");
    modeStr = modeP->value.s;

    if (strcmp(modeStr, "inclusive") != 0 &&
        strcmp(modeStr, "exclusive") != 0 &&
        strcmp(modeStr, "redirect")  != 0 &&
        strcmp(modeStr, "auxiliary") != 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Mode",
              "Registration 'mode' must be one of: inclusive, exclusive, redirect, auxiliary (got '%s')", modeStr);
      return false;
    }
  }

  // § 9.3.3 — exclusive mode has stricter structural requirements than the
  // generic information-array shape: every information[] element must carry
  // a specific entity id AND at least one attribute name.
  if (infoP != NULL && strcmp(modeStr, "exclusive") == 0 && checkExclusiveStructure(infoP) == false)
    return false;

  // expiresAt — DateTime + must be in the future (§ 5.9.2)
  if (expiresAtP != NULL)
  {
    STRING_CHECK(expiresAtP, "Invalid Registration", "'expiresAt' must be a DateTime string");
    DATETIME_CHECK(expiresAtP->value.s, "'expiresAt' is not a valid ISO 8601 DateTime");

    // The future-check applies to a value being SET (create, or a fragment that
    // sets expiresAt). On the post-merge re-validation ('merged') a stored
    // expiresAt that has since elapsed rides along in the merged document and
    // must NOT be re-rejected — a registration has no 'expired' status and is
    // not auto-removed, so an unrelated PATCH of a TTL-elapsed reg must still go.
    uint64_t expiresNs = ldIsoToNanoseconds(expiresAtP->value.s);
    if (!merged && expiresNs > 0 && expiresNs < corRest.requestStartTime)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'expiresAt' must be a DateTime in the future");
      return false;
    }
  }

  // contextSourceInfo — optional array
  if (contextSrcInfoP != NULL && checkContextSourceInfo(contextSrcInfoP) == false)
    return false;

  // operations — § 5.2.9 / § 4.20. Auxiliary mode (§ 5.9.2) further
  // restricts to retrieve-only; other modes accept any spec-named op or group.
  if (operationsP != NULL && checkOperations(operationsP, modeStr) == false)
    return false;

  // Optional descriptive strings — non-empty when present.
  #define NONEMPTY_STRING(field, label)                                                       \
    do {                                                                                       \
      if (field != NULL)                                                                       \
      {                                                                                        \
        STRING_CHECK(field, "Invalid Registration", "'" label "' must be a string");           \
        if (field->value.s[0] == 0)                                                            \
        {                                                                                      \
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",                      \
                  "'" label "' must not be empty");                                            \
          return false;                                                                        \
        }                                                                                      \
      }                                                                                        \
    } while (0)

  NONEMPTY_STRING(descriptionP,      "description");
  NONEMPTY_STRING(registrationNameP, "registrationName");
  NONEMPTY_STRING(csourceAliasP,     "contextSourceAlias");
  NONEMPTY_STRING(tenantP,           "tenant");

  #undef NONEMPTY_STRING

  // scope — string or string[] (§ 4.19)
  if (scopeP != NULL && checkScope(scopeP) == false)
    return false;

  // refreshRate — ISO 8601 duration string
  if (refreshRateP != NULL)
  {
    STRING_CHECK(refreshRateP, "Invalid Registration", "'refreshRate' must be a string");
    if (!isIsoDuration(refreshRateP->value.s))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'refreshRate' must be an ISO 8601 duration string (e.g. PT5M, PT1H30M, P1D)");
      return false;
    }
  }

  // observationInterval / managementInterval — TimeInterval objects (§ 5.2.11)
  if (observationIntervalP != NULL && checkTimeInterval(observationIntervalP, "observationInterval", op == LdOpCreateRegistration) == false)
    return false;
  if (managementIntervalP != NULL && checkTimeInterval(managementIntervalP, "managementInterval", op == LdOpCreateRegistration) == false)
    return false;

  // management — RegistrationManagementInfo (§ 5.2.34)
  if (managementP != NULL && checkManagement(managementP) == false)
    return false;

  // datasetId — array of valid URIs (or "@none") (§ 5.2.9 / § 4.5.5)
  if (datasetIdP != NULL && checkDatasetIdArray(datasetIdP) == false)
    return false;

  // location / observationSpace / operationSpace — GeoJSON geometry (§ 5.2.9 / § 4.7).
  // ldCheckGeo enforces type ∈ Point/LineString/Polygon/Multi*, coordinate
  // shape per type, lat/lon ranges, and polygon-ring closure.
  if (locationP         != NULL && ldCheckGeo(locationP)         == false)  return false;
  if (observationSpaceP != NULL && ldCheckGeo(observationSpaceP) == false)  return false;
  if (operationSpaceP   != NULL && ldCheckGeo(operationSpaceP)   == false)  return false;

  KLOG_T(LdTCheckReg, "Registration payload valid");
  return true;
}
