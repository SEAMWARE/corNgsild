//
// FILE            ldCheckRegistration.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
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

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode

#include "swRest/SwRestState.h"                          // swRest (requestStartTime)

#include "swNgsild/LdOp.h"                               // LdOp
#include "swNgsild/LdCheck.h"                            // OBJECT_CHECK, ARRAY_CHECK, ...
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/ldCheckDateTime.h"                    // ldIsoToNanoseconds
#include "swNgsild/ldTypes.h"                            // ldOpToString
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldCheckRegistration.h"                // Own interface
#include "swNgsild/ldTraceLevels.h"                      // LdTCheckReg



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

  EMPTY_ARRAY_CHECK(arrP, "'information[].propertyNames' / 'relationshipNames' must not be empty");

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
// checkInformationArray - validate the "information" array (§ 5.2.10)
//
// Each element must have at least one of: entities, propertyNames,
// relationshipNames. Empty sub-arrays are not allowed.
//
static bool checkInformationArray(KjNode* infoArrayP)
{
  ARRAY_CHECK(infoArrayP, "Invalid Registration", "'information' must be a JSON array");
  EMPTY_ARRAY_CHECK(infoArrayP, "'information' must have at least one element");

  for (KjNode* infoP = infoArrayP->value.firstChildP; infoP != NULL; infoP = infoP->next)
  {
    OBJECT_CHECK(infoP, "Invalid Registration", "'information' items must be JSON objects");

    KjNode* entitiesP = NULL;
    KjNode* propsP    = NULL;
    KjNode* relsP     = NULL;

    for (KjNode* fP = infoP->value.firstChildP; fP != NULL; fP = fP->next)
    {
      if      (strcmp(fP->name, LD_VOCAB_ENTITIES)   == 0)  entitiesP = fP;
      else if (strcmp(fP->name, "propertyNames")     == 0)  propsP    = fP;
      else if (strcmp(fP->name, "relationshipNames") == 0)  relsP     = fP;
    }

    if (entitiesP == NULL && propsP == NULL && relsP == NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'information' element must have at least one of 'entities', 'propertyNames', 'relationshipNames'");
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

    if (propsP != NULL && checkStringArrayNonEmpty(propsP, "propertyNames")     == false)  return false;
    if (relsP  != NULL && checkStringArrayNonEmpty(relsP,  "relationshipNames") == false)  return false;
  }

  return true;
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
static bool checkTimeInterval(KjNode* tiP, const char* fieldName)
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

  if (startP == NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
            "'%s.startAt' is mandatory", fieldName);
    return false;
  }
  if (startP->type != KjString)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
            "'%s.startAt' must be a DateTime string", fieldName);
    return false;
  }
  if (ldCheckDateTime(startP->value.s) < 0)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
            "'%s.startAt' is not a valid ISO 8601 DateTime", fieldName);
    return false;
  }

  if (endP != NULL)
  {
    if (endP->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'%s.endAt' must be a DateTime string", fieldName);
      return false;
    }
    if (ldCheckDateTime(endP->value.s) < 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'%s.endAt' is not a valid ISO 8601 DateTime", fieldName);
      return false;
    }
    if (ldIsoToNanoseconds(endP->value.s) <= ldIsoToNanoseconds(startP->value.s))
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
      if (v < 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
                "'management.%s' must be non-negative", fP->name);
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
    // Unknown keys passed through silently (forward-compat for spec extensions).
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// auxiliaryOpsAllowed - operations subset for auxiliary mode (§ 5.9.2)
//
// auxiliary registrations may only define operations as one of:
// "retrieveOps", "retrieveEntity", "queryEntity", or a combination thereof.
//
static bool auxiliaryOpsAllowed(KjNode* opsP)
{
  ARRAY_CHECK(opsP, "Invalid Registration", "'operations' must be an array of strings");

  for (KjNode* sP = opsP->value.firstChildP; sP != NULL; sP = sP->next)
  {
    STRING_CHECK(sP, "Invalid Registration", "'operations' items must be strings");

    if (strcmp(sP->value.s, "retrieveOps")    != 0 &&
        strcmp(sP->value.s, "retrieveEntity") != 0 &&
        strcmp(sP->value.s, "queryEntity")    != 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "auxiliary registration 'operations' must be one of: retrieveOps, retrieveEntity, queryEntity (got '%s')",
              sP->value.s);
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldCheckRegistration -
//
bool ldCheckRegistration(KjNode* regP, LdOp op, KAlloc* faP)
{
  (void) faP;  // reserved for future use (e.g. allocating expanded names)

  OBJECT_CHECK(regP, "Invalid Registration", "Registration payload must be a JSON object");

  KLOG_T(LdTCheckReg, "Checking registration payload for op %s", ldOpToString(op));

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

  for (KjNode* childP = regP->value.firstChildP; childP != NULL; childP = childP->next)
  {
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
  }

  // `type` is validated AND stripped by ldParseHook for /csourceRegistrations —
  // see swNgsild.fixedTypeRecord. Nothing to check here.
  (void) typeP;

  // information — required for create, validated when present
  if (op == LdOpCreateRegistration)
    MANDATORY_CHECK(infoP, "Missing Information", "Registration 'information' is mandatory for create");

  if (infoP != NULL && checkInformationArray(infoP) == false)
    return false;

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

  // expiresAt — DateTime + must be in the future (§ 5.9.2)
  if (expiresAtP != NULL)
  {
    STRING_CHECK(expiresAtP, "Invalid Registration", "'expiresAt' must be a DateTime string");
    DATETIME_CHECK(expiresAtP->value.s, "'expiresAt' is not a valid ISO 8601 DateTime");

    uint64_t expiresNs = ldIsoToNanoseconds(expiresAtP->value.s);
    if (expiresNs > 0 && expiresNs < swRest.requestStartTime)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration",
              "'expiresAt' must be a DateTime in the future");
      return false;
    }
  }

  // contextSourceInfo — optional array
  if (contextSrcInfoP != NULL && checkContextSourceInfo(contextSrcInfoP) == false)
    return false;

  // auxiliary mode: operations subset (§ 5.9.2)
  if (strcmp(modeStr, "auxiliary") == 0 && operationsP != NULL)
  {
    if (auxiliaryOpsAllowed(operationsP) == false)
      return false;
  }

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
  if (observationIntervalP != NULL && checkTimeInterval(observationIntervalP, "observationInterval") == false)
    return false;
  if (managementIntervalP != NULL && checkTimeInterval(managementIntervalP, "managementInterval") == false)
    return false;

  // management — RegistrationManagementInfo (§ 5.2.34)
  if (managementP != NULL && checkManagement(managementP) == false)
    return false;

  KLOG_T(LdTCheckReg, "Registration payload valid");
  return true;
}
