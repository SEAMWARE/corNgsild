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
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjLookup.h"                             // kjLookup

#include "swNgsild/LdOp.h"                               // LdOp
#include "swNgsild/LdCheck.h"                            // OBJECT_CHECK, STRING_CHECK, ...
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/ldTypes.h"                            // ldOpToString
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "swNgsild/ldCheckAttribute.h"                   // ldCheckAttribute
#include "swNgsild/ldCheckEntity.h"                      // Own interface
#include "swNgsild/ldTraceLevels.h"                      // LdTCheckEnt



// -----------------------------------------------------------------------------
//
// isEntityKeyword - check if a field name is an entity-level keyword
//
// After expansion, "id" stays as "id" (@id is skipped by expander),
// "type" stays as "type" (@type is skipped), "@context" is removed from tree.
// "scope" is expanded to its full IRI.
//
static bool isEntityKeyword(const char* name)
{
  if (strcmp(name, "id")            == 0)  return true;
  if (strcmp(name, "@id")           == 0)  return true;
  if (strcmp(name, "type")          == 0)  return true;
  if (strcmp(name, "@type")         == 0)  return true;
  if (strcmp(name, "@context")      == 0)  return true;
  if (strcmp(name, LD_VOCAB_SCOPE)  == 0)  return true;

  return false;
}



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
// ldCheckEntity -
//
bool ldCheckEntity(KjNode* entityP, LdOp op, KjNode* dbEntityP, KAlloc* faP)
{
  OBJECT_CHECK(entityP, "Invalid Entity", "Entity payload must be a JSON object");

  KLOG_T(LdTCheckEnt, "Checking entity payload for op %s", ldOpToString(op));

  KjNode*  idNodeP    = NULL;
  KjNode*  typeNodeP  = NULL;
  bool     hasId      = false;
  bool     hasAtId    = false;
  bool     hasType    = false;
  bool     hasAtType  = false;

  // First pass: find id and type, check for duplicates
  for (KjNode* childP = entityP->value.firstChildP; childP != NULL; childP = childP->next)
  {
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
  }

  // Validate "type" if present — string or array of non-empty strings
  if (typeNodeP != NULL)
  {
    if (typeNodeP->type == KjString)
    {
      // single string — OK (existing behavior)
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
    if (isEntityKeyword(childP->name) == true)
      continue;

    LdAttrType dbAttrType = findAttrTypeInDb(dbEntityP, childP->name);

    if (childP->type == KjArray)
    {
      // Multi-attribute: each element must be a valid attribute instance
      if (childP->value.firstChildP == NULL)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Multi-Attribute", "Multi-attribute '%s' must not be an empty array", childP->name);
        return false;
      }

      bool hasDefault = false;

      for (KjNode* instP = childP->value.firstChildP; instP != NULL; instP = instP->next)
      {
        if (instP->type != KjObject)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Multi-Attribute", "Multi-attribute '%s': each instance must be a JSON object", childP->name);
          return false;
        }

        // Check datasetId uniqueness
        KjNode* dsP = kjLookup(instP, LD_VOCAB_DATASET_ID);

        if (dsP == NULL)
        {
          if (hasDefault)
          {
            ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Duplicate Default Instance", "Multi-attribute '%s': only one instance without datasetId allowed", childP->name);
            return false;
          }
          hasDefault = true;
        }
        else
        {
          // Check for duplicate datasetId values among prior instances
          for (KjNode* otherP = childP->value.firstChildP; otherP != instP; otherP = otherP->next)
          {
            KjNode* otherDsP = kjLookup(otherP, LD_VOCAB_DATASET_ID);

            if (otherDsP != NULL && strcmp(dsP->value.s, otherDsP->value.s) == 0)
            {
              ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Duplicate datasetId", "Multi-attribute '%s': duplicate datasetId '%s'", childP->name, dsP->value.s);
              return false;
            }
          }
        }

        // Validate the attribute instance itself
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
