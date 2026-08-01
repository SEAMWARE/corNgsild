//
// FILE            ldEntityAttrsSet.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

#include <string.h>                                   // strcmp

#include "kjson/KjNode.h"                             // KjNode
#include "kjson/kjBuilder.h"                          // kjObject, kjString, kjInteger, kjChildAdd, kjChildRemove
#include "kjson/kjLookup.h"                           // kjLookup
#include "kjson/kjClone.h"                            // kjClone
#include "kjson/kjFree.h"                             // kjFree
#include "swRest/swRest.h"                            // swRest (request-scoped allocator for the report)

#include "swNgsild/LdVocab.h"                         // LD_VOCAB_*
#include "swNgsild/ldEntityAttrsSet.h"                // Own interface
#include "swNgsild/ldIsEntityKeyword.h"                   // ldIsNotAttributeName



// -----------------------------------------------------------------------------
//
// removeChild - unlink a node from its container and, when the target lives on
// the malloc heap (allocP == NULL, e.g. the in-memory store), free it. On a
// request arena (allocP != NULL, e.g. mongoc) the arena reclaims it, so freeing
// would be wrong. The node must not be referenced elsewhere — every call site
// here replaces it wholesale (report entries hold separate clones).
//
static void removeChild(KjNode* container, KjNode* node, Kjson* allocP)
{
  kjChildRemove(container, node);
  if (allocP == NULL)
    kjFree(node);
}



// -----------------------------------------------------------------------------
//
// isNgsildNull - true if node is the "urn:ngsi-ld:null" delete-marker string
//
static inline bool isNgsildNull(const KjNode* nodeP)
{
  return (nodeP != NULL &&
          nodeP->type == KjString &&
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
static bool instanceValueIsNull(KjNode* instP)
{
  if (instP == NULL || instP->type != KjObject)
    return false;

  KjNode* valP = kjLookup(instP, "value");
  if (valP == NULL)
    return false;

  if (isNgsildNull(valP))
    return true;

  if (valP->type == KjObject)
  {
    KjNode* noneP = kjLookup(valP, "@none");
    if (isNgsildNull(noneP))
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// bumpModifiedAt - set modifiedAt on an object (replace in-place or add)
//
static void bumpModifiedAt(KjNode* objP, uint64_t ts, Kjson* allocP)
{
  if (objP == NULL || objP->type != KjObject)
    return;

  KjNode* mP = kjLookup(objP, LD_VOCAB_MODIFIED_AT);
  if (mP != NULL)
  {
    mP->type    = KjInt;
    mP->value.i = (long long) ts;
    return;
  }
  kjChildAdd(objP, kjInteger(allocP, LD_VOCAB_MODIFIED_AT, (long long) ts));
}



// -----------------------------------------------------------------------------
//
// stampCreatedAtIfMissing -
//
static void stampCreatedAtIfMissing(KjNode* objP, uint64_t ts, Kjson* allocP)
{
  if (objP == NULL || objP->type != KjObject)
    return;

  if (kjLookup(objP, LD_VOCAB_CREATED_AT) == NULL)
    kjChildAdd(objP, kjInteger(allocP, LD_VOCAB_CREATED_AT, (long long) ts));
}



// -----------------------------------------------------------------------------
//
// addReportEntry - append one per-attr change record
//
// Matches LdMergeReport's shape (used by subscription matcher): each
// record is a KjObject with "attr", "reason", and optional "preValue".
// Report nodes live on the request-scoped allocator (swRest.kjsonP).
//
static void addReportEntry(LdMergeReport* reportP, const char* attrName,
                           const char* reason, KjNode* preValue)
{
  if (reportP == NULL)
    return;

  if (reportP->changes == NULL)
    reportP->changes = kjArray(swRest.kjsonP, "changes");

  KjNode* entry = kjObject(swRest.kjsonP, NULL);
  kjChildAdd(entry, kjString(swRest.kjsonP, "attr",   attrName));
  kjChildAdd(entry, kjString(swRest.kjsonP, "reason", reason));
  if (preValue != NULL)
  {
    KjNode* clone = kjClone(swRest.kjsonP, preValue);
    clone->name = (char*) "preValue";
    kjChildAdd(entry, clone);
  }
  kjChildAdd(reportP->changes, entry);
}



// -----------------------------------------------------------------------------
//
// applyType - union fragment's type values into target's type
//
// target's "type" may be KjString (single) or KjArray. Fragment's likewise.
// Result is KjString if only one value, else a KjArray containing all
// unique values.
//
static void applyType(KjNode* target, KjNode* fragType, Kjson* allocP)
{
  if (fragType == NULL)
    return;

  KjNode* tType = kjLookup(target, "type");

  //
  // Normalize target->type to a KjArray so we can append without caring
  // about the prior shape. Restore to KjString at the end if only one.
  //
  if (tType == NULL)
  {
    // Fragment's type becomes target's type wholesale (clone).
    KjNode* clone = kjClone(allocP, fragType);
    clone->name = (char*) "type";
    kjChildAdd(target, clone);
    return;
  }

  // Collect existing values into an array form
  KjNode* arrayP = kjArray(allocP, "type");
  if (tType->type == KjString)
  {
    kjChildAdd(arrayP, kjString(allocP, NULL, tType->value.s));
  }
  else if (tType->type == KjArray)
  {
    for (KjNode* c = tType->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString)
        kjChildAdd(arrayP, kjString(allocP, NULL, c->value.s));
  }

  // Union in the fragment values (skip duplicates)
  const char* toAdd[32];
  int         toAddN = 0;
  if (fragType->type == KjString)
  {
    toAdd[toAddN++] = fragType->value.s;
  }
  else if (fragType->type == KjArray)
  {
    for (KjNode* c = fragType->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString && toAddN < 32)
        toAdd[toAddN++] = c->value.s;
  }

  for (int i = 0; i < toAddN; i++)
  {
    bool present = false;
    for (KjNode* c = arrayP->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString && strcmp(c->value.s, toAdd[i]) == 0) { present = true; break; }
    if (!present)
      kjChildAdd(arrayP, kjString(allocP, NULL, toAdd[i]));
  }

  // Replace target.type
  removeChild(target, tType, allocP);

  // Count elements in arrayP
  int n = 0;
  for (KjNode* c = arrayP->value.firstChildP; c != NULL; c = c->next) n++;

  if (n == 1)
  {
    KjNode* only = arrayP->value.firstChildP;
    kjChildAdd(target, kjString(allocP, "type", only->value.s));
  }
  else
  {
    kjChildAdd(target, arrayP);
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
static void applyExpiresAt(KjNode* target, KjNode* fragExpiresAt, Kjson* allocP)
{
  if (fragExpiresAt == NULL)
    return;

  KjNode* tExpiresAt = kjLookup(target, LD_VOCAB_EXPIRES_AT);

  if (isNgsildNull(fragExpiresAt))
  {
    if (tExpiresAt != NULL)
      removeChild(target, tExpiresAt, allocP);

    return;
  }

  KjNode* cloneP = kjClone(allocP, fragExpiresAt);

  cloneP->name = (char*) LD_VOCAB_EXPIRES_AT;

  if (tExpiresAt != NULL)
    removeChild(target, tExpiresAt, allocP);

  kjChildAdd(target, cloneP);
}



// -----------------------------------------------------------------------------
//
// applyScope - replace or union fragment's scope into target's scope
//
static void applyScope(KjNode* target, KjNode* fragScope, bool overwrite, Kjson* allocP)
{
  if (fragScope == NULL)
    return;

  KjNode* tScope = kjLookup(target, LD_VOCAB_SCOPE);

  //
  // § 5.4.1 — a member whose value is the NGSI-LD Null is removed from the target, and § 10.2.7.4
  // has scope among the Entity members that can be deleted. Removing it is the point: storing the
  // marker would leave the Entity in a Scope that is not a Scope.
  //
  if ((fragScope->type == KjString) && (strcmp(fragScope->value.s, LD_VOCAB_NGSILD_NULL) == 0))
  {
    if (tScope != NULL)
      removeChild(target, tScope, allocP);

    return;
  }

  if (overwrite || tScope == NULL)
  {
    if (tScope != NULL)
      removeChild(target, tScope, allocP);

    KjNode* clone = kjClone(allocP, fragScope);
    clone->name = (char*) LD_VOCAB_SCOPE;
    kjChildAdd(target, clone);
    return;
  }

  //
  // Union: merge fragment scope values into target scope (string or array).
  //
  KjNode* arrayP = kjArray(allocP, LD_VOCAB_SCOPE);
  if (tScope->type == KjString)
    kjChildAdd(arrayP, kjString(allocP, NULL, tScope->value.s));
  else if (tScope->type == KjArray)
    for (KjNode* c = tScope->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString)
        kjChildAdd(arrayP, kjString(allocP, NULL, c->value.s));

  const char* toAdd[32];
  int         toAddN = 0;
  if (fragScope->type == KjString)
    toAdd[toAddN++] = fragScope->value.s;
  else if (fragScope->type == KjArray)
    for (KjNode* c = fragScope->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString && toAddN < 32)
        toAdd[toAddN++] = c->value.s;

  for (int i = 0; i < toAddN; i++)
  {
    bool present = false;
    for (KjNode* c = arrayP->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString && strcmp(c->value.s, toAdd[i]) == 0) { present = true; break; }
    if (!present)
      kjChildAdd(arrayP, kjString(allocP, NULL, toAdd[i]));
  }

  removeChild(target, tScope, allocP);

  //
  // § 5.2.5: scope is "a String or Array of Strings" — a single scope renders
  // as a bare String, not a one-element Array. Collapse when the union left
  // exactly one value (e.g. target "/x" ∪ fragment "/x").
  //
  KjNode* firstP = arrayP->value.firstChildP;
  if ((firstP != NULL) && (firstP->next == NULL))
    kjChildAdd(target, kjString(allocP, LD_VOCAB_SCOPE, firstP->value.s));
  else
    kjChildAdd(target, arrayP);
}



// -----------------------------------------------------------------------------
//
// ldEntityAttrsSet -
//
void ldEntityAttrsSet(KjNode* target, KjNode* fragment,
                      bool overwriteScope, uint64_t ts,
                      LdMergeReport* reportP, Kjson* targetAllocP)
{
  if (target == NULL || fragment == NULL || fragment->type != KjObject)
    return;

  bool anyChange = false;

  //
  // First pass: handle top-level keywords (type, scope). id is not
  // copied over — entity id is immutable on append.
  //
  for (KjNode* fP = fragment->value.firstChildP; fP != NULL; fP = fP->next)
  {
    if (fP->name == NULL)
      continue;
    if (strcmp(fP->name, "type") == 0)
    {
      applyType(target, fP, targetAllocP);
      anyChange = true;
      // Signal the entity-level type change to the DB plugin via the
      // merge report so the surgical $set picks it up. Without this,
      // a fragment carrying ONLY ?type= (no attrs) wouldn't trigger
      // hasSet, and the in-memory union wouldn't persist (ETSI 011_06_*).
      addReportEntry(reportP, "type", "entityModified", NULL);
    }
    else if (strcmp(fP->name, LD_VOCAB_SCOPE) == 0)
    {
      applyScope(target, fP, overwriteScope, targetAllocP);
      anyChange = true;
      addReportEntry(reportP, LD_VOCAB_SCOPE, "entityModified", NULL);
    }
    else if (strcmp(fP->name, LD_VOCAB_EXPIRES_AT) == 0)
    {
      //
      // An Entity member like the two above, and reported the same way - a fragment carrying
      // nothing else has to reach the database too. Deleting it is reported as a deletion:
      // that is the only reason a driver unsets rather than sets.
      //
      applyExpiresAt(target, fP, targetAllocP);
      anyChange = true;
      addReportEntry(reportP, LD_VOCAB_EXPIRES_AT,
                     (kjLookup(target, LD_VOCAB_EXPIRES_AT) == NULL) ? "attributeDeleted" : "entityModified",
                     NULL);
    }
  }

  //
  // Second pass: top-level attributes (anything not a keyword).
  //
  for (KjNode* fAttrP = fragment->value.firstChildP; fAttrP != NULL; fAttrP = fAttrP->next)
  {
    // type, scope and expiresAt were handled in the first pass - and are Entity members anyway
    if (ldIsNotAttributeName(fAttrP->name))
      continue;

    //
    // Top-level null-marker → delete the whole attr. Used by PATCH /attrs
    // and PATCH /{id} (merge). Append's validator rejects nulls upfront,
    // so this branch never fires for Append callers.
    //
    if (isNgsildNull(fAttrP))
    {
      KjNode* tAttrP = kjLookup(target, fAttrP->name);
      if (tAttrP != NULL)
      {
        KjNode* preClone = (reportP != NULL) ? kjClone(swRest.kjsonP, tAttrP) : NULL;
        removeChild(target, tAttrP, targetAllocP);
        addReportEntry(reportP, fAttrP->name, "attributeDeleted", preClone);
        anyChange = true;
      }
      continue;
    }

    if (fAttrP->type != KjObject)                  continue;

    KjNode* tAttrP = kjLookup(target, fAttrP->name);

    if (tAttrP == NULL)
    {
      //
      // New attribute — clone the wrapper with all its dsKey instances.
      // Stamp createdAt/modifiedAt on every instance.
      //
      KjNode* clone = kjClone(targetAllocP, fAttrP);
      for (KjNode* instP = clone->value.firstChildP; instP != NULL; instP = instP->next)
      {
        if (instP->type != KjObject)
          continue;
        stampCreatedAtIfMissing(instP, ts, targetAllocP);
        bumpModifiedAt(instP, ts, targetAllocP);
      }
      kjChildAdd(target, clone);

      addReportEntry(reportP, fAttrP->name, "attributeCreated", NULL);
      anyChange = true;
      continue;
    }

    //
    // Existing attribute — set or append per dsKey instance.
    //
    KjNode* preClone = NULL;
    if (reportP != NULL)
      preClone = kjClone(swRest.kjsonP, tAttrP);

    KjNode* fInstP = fAttrP->value.firstChildP;
    while (fInstP != NULL)
    {
      KjNode* nextInst = fInstP->next;
      if (fInstP->type != KjObject)
      {
        fInstP = nextInst;
        continue;
      }

      KjNode* tInstP = kjLookup(tAttrP, fInstP->name);

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
        KjNode* clone = kjClone(targetAllocP, fInstP);
        stampCreatedAtIfMissing(clone, ts, targetAllocP);
        bumpModifiedAt(clone, ts, targetAllocP);
        kjChildAdd(tAttrP, clone);
      }
      else
      {
        // Replace instance preserving createdAt
        KjNode* oldCreatedAt = kjLookup(tInstP, LD_VOCAB_CREATED_AT);
        long long createdAtNs = (oldCreatedAt != NULL && oldCreatedAt->type == KjInt)
                                ? oldCreatedAt->value.i
                                : (long long) ts;

        KjNode* newInst = kjClone(targetAllocP, fInstP);
        // Make sure newInst has createdAt (from old) and modifiedAt=ts
        KjNode* nCreated = kjLookup(newInst, LD_VOCAB_CREATED_AT);
        if (nCreated != NULL)
        {
          nCreated->type    = KjInt;
          nCreated->value.i = createdAtNs;
        }
        else
        {
          kjChildAdd(newInst, kjInteger(targetAllocP, LD_VOCAB_CREATED_AT, createdAtNs));
        }
        bumpModifiedAt(newInst, ts, targetAllocP);

        // Swap: remove old, add new (keeps the name key)
        removeChild(tAttrP, tInstP, targetAllocP);
        kjChildAdd(tAttrP, newInst);
      }

      fInstP = nextInst;
    }

    // The attr wrapper (dsKey-keyed map) holds no timestamps — only
    // individual instances do, which were stamped above. If every
    // instance was null'd away, the attribute itself is gone — drop
    // the empty wrapper from the target and report attributeDeleted.
    if (tAttrP->value.firstChildP == NULL)
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
