//
// FILE            ldEntityAttrsSet.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//

#include <string.h>                                   // strcmp

#include "corAlloc/CorAlloc.h"                        // CorAlloc
#include "corTree/CorNode.h"                          // CorNode
#include "corTree/corTreeBuilder.h"                   // corTreeObject, corTreeString, corTreeInteger, corTreeChildAdd, corTreeChildRemove
#include "corTree/corTreeLookup.h"                    // corTreeLookup
#include "corTree/corTreeClone.h"                     // corTreeClone
#include "corTree/corTreeFree.h"                      // corTreeFree
#include "corRest/corRest.h"                            // corRest (request-scoped allocator for the report)

#include "corNgsild/LdVocab.h"                         // LD_VOCAB_*
#include "corNgsild/ldEntityAttrsSet.h"                // Own interface
#include "corNgsild/ldIsEntityKeyword.h"               // ldIsNotAttributeName
#include "corNgsild/ldTermId.h"                        // ldTermId, CorTerm*



// -----------------------------------------------------------------------------
//
// removeChild - unlink a node from its container and, when the target lives on
// the malloc heap (allocP == NULL, e.g. the in-memory store), free it. On a
// request arena (allocP != NULL, e.g. mongoc) the arena reclaims it, so freeing
// would be wrong. The node must not be referenced elsewhere — every call site
// here replaces it wholesale (report entries hold separate clones).
//
static void removeChild(CorNode* container, CorNode* node, CorAlloc* allocP)
{
  corTreeChildRemove(container, node);
  if (allocP == NULL)
    corTreeFree(node);
}



// -----------------------------------------------------------------------------
//
// isNgsildNull - true if node is the "urn:ngsi-ld:null" delete-marker string
//
static inline bool isNgsildNull(const CorNode* nodeP)
{
  return (nodeP != NULL &&
          nodeP->type == CorString &&
          strcmp(nodeP->value.s, LD_VOCAB_NGSILD_NULL) == 0);
}



// -----------------------------------------------------------------------------
//
// instanceValueIsNull - true if an instance object's "value" is the null-marker
//
// After ldApiEntityToDbModel, every instance's payload key is "value"
// (Property.value, Relationship.object, LanguageProperty.languageMap, ...
// all normalized).
//
// Two shapes count as null:
//   - bare string  "value": "urn:ngsi-ld:null"   (Property, Relationship,
//                                                 GeoProperty, ...)
//   - LanguageProperty:  "value": { "@none": "urn:ngsi-ld:null" }
//     per § 4.5.5.9 — the languageMap container's @none entry carries the
//     marker; presence of that single entry means the attribute is being
//     deleted.
//
static bool instanceValueIsNull(CorNode* instP)
{
  if (instP == NULL || instP->type != CorObject)
    return false;

  CorNode* valP = corTreeLookup(instP, "value");
  if (valP == NULL)
    return false;

  if (isNgsildNull(valP))
    return true;

  if (valP->type == CorObject)
  {
    CorNode* noneP = corTreeLookup(valP, "@none");
    if (isNgsildNull(noneP))
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// bumpModifiedAt - set modifiedAt on an object (replace in-place or add)
//
static void bumpModifiedAt(CorNode* objP, uint64_t ts, CorAlloc* allocP)
{
  if (objP == NULL || objP->type != CorObject)
    return;

  CorNode* mP = corTreeLookup(objP, LD_VOCAB_MODIFIED_AT);
  if (mP != NULL)
  {
    mP->type    = CorInt;
    mP->value.i = (long long) ts;
    return;
  }
  corTreeChildAdd(objP, corTreeInteger(allocP, LD_VOCAB_MODIFIED_AT, (long long) ts));
}



// -----------------------------------------------------------------------------
//
// stampCreatedAtIfMissing -
//
static void stampCreatedAtIfMissing(CorNode* objP, uint64_t ts, CorAlloc* allocP)
{
  if (objP == NULL || objP->type != CorObject)
    return;

  if (corTreeLookup(objP, LD_VOCAB_CREATED_AT) == NULL)
    corTreeChildAdd(objP, corTreeInteger(allocP, LD_VOCAB_CREATED_AT, (long long) ts));
}



// -----------------------------------------------------------------------------
//
// addReportEntry - append one per-attr change record
//
// Matches LdMergeReport's shape (used by subscription matcher): each
// record is a CorObject with "attr", "reason", and optional "preValue".
// Report nodes live on the request-scoped allocator (corRest.kallocP).
//
static void addReportEntry(LdMergeReport* reportP, const char* attrName,
                           const char* reason, CorNode* preValue)
{
  if (reportP == NULL)
    return;

  if (reportP->changes == NULL)
    reportP->changes = corTreeArray(corRest.kallocP, "changes");

  CorNode* entry = corTreeObject(corRest.kallocP, NULL);
  corTreeChildAdd(entry, corTreeString(corRest.kallocP, "attr", attrName));
  corTreeChildAdd(entry, corTreeString(corRest.kallocP, "reason", reason));
  if (preValue != NULL)
  {
    CorNode* clone = corTreeClone(corRest.kallocP, preValue);
    ldNodeRename(clone, (char*) "preValue");
    corTreeChildAdd(entry, clone);
  }
  corTreeChildAdd(reportP->changes, entry);
}



// -----------------------------------------------------------------------------
//
// applyType - union fragment's type values into target's type
//
// target's "type" may be CorString (single) or CorArray. Fragment's likewise.
// Result is CorString if only one value, else a CorArray containing all
// unique values.
//
static void applyType(CorNode* target, CorNode* fragType, CorAlloc* allocP)
{
  if (fragType == NULL)
    return;

  CorNode* tType = corTreeLookup(target, "type");

  //
  // Normalize target->type to a CorArray so we can append without caring
  // about the prior shape. Restore to CorString at the end if only one.
  //
  if (tType == NULL)
  {
    // Fragment's type becomes target's type wholesale (clone).
    CorNode* clone = corTreeClone(allocP, fragType);
    ldNodeRename(clone, (char*) "type");
    corTreeChildAdd(target, clone);
    return;
  }

  // Collect existing values into an array form
  CorNode* arrayP = corTreeArray(allocP, "type");
  if (tType->type == CorString)
  {
    corTreeChildAdd(arrayP, corTreeString(allocP, NULL, tType->value.s));
  }
  else if (tType->type == CorArray)
  {
    for (CorNode* c = tType->value.head; c != NULL; c = c->next)
      if (c->type == CorString)
        corTreeChildAdd(arrayP, corTreeString(allocP, NULL, c->value.s));
  }

  // Union in the fragment values (skip duplicates)
  const char* toAdd[32];
  int         toAddN = 0;
  if (fragType->type == CorString)
  {
    toAdd[toAddN++] = fragType->value.s;
  }
  else if (fragType->type == CorArray)
  {
    for (CorNode* c = fragType->value.head; c != NULL; c = c->next)
      if (c->type == CorString && toAddN < 32)
        toAdd[toAddN++] = c->value.s;
  }

  for (int i = 0; i < toAddN; i++)
  {
    bool present = false;
    for (CorNode* c = arrayP->value.head; c != NULL; c = c->next)
      if (c->type == CorString && strcmp(c->value.s, toAdd[i]) == 0) { present = true; break; }
    if (!present)
      corTreeChildAdd(arrayP, corTreeString(allocP, NULL, toAdd[i]));
  }

  // Replace target.type
  removeChild(target, tType, allocP);

  // Count elements in arrayP
  int n = 0;
  for (CorNode* c = arrayP->value.head; c != NULL; c = c->next) n++;

  if (n == 1)
  {
    CorNode* only = arrayP->value.head;
    corTreeChildAdd(target, corTreeString(allocP, "type", only->value.s));
  }
  else
  {
    corTreeChildAdd(target, arrayP);
  }
}



// -----------------------------------------------------------------------------
//
// applyExpiresAt - replace the target's expiresAt, or delete it on the NGSI-LD Null
//
// The fragment carries what ldApiEntityToDbModel left: the epoch-nanosecond integer of the
// DB model, or the NGSI-LD Null string it deliberately does not convert. Either way this is
// an Entity member, not an Attribute - it never grows dataset-keyed instances.
//
static void applyExpiresAt(CorNode* target, CorNode* fragExpiresAt, CorAlloc* allocP)
{
  if (fragExpiresAt == NULL)
    return;

  CorNode* tExpiresAt = corTreeLookup(target, LD_VOCAB_EXPIRES_AT);

  if (isNgsildNull(fragExpiresAt))
  {
    if (tExpiresAt != NULL)
      removeChild(target, tExpiresAt, allocP);

    return;
  }

  CorNode* cloneP = corTreeClone(allocP, fragExpiresAt);

  ldNodeRename(cloneP, (char*) LD_VOCAB_EXPIRES_AT);

  if (tExpiresAt != NULL)
    removeChild(target, tExpiresAt, allocP);

  corTreeChildAdd(target, cloneP);
}



// -----------------------------------------------------------------------------
//
// applyScope - replace or union fragment's scope into target's scope
//
static void applyScope(CorNode* target, CorNode* fragScope, bool overwrite, CorAlloc* allocP)
{
  if (fragScope == NULL)
    return;

  CorNode* tScope = corTreeLookup(target, LD_VOCAB_SCOPE);

  //
  // § 5.4.1 — a member whose value is the NGSI-LD Null is removed from the target, and § 10.2.7.4
  // has scope among the Entity members that can be deleted. Removing it is the point: storing the
  // marker would leave the Entity in a Scope that is not a Scope.
  //
  if ((fragScope->type == CorString) && (strcmp(fragScope->value.s, LD_VOCAB_NGSILD_NULL) == 0))
  {
    if (tScope != NULL)
      removeChild(target, tScope, allocP);

    return;
  }

  if (overwrite || tScope == NULL)
  {
    if (tScope != NULL)
      removeChild(target, tScope, allocP);

    CorNode* clone = corTreeClone(allocP, fragScope);
    ldNodeRename(clone, (char*) LD_VOCAB_SCOPE);
    corTreeChildAdd(target, clone);
    return;
  }

  //
  // Union: merge fragment scope values into target scope (string or array).
  //
  CorNode* arrayP = corTreeArray(allocP, LD_VOCAB_SCOPE);
  if (tScope->type == CorString)
    corTreeChildAdd(arrayP, corTreeString(allocP, NULL, tScope->value.s));
  else if (tScope->type == CorArray)
    for (CorNode* c = tScope->value.head; c != NULL; c = c->next)
      if (c->type == CorString)
        corTreeChildAdd(arrayP, corTreeString(allocP, NULL, c->value.s));

  const char* toAdd[32];
  int         toAddN = 0;
  if (fragScope->type == CorString)
    toAdd[toAddN++] = fragScope->value.s;
  else if (fragScope->type == CorArray)
    for (CorNode* c = fragScope->value.head; c != NULL; c = c->next)
      if (c->type == CorString && toAddN < 32)
        toAdd[toAddN++] = c->value.s;

  for (int i = 0; i < toAddN; i++)
  {
    bool present = false;
    for (CorNode* c = arrayP->value.head; c != NULL; c = c->next)
      if (c->type == CorString && strcmp(c->value.s, toAdd[i]) == 0) { present = true; break; }
    if (!present)
      corTreeChildAdd(arrayP, corTreeString(allocP, NULL, toAdd[i]));
  }

  removeChild(target, tScope, allocP);

  //
  // § 5.2.5: scope is "a String or Array of Strings" — a single scope renders
  // as a bare String, not a one-element Array. Collapse when the union left
  // exactly one value (e.g. target "/x" ∪ fragment "/x").
  //
  CorNode* firstP = arrayP->value.head;
  if ((firstP != NULL) && (firstP->next == NULL))
    corTreeChildAdd(target, corTreeString(allocP, LD_VOCAB_SCOPE, firstP->value.s));
  else
    corTreeChildAdd(target, arrayP);
}



// -----------------------------------------------------------------------------
//
// ldEntityAttrsSet -
//
void ldEntityAttrsSet(CorNode* target, CorNode* fragment,
                      bool overwriteScope, uint64_t ts,
                      LdMergeReport* reportP, CorAlloc* targetAllocP)
{
  if (target == NULL || fragment == NULL || fragment->type != CorObject)
    return;

  bool anyChange = false;

  //
  // First pass: handle top-level keywords (type, scope). id is not
  // copied over — entity id is immutable on append.
  //
  for (CorNode* fP = fragment->value.head; fP != NULL; fP = fP->next)
  {
    if (fP->name == NULL)
      continue;
    if (ldTermId(fP) == CorTermType)
    {
      applyType(target, fP, targetAllocP);
      anyChange = true;
      // Signal the entity-level type change to the DB plugin via the
      // merge report so the surgical $set picks it up. Without this,
      // a fragment carrying ONLY ?type= (no attrs) wouldn't trigger
      // hasSet, and the in-memory union wouldn't persist (ETSI 011_06_*).
      addReportEntry(reportP, "type", "entityModified", NULL);
    }
    else if (ldTermId(fP) == CorTermScope)
    {
      applyScope(target, fP, overwriteScope, targetAllocP);
      anyChange = true;
      addReportEntry(reportP, LD_VOCAB_SCOPE, "entityModified", NULL);
    }
    else if (ldTermId(fP) == CorTermExpiresAt)
    {
      //
      // An Entity member like the two above, and reported the same way - a fragment carrying
      // nothing else has to reach the database too. Deleting it is reported as a deletion:
      // that is the only reason a driver unsets rather than sets.
      //
      applyExpiresAt(target, fP, targetAllocP);
      anyChange = true;
      addReportEntry(reportP, LD_VOCAB_EXPIRES_AT,
                     (corTreeLookup(target, LD_VOCAB_EXPIRES_AT) == NULL) ? "attributeDeleted" : "entityModified",
                     NULL);
    }
  }

  //
  // Second pass: top-level attributes (anything not a keyword).
  //
  for (CorNode* fAttrP = fragment->value.head; fAttrP != NULL; fAttrP = fAttrP->next)
  {
    // type, scope and expiresAt were handled in the first pass - and are Entity members anyway
    if (ldIsNotAttribute(fAttrP))
      continue;

    //
    // Top-level null-marker → delete the whole attr. Used by PATCH /attrs
    // and PATCH /{id} (merge). Append's validator rejects nulls upfront,
    // so this branch never fires for Append callers.
    //
    if (isNgsildNull(fAttrP))
    {
      CorNode* tAttrP = corTreeLookup(target, fAttrP->name);
      if (tAttrP != NULL)
      {
        CorNode* preClone = (reportP != NULL) ? corTreeClone(corRest.kallocP, tAttrP) : NULL;
        removeChild(target, tAttrP, targetAllocP);
        addReportEntry(reportP, fAttrP->name, "attributeDeleted", preClone);
        anyChange = true;
      }
      continue;
    }

    if (fAttrP->type != CorObject)                 continue;

    CorNode* tAttrP = corTreeLookup(target, fAttrP->name);

    if (tAttrP == NULL)
    {
      //
      // New attribute — clone the wrapper with all its dsKey instances.
      // Stamp createdAt/modifiedAt on every instance.
      //
      CorNode* clone = corTreeClone(targetAllocP, fAttrP);
      for (CorNode* instP = clone->value.head; instP != NULL; instP = instP->next)
      {
        if (instP->type != CorObject)
          continue;
        stampCreatedAtIfMissing(instP, ts, targetAllocP);
        bumpModifiedAt(instP, ts, targetAllocP);
      }
      corTreeChildAdd(target, clone);

      addReportEntry(reportP, fAttrP->name, "attributeCreated", NULL);
      anyChange = true;
      continue;
    }

    //
    // Existing attribute — set or append per dsKey instance.
    //
    CorNode* preClone = NULL;
    if (reportP != NULL)
      preClone = corTreeClone(corRest.kallocP, tAttrP);

    CorNode* fInstP = fAttrP->value.head;
    while (fInstP != NULL)
    {
      CorNode* nextInst = fInstP->next;
      if (fInstP->type != CorObject)
      {
        fInstP = nextInst;
        continue;
      }

      CorNode* tInstP = corTreeLookup(tAttrP, fInstP->name);

      //
      // Instance-level null-marker (§ 5.6.2.4 Update Attributes): the
      // primary value member of an instance set to "urn:ngsi-ld:null"
      // means delete that dsKey from the target.
      //
      // After ldApiEntityToDbModel the primary member is always renamed
      // to "value" (Property.value, Relationship.object,
      // LanguageProperty.languageMap → all "value"), so the check is
      // simple. If the wrapper ends up with zero instances, the loop
      // tail re-classifies the report entry from attributeModified to
      // attributeDeleted so subscriptions with notificationTrigger
      // ["attributeDeleted"] match (ETSI 046_22_*).
      //
      if (instanceValueIsNull(fInstP))
      {
        if (tInstP != NULL)
          removeChild(tAttrP, tInstP, targetAllocP);
        fInstP = nextInst;
        continue;
      }

      if (tInstP == NULL)
      {
        // Add this dsKey instance
        CorNode* clone = corTreeClone(targetAllocP, fInstP);
        stampCreatedAtIfMissing(clone, ts, targetAllocP);
        bumpModifiedAt(clone, ts, targetAllocP);
        corTreeChildAdd(tAttrP, clone);
      }
      else
      {
        // Replace instance preserving createdAt
        CorNode* oldCreatedAt = corTreeLookup(tInstP, LD_VOCAB_CREATED_AT);
        long long createdAtNs = (oldCreatedAt != NULL && oldCreatedAt->type == CorInt)
                                ? oldCreatedAt->value.i
                                : (long long) ts;

        CorNode* newInst = corTreeClone(targetAllocP, fInstP);
        // Make sure newInst has createdAt (from old) and modifiedAt=ts
        CorNode* nCreated = corTreeLookup(newInst, LD_VOCAB_CREATED_AT);
        if (nCreated != NULL)
        {
          nCreated->type    = CorInt;
          nCreated->value.i = createdAtNs;
        }
        else
        {
          corTreeChildAdd(newInst, corTreeInteger(targetAllocP, LD_VOCAB_CREATED_AT, createdAtNs));
        }
        bumpModifiedAt(newInst, ts, targetAllocP);

        // Swap: remove old, add new (keeps the name key)
        removeChild(tAttrP, tInstP, targetAllocP);
        corTreeChildAdd(tAttrP, newInst);
      }

      fInstP = nextInst;
    }

    // The attr wrapper (dsKey-keyed map) holds no timestamps — only
    // individual instances do, which were stamped above. If every
    // instance was null'd away, the attribute itself is gone — drop
    // the empty wrapper from the target and report attributeDeleted.
    if (tAttrP->value.head == NULL)
    {
      removeChild(target, tAttrP, targetAllocP);
      addReportEntry(reportP, fAttrP->name, "attributeDeleted", preClone);
    }
    else
    {
      addReportEntry(reportP, fAttrP->name, "attributeModified", preClone);
    }
    anyChange = true;
  }

  if (anyChange)
    bumpModifiedAt(target, ts, targetAllocP);
}
