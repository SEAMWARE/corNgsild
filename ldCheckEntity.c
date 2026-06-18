//
// FILE            ldCheckEntity.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kalloc/KAlloc.h"                             // KAlloc

#include "swRest/swRest.h"                              // swRest
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjLookup.h"                             // kjLookup
#include "kjson/kjBuilder.h"                            // kjChildRemove

#include "swNgsild/LdOp.h"                               // LdOp
#include "swNgsild/LdCheck.h"                            // OBJECT_CHECK, STRING_CHECK, ...
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/ldTypes.h"                            // ldOpToString
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "swNgsild/ldCheckAttribute.h"                   // ldCheckAttribute
#include "swNgsild/ldIsEntityKeyword.h"                   // ldIsEntityKeyword
#include "swNgsild/ldCheckEntity.h"                      // Own interface
#include "swNgsild/ldTraceLevels.h"                      // LdTCheckEnt



// -----------------------------------------------------------------------------
//
// isCreateOp - check if the operation is a create-type operation
//
static bool isCreateOp(LdOp op)
{
  if (op == LdOpCreateEntity)  return true;
  if (op == LdOpBatchCreate)   return true;
  if (op == LdOpBatchUpsert)   return true;
  if (op == LdOpReplaceEntity) return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// isUpdateOp - ops where the URL carries the entity id and body must not
//
static bool isUpdateOp(LdOp op)
{
  if (op == LdOpUpdateEntity) return true;
  if (op == LdOpAppendAttrs)  return true;
  if (op == LdOpMergeEntity)  return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// findAttrTypeInDb - find an attribute's type from the DB entity
//
static LdAttrType findAttrTypeInDb(KjNode* dbEntityP, const char* attrName)
{
  if (dbEntityP == NULL)
    return LdAttrNone;

  for (KjNode* attrP = dbEntityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (strcmp(attrP->name, attrName) == 0)
      return ldAttrTypeDetect(attrP);
  }

  return LdAttrNone;
}




// -----------------------------------------------------------------------------
//
// hasDuplicateSibling - check if a node has a sibling with the same name (before it)
//
static bool hasDuplicateSibling(KjNode* container, KjNode* nodeP)
{
  for (KjNode* p = container->value.firstChildP; p != nodeP; p = p->next)
  {
    if (strcmp(p->name, nodeP->name) == 0)
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// ldCheckEntity -
//
bool ldCheckEntity(KjNode* entityP, LdOp op, KjNode* dbEntityP, KAlloc* faP)
{
  OBJECT_CHECK_IR(entityP, "Invalid Entity", "Entity payload must be a JSON object");

  KLOG_T(LdTCheckEnt, "Checking entity payload for op %s", ldOpToString(op));

  // Silently remove system-managed timestamps if present in payload
  KjNode* rmP;
  if ((rmP = kjLookup(entityP, LD_VOCAB_CREATED_AT))  != NULL)  kjChildRemove(entityP, rmP);
  if ((rmP = kjLookup(entityP, LD_VOCAB_MODIFIED_AT)) != NULL)  kjChildRemove(entityP, rmP);

  KjNode*  idNodeP    = NULL;
  KjNode*  typeNodeP  = NULL;
  bool     hasId      = false;
  bool     hasAtId    = false;
  bool     hasType    = false;
  bool     hasAtType  = false;

  // First pass: find id and type, check for duplicates, null values, and attribute names
  for (KjNode* childP = entityP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    // JSON null is not allowed in NGSI-LD (JSON-LD drops null values)
    if (childP->type == KjNull)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value", "JSON null is not allowed in NGSI-LD (field: '%s')", childP->name);
      return false;
    }

    // urn:ngsi-ld:null is not allowed as a first-level value in Create Entity
    if (isCreateOp(op) && childP->type == KjString && strcmp(childP->value.s, "urn:ngsi-ld:null") == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value", "'urn:ngsi-ld:null' is not allowed as a first-level value in Create Entity (field: '%s')", childP->name);
      return false;
    }

    if (strcmp(childP->name, "id") == 0)
    {
      DUPLICATE_FLAG_CHECK(hasId, hasAtId, "Duplicate Id", "Duplicate 'id' / '@id' in entity");
      hasId   = true;
      idNodeP = childP;
    }
    else if (strcmp(childP->name, "@id") == 0)
    {
      DUPLICATE_FLAG_CHECK(hasId, hasAtId, "Duplicate Id", "Duplicate 'id' / '@id' in entity");
      hasAtId = true;
      childP->name = (char*) "id";    // normalize the JSON-LD keyword to the short NGSI-LD form
      idNodeP = childP;
    }
    else if (strcmp(childP->name, "type") == 0)
    {
      DUPLICATE_FLAG_CHECK(hasType, hasAtType, "Duplicate Type", "Duplicate 'type' / '@type' in entity");
      hasType    = true;
      typeNodeP  = childP;
    }
    else if (strcmp(childP->name, "@type") == 0)
    {
      DUPLICATE_FLAG_CHECK(hasType, hasAtType, "Duplicate Type", "Duplicate 'type' / '@type' in entity");
      hasAtType  = true;
      childP->name = (char*) "type";  // normalize the JSON-LD keyword to the short NGSI-LD form
      typeNodeP  = childP;
    }
  }

  // Check mandatory fields for create operations
  if (isCreateOp(op) == true)
  {
    if (idNodeP == NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Missing Entity Id", "Entity 'id' is mandatory for %s", ldOpToString(op));
      return false;
    }

    if (typeNodeP == NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Missing Entity Type", "Entity 'type' is mandatory for %s", ldOpToString(op));
      return false;
    }
  }

  // Validate "id" if present
  if (idNodeP != NULL)
  {
    STRING_CHECK(idNodeP, "Invalid Entity Id", "Entity 'id' must be a string");
    URI_CHECK(idNodeP->value.s);

    // Update ops take the entity id from the URL. The body MAY repeat the
    // id (the ETSI test suite and some clients do), but if it doesn't
    // match the URL id the request is ambiguous about which entity to
    // mutate — reject only that case.
    if (isUpdateOp(op) && swRest.in.wildcard[0] != NULL &&
        strcmp(idNodeP->value.s, swRest.in.wildcard[0]) != 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Entity Id Mismatch",
              "Entity 'id' in body ('%s') does not match URL ('%s')",
              idNodeP->value.s, swRest.in.wildcard[0]);
      return false;
    }
  }

  // Validate "type" if present — string or array of non-empty strings.
  // Any value beginning with '@' is a JSON-LD keyword (`@type`, `@id`,
  // `@vocab`, …) and is never a valid entity type per § 4.6.2; reject
  // those explicitly. Test ETSI 001_02_04 sends `"type": "type"` which
  // the core context's `"type": "@type"` term mapping expands into the
  // bare keyword — without this guard the broker happily stores the
  // entity with `@type` as its type.
  if (typeNodeP != NULL)
  {
    if (typeNodeP->type == KjString)
    {
      if (typeNodeP->value.s[0] == '@')
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Entity Type",
                "Entity 'type' must not be a JSON-LD keyword ('%s')", typeNodeP->value.s);
        return false;
      }
    }
    else if (typeNodeP->type == KjArray)
    {
      if (typeNodeP->value.firstChildP == NULL)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Entity Type", "Entity 'type' array must not be empty");
        return false;
      }

      for (KjNode* elemP = typeNodeP->value.firstChildP; elemP != NULL; elemP = elemP->next)
      {
        if (elemP->type != KjString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Entity Type", "Entity 'type' array elements must be strings");
          return false;
        }

        if (elemP->value.s[0] == 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Entity Type", "Entity 'type' array elements must not be empty strings");
          return false;
        }

        if (elemP->value.s[0] == '@')
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Entity Type",
                  "Entity 'type' must not be a JSON-LD keyword ('%s')", elemP->value.s);
          return false;
        }
      }
    }
    else
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Entity Type", "Entity 'type' must be a string or array of strings");
      return false;
    }
  }

  // Validate scope if present
  for (KjNode* childP = entityP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, LD_VOCAB_SCOPE) == 0)
    {
      if (childP->type == KjString)
      {
        if (childP->value.s[0] == 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Scope", "Entity 'scope' must not be an empty string");
          return false;
        }
      }
      else if (childP->type == KjArray)
      {
        if (childP->value.firstChildP == NULL)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Scope", "Entity 'scope' array must not be empty");
          return false;
        }

        for (KjNode* elemP = childP->value.firstChildP; elemP != NULL; elemP = elemP->next)
        {
          if (elemP->type != KjString)
          {
            ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Scope", "Entity 'scope' array elements must be strings");
            return false;
          }

          if (elemP->value.s[0] == 0)
          {
            ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Scope", "Entity 'scope' array elements must not be empty strings");
            return false;
          }
        }
      }
      else
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Scope", "Entity 'scope' must be a string or array of strings");
        return false;
      }
    }
  }

  // Second pass: validate each attribute
  for (KjNode* childP = entityP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (ldIsEntityKeyword(childP->name) == true)
      continue;

    // Duplicate attribute detection
    if (hasDuplicateSibling(entityP, childP))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Duplicate Field", "Duplicated field in entity: '%s'", childP->name);
      return false;
    }

    LdAttrType dbAttrType = findAttrTypeInDb(dbEntityP, childP->name);

    if (childP->type == KjArray)
    {
      // Multi-attribute: each element must be a valid attribute instance
      if (childP->value.firstChildP == NULL)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Multi-Attribute", "Multi-attribute '%s' must not be an empty array", childP->name);
        return false;
      }

      // Each element must be an object (shape check before dedup).
      for (KjNode* instP = childP->value.firstChildP; instP != NULL; instP = instP->next)
      {
        if (instP->type != KjObject)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Multi-Attribute", "Multi-attribute '%s': each instance must be a JSON object", childP->name);
          return false;
        }
      }

      // § 5.2.5 / § 4.5.5 — a single submitted multi-instance attribute must
      // have at most ONE default instance (no datasetId) and UNIQUE datasetIds.
      // (The § 8.5.3 tiebreaker is for conflicting instances assembled from
      // multiple Context Sources, not a single local write — so a conflicting
      // single payload is rejected, not resolved.)
      bool defaultFound = false;
      for (KjNode* instP = childP->value.firstChildP; instP != NULL; instP = instP->next)
      {
        KjNode* dsP = kjLookup(instP, "datasetId");

        if (dsP == NULL)
        {
          if (defaultFound == true)
          {
            ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Multi-Attribute", "Attribute '%s': more than one instance without a datasetId", childP->name);
            return false;
          }
          defaultFound = true;
        }
        else if (dsP->type == KjString)
        {
          // Duplicate datasetId among earlier instances of the same attribute?
          // (full datasetId URI validation is done by ldCheckAttribute below)
          for (KjNode* otherP = childP->value.firstChildP; otherP != instP; otherP = otherP->next)
          {
            KjNode* otherDsP = kjLookup(otherP, "datasetId");
            if ((otherDsP != NULL) && (otherDsP->type == KjString) && (strcmp(otherDsP->value.s, dsP->value.s) == 0))
            {
              ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Multi-Attribute", "Attribute '%s': duplicate datasetId '%s'", childP->name, dsP->value.s);
              return false;
            }
          }
        }
      }

      // Validate each instance.
      for (KjNode* instP = childP->value.firstChildP; instP != NULL; instP = instP->next)
      {
        instP->name = childP->name;
        if (ldCheckAttribute(instP, op, dbAttrType, faP) == false)
          return false;
      }
    }
    else if (ldCheckAttribute(childP, op, dbAttrType, faP) == false)
      return false;
  }

  KLOG_T(LdTCheckEnt, "Entity payload valid");
  return true;
}
