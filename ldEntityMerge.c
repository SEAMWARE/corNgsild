//
// FILE            ldEntityMerge.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Merge Entity (PATCH /entities/{id}) implementation — see ETSI GS CIM 009
// v1.9.1 clauses 5.5.12 (Merge Patch Behaviour) and 5.6.17 (Merge Entity),
// which are adaptations of IETF RFC 7396 (JSON Merge Patch). NGSI-LD reserves
// the string "urn:ngsi-ld:null" as the delete-marker (JSON null is forbidden
// in NGSI-LD payloads because JSON-LD @context processing consumes it).
//
// Two allocators are threaded through the merge:
//   targetAllocP  — used for any node grafted into the target entity. Must
//                   match the target's lifetime (NULL for malloc-backed
//                   stores, swRest.kjsonP for a request-scoped buffer).
//   swRest.kjsonP — used for the merge report (per-attribute change records
//                   with cloned preValue subtrees). The report is always
//                   consumed within the same request.
//

#include <stdbool.h>                                  // bool
#include <string.h>                                   // strcmp

#include "kjson/KjNode.h"                             // KjNode
#include "kjson/kjBuilder.h"                          // kjObject, kjArray, kjString, kjInteger, kjChildAdd, kjChildRemove
#include "kjson/kjClone.h"                            // kjClone
#include "kjson/kjLookup.h"                           // kjLookup
#include "kjson/kjChildReplace.h"                     // kjChildReplace
#include "swRest/swRest.h"                            // swRest

#include "swNgsild/LdVocab.h"                         // LD_VOCAB_*
#include "swNgsild/LdAttrType.h"                      // LdAttrType, LdAttr*
#include "swNgsild/ldAttrTypeDetect.h"                // ldAttrTypeDetect
#include "swNgsild/ldTypes.h"                         // ldAttrTypeFromString, ldAttrTypeToString
#include "swNgsild/ldError.h"                         // ldError
#include "swNgsild/LdProblem.h"                       // LD_ERROR_*
#include "swNgsild/SwNgsild.h"                        // swNgsild (lang, observedAtNs)
#include "swNgsild/ldIsEntityKeyword.h"               // ldIsEntityKeyword
#include "swNgsild/ldEntityMerge.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// isNgsildNull - check if a node is the ngsi-ld null delete-marker
//
// Two shapes per § 4.5:
//   - the URI string "urn:ngsi-ld:null" (any URI-valued slot)
//   - the JSON object {"@none": "urn:ngsi-ld:null"} (languageMap slot —
//     because languageMap is itself an object, the URI cannot stand
//     alone; the @none key is the universal-language default)
//
static inline bool isNgsildNull(const KjNode* nodeP)
{
  if (nodeP == NULL)
    return false;
  if (nodeP->type == KjString)
    return strcmp(nodeP->value.s, LD_VOCAB_NGSILD_NULL) == 0;
  if (nodeP->type == KjObject)
  {
    KjNode* firstP = nodeP->value.firstChildP;
    if (firstP == NULL || firstP->next != NULL) return false;   // exactly one child
    if (firstP->name == NULL || strcmp(firstP->name, "@none") != 0) return false;
    return firstP->type == KjString && strcmp(firstP->value.s, LD_VOCAB_NGSILD_NULL) == 0;
  }
  return false;
}





// -----------------------------------------------------------------------------
//
// bumpModifiedAt - set or add a modifiedAt timestamp on an object node
//
// If the object already has a modifiedAt child, its integer value is updated
// in place (no allocation). Otherwise a new KjInt child is allocated from the
// target allocator (so it lives alongside the object it is attached to).
// createdAt is left alone.
//
static void bumpModifiedAt(KjNode* objP, uint64_t ts, Kjson* targetAllocP)
{
  if (objP == NULL || objP->type != KjObject)
    return;

  KjNode* mP = kjLookup(objP, LD_VOCAB_MODIFIED_AT);
  if (mP != NULL)
  {
    mP->type    = KjInt;
    mP->value.i = (long long) ts;
  }
  else
  {
    kjChildAdd(objP, kjInteger(targetAllocP, LD_VOCAB_MODIFIED_AT, (long long) ts));
  }
}



// -----------------------------------------------------------------------------
//
// hasModifiedAt - true if objP carries a modifiedAt child
//
// Used to detect "this container is an attribute-instance-like object" so we
// know to cascade timestamp bumps. Raw values (KjObject holding e.g. a JSON
// value like {street, city}) do not carry timestamps.
//
static bool hasModifiedAt(KjNode* objP)
{
  return (objP != NULL && objP->type == KjObject && kjLookup(objP, LD_VOCAB_MODIFIED_AT) != NULL);
}



// -----------------------------------------------------------------------------
//
// targetDefaultInstance - return the @none instance of an attribute wrapper,
// or NULL if the wrapper has no default instance (only named datasetId entries).
//
static KjNode* targetDefaultInstance(KjNode* wrapperP)
{
  if (wrapperP == NULL || wrapperP->type != KjObject)
    return NULL;

  return kjLookup(wrapperP, "@none");
}



// -----------------------------------------------------------------------------
//
// validateNoTypeChange - reject attempts to change an attribute's type via merge
//
// Per ETSI GS CIM 009 v1.9.1 § 5.6.4.4 (Partial Attribute update): "it is not
// allowed to change the type of an Attribute". § 6.5.3.4 (PATCH Merge Entity)
// states the same for format=simplified; this implementation applies the rule
// unconditionally, as a Property whose type was flipped to Relationship would
// be structurally inconsistent (the old hasValue stays around and there's no
// hasObject, leaving the attribute in a broken state).
//
// Walks fragment wrapper instances. Whenever a fragment instance carries an
// explicit "type" that differs from the target instance with the same
// dataset key, raises a BadRequestData 400 and returns false.
//
static bool validateNoTypeChange(const char* attrName, KjNode* tWrapper, KjNode* fWrapper)
{
  if (tWrapper == NULL || fWrapper == NULL || fWrapper->type != KjObject)
    return true;

  for (KjNode* fInst = fWrapper->value.firstChildP; fInst != NULL; fInst = fInst->next)
  {
    if (fInst->type != KjObject)
      continue;

    KjNode* fType = kjLookup(fInst, "type");
    if (fType == NULL || fType->type != KjString)
      continue;

    KjNode* tInst = kjLookup(tWrapper, fInst->name);
    if (tInst == NULL || tInst->type != KjObject)
      continue;

    KjNode* tType = kjLookup(tInst, "type");
    if (tType == NULL || tType->type != KjString)
      continue;

    // Target's type is typically the JSON-LD-expanded IRI
    // ("https://uri.etsi.org/ngsi-ld/Property"); fragment's type may be the
    // short form because ldNormalizeInput injected it after JSON-LD expansion.
    // Compare via the enum.
    LdAttrType fEnum = ldAttrTypeFromString(fType->value.s);
    LdAttrType tEnum = ldAttrTypeFromString(tType->value.s);

    if (fEnum == LdAttrNone || tEnum == LdAttrNone)
      continue;  // one side unknown — let downstream handle it

    if (fEnum != tEnum)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Attribute Type Change",
              "cannot change attribute '%s' type from '%s' to '%s' in a Merge Entity operation",
              attrName, ldAttrTypeToString(tEnum), ldAttrTypeToString(fEnum));
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// buildInstanceFromScalar - construct an attribute-instance object from a
// simplified-form scalar, shaped to match the target attribute's type.
//
// Returns NULL with ldError() set on error (e.g. LanguageProperty without
// lang URL param, or unsupported target type like GeoProperty).
//
static KjNode* buildInstanceFromScalar(const char* attrName,
                                       KjNode*     targetInstance,
                                       KjNode*     fragScalar,
                                       Kjson*      targetAllocP)
{
  LdAttrType targetType = (targetInstance != NULL)
                            ? ldAttrTypeDetect(targetInstance)
                            : LdAttrProperty;  // no target → default to Property

  KjNode* inst = kjObject(targetAllocP, NULL);

  //
  // Note: in the DB model, ldApiEntityToDbModel's normalizeValueKey renames
  // every hasValue/hasObject/hasLanguageMap/... IRI to the short key "value".
  // So when we build an instance here we MUST use "value" as the key name, to
  // match the target's existing shape. Otherwise the RFC 7396 merge will see
  // fragment and target as having different keys and will duplicate the field
  // instead of overwriting it.
  //
  switch (targetType)
  {
  case LdAttrProperty:
  {
    kjChildAdd(inst, kjString(targetAllocP, "type", "Property"));
    KjNode* valueP = kjClone(targetAllocP, fragScalar);
    valueP->name = (char*) "value";
    kjChildAdd(inst, valueP);
    break;
  }

  case LdAttrRelationship:
  {
    if (fragScalar->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Relationship",
              "simplified value for Relationship '%s' must be a URI string", attrName);
      return NULL;
    }
    kjChildAdd(inst, kjString(targetAllocP, "type", "Relationship"));
    KjNode* objP = kjClone(targetAllocP, fragScalar);
    objP->name = (char*) "value";
    kjChildAdd(inst, objP);
    break;
  }

  case LdAttrLanguageProperty:
  {
    if (swNgsild.lang == NULL || swNgsild.lang[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Missing lang",
              "simplified value for LanguageProperty '%s' requires the 'lang' URL parameter", attrName);
      return NULL;
    }
    if (fragScalar->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid LanguageProperty",
              "simplified value for LanguageProperty '%s' must be a string", attrName);
      return NULL;
    }

    kjChildAdd(inst, kjString(targetAllocP, "type", "LanguageProperty"));

    // The merge path replaces the whole "value" wholesale (§ 4.5.21 — see
    // replaceWhole in rfc7396Merge). Pre-seed the constructed languageMap with
    // the target's existing entries so the single-lang URL-param update adds/
    // overrides just one key instead of wiping en, fr, etc.
    KjNode* languageMap = kjObject(targetAllocP, "value");
    if (targetInstance != NULL)
    {
      KjNode* targetValue = kjLookup(targetInstance, "value");
      if (targetValue != NULL && targetValue->type == KjObject)
      {
        for (KjNode* langP = targetValue->value.firstChildP; langP != NULL; langP = langP->next)
        {
          if (langP->name != NULL && strcmp(langP->name, swNgsild.lang) == 0)
            continue;  // skip — will be overwritten by the URL-param entry below
          kjChildAdd(languageMap, kjClone(targetAllocP, langP));
        }
      }
    }
    kjChildAdd(languageMap, kjString(targetAllocP, swNgsild.lang, fragScalar->value.s));
    kjChildAdd(inst, languageMap);
    break;
  }

  default:
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Unsupported simplified merge",
            "attribute '%s' has a type that does not accept simplified scalar values", attrName);
    return NULL;
  }

  return inst;
}



// -----------------------------------------------------------------------------
//
// injectObservedAtIfNeeded - implement the observedAt URL parameter semantic
//
// For each fragment attribute instance whose target counterpart previously
// contained an observedAt sub-attribute, inject the URL param observedAt
// value (in nanoseconds) into the fragment instance — but only if the
// fragment instance does not already carry an explicit observedAt. See
// ETSI GS CIM 009 v1.9.1 § 5.6.17.4 and § 6.5.3.4 Table 1.
//
// wrapperTarget / wrapperFragment are the dataset-keyed wrappers of a single
// top-level attribute (e.g. {@none: {...}, "urn:ds:1": {...}}).
//
static void injectObservedAtIfNeeded(KjNode* wrapperTarget, KjNode* wrapperFragment,
                                     uint64_t observedAtNs, Kjson* targetAllocP)
{
  if (observedAtNs == 0 || wrapperTarget == NULL || wrapperFragment == NULL)
    return;

  for (KjNode* fragInst = wrapperFragment->value.firstChildP; fragInst != NULL; fragInst = fragInst->next)
  {
    if (fragInst->type != KjObject)
      continue;

    KjNode* tgtInst = kjLookup(wrapperTarget, fragInst->name);
    if (tgtInst == NULL || tgtInst->type != KjObject)
      continue;

    if (kjLookup(tgtInst, LD_VOCAB_OBSERVED_AT) == NULL)
      continue;  // target had no observedAt — spec says don't inject

    if (kjLookup(fragInst, LD_VOCAB_OBSERVED_AT) != NULL)
      continue;  // fragment has its own observedAt — wins

    kjChildAdd(fragInst, kjInteger(targetAllocP, LD_VOCAB_OBSERVED_AT, (long long) observedAtNs));
  }
}



// -----------------------------------------------------------------------------
//
// reportAdd - append a change record to the merge report
//
// preClone is optional; if non-NULL it is added as a "preValue" member. All
// report allocations use the request-scoped kjson arena.
//
static void reportAdd(LdMergeReport* reportP, const char* attrName, const char* reason, KjNode* preClone)
{
  if (reportP == NULL)
    return;

  if (reportP->changes == NULL)
    reportP->changes = kjArray(swRest.kjsonP, NULL);

  KjNode* rec = kjObject(swRest.kjsonP, NULL);
  kjChildAdd(rec, kjString(swRest.kjsonP, "attr",   attrName));
  kjChildAdd(rec, kjString(swRest.kjsonP, "reason", reason));
  if (preClone != NULL)
  {
    preClone->name = (char*) "preValue";
    kjChildAdd(rec, preClone);
  }

  kjChildAdd(reportP->changes, rec);
}



// -----------------------------------------------------------------------------
//
// ldEntityReplaceReport - change-report by diffing the old vs the new entity
//
// For Replace (PUT /entities/{id}): relative to the stored entity, an attribute
// present only in the NEW body is attributeCreated, present only in the OLD is
// attributeDeleted, present in BOTH is attributeModified. Both entities are in
// DB-model form (expanded-IRI attribute names). A clone of the old attribute
// subtree is attached as preValue for modified/deleted, so the value-change-only
// filter and the attribute-delete markers can use it. Without this report a
// Replace would only notify entityUpdated subscriptions — never the default set
// or the attribute-level (created/updated/deleted) triggers.
//
void ldEntityReplaceReport(KjNode* oldEntityP, KjNode* newEntityP, LdMergeReport* reportP)
{
  if ((oldEntityP == NULL) || (newEntityP == NULL) || (reportP == NULL))
    return;

  // new vs old → attributeCreated (new-only) / attributeModified (in both)
  for (KjNode* nAttrP = newEntityP->value.firstChildP; nAttrP != NULL; nAttrP = nAttrP->next)
  {
    if ((nAttrP->name == NULL) || ldIsEntityKeyword(nAttrP->name) || (strcmp(nAttrP->name, "_id") == 0))
      continue;

    KjNode* oAttrP = kjLookup(oldEntityP, nAttrP->name);
    if (oAttrP == NULL)
      reportAdd(reportP, nAttrP->name, "attributeCreated", NULL);
    else
      reportAdd(reportP, nAttrP->name, "attributeModified", kjClone(swRest.kjsonP, oAttrP));
  }

  // old not in new → attributeDeleted
  for (KjNode* oAttrP = oldEntityP->value.firstChildP; oAttrP != NULL; oAttrP = oAttrP->next)
  {
    if ((oAttrP->name == NULL) || ldIsEntityKeyword(oAttrP->name) || (strcmp(oAttrP->name, "_id") == 0))
      continue;

    if (kjLookup(newEntityP, oAttrP->name) == NULL)
      reportAdd(reportP, oAttrP->name, "attributeDeleted", kjClone(swRest.kjsonP, oAttrP));
  }
}



// -----------------------------------------------------------------------------
//
// rfc7396Merge - apply RFC 7396 merge with NGSI-LD null semantics
//
// Recursively merges patchP into targetP, both KjObjects. Returns true if
// targetP (or any descendant) was actually mutated. For any object encountered
// that carries a modifiedAt child (an attribute-instance-like container), its
// modifiedAt is bumped when a mutation happens at or below it.
//
// patchP is not mutated; nodes that need to be inserted into targetP are
// cloned with targetAllocP.
//
static bool rfc7396Merge(KjNode* targetP, KjNode* patchP, uint64_t ts, Kjson* targetAllocP, bool deepValueMerge)
{
  bool mutated = false;

  KjNode* pChild = patchP->value.firstChildP;
  while (pChild != NULL)
  {
    KjNode* pNext = pChild->next;

    // Skip system-managed timestamps: ldApiEntityToDbModel injects createdAt /
    // modifiedAt onto the fragment at the current request time. createdAt must
    // never be overwritten on the target, and modifiedAt is handled separately
    // by bumpModifiedAt on the way back up.
    if (pChild->name != NULL &&
        (strcmp(pChild->name, LD_VOCAB_CREATED_AT)  == 0 ||
         strcmp(pChild->name, LD_VOCAB_MODIFIED_AT) == 0))
    {
      pChild = pNext;
      continue;
    }

    KjNode* tChild = kjLookup(targetP, pChild->name);

    // The primary-value member of every attribute type (Property.value,
    // Relationship.object, LanguageProperty.languageMap, JsonProperty.json, ...)
    // is normalised by ldApiEntityToDbModel to the key "value".
    //
    // Two opposite semantics, selected by the caller:
    //   - replace/append (Update/Partial-Attribute Update, Append, Replace):
    //     the primary value is replaced WHOLESALE, never deep-merged. This is
    //     what PartialAttributeUpdate asserts (ETSI 012_04_01, 012_08_01).
    //   - true Merge Entity (RFC 7396, § 10.2.9): the value is a normal JSON
    //     value and a merge surgically deep-merges object values (keeping
    //     unspecified siblings; null deletes), per RFC 7396.
    bool replaceWhole = (deepValueMerge == false) && (pChild->name != NULL && strcmp(pChild->name, "value") == 0);

    if (isNgsildNull(pChild))
    {
      if (tChild != NULL)
      {
        kjChildRemove(targetP, tChild);
        mutated = true;
      }
    }
    else if (tChild == NULL)
    {
      kjChildAdd(targetP, kjClone(targetAllocP, pChild));
      mutated = true;
    }
    else if (!replaceWhole && tChild->type == KjObject && pChild->type == KjObject)
    {
      if (rfc7396Merge(tChild, pChild, ts, targetAllocP, deepValueMerge))
      {
        mutated = true;

        // § 5.4.1 sub-attribute removal: if the PATCH set this sub-attribute's
        // primary value to the NGSI-LD Null marker (Property.value / Relationship.
        // object / …, normalised to "value"), the recursion above removed that
        // member — the sub-attribute is now an orphaned typed shell and is
        // removed too. Guarded on the patch carrying value=null (not on tChild's
        // post-state alone) so a JsonProperty's opaque object value whose own
        // keys change — its content may itself hold a "type" key — is never
        // mistaken for an orphaned sub-attribute.
        KjNode* pValueP = kjLookup(pChild, "value");
        if (pValueP != NULL && isNgsildNull(pValueP) && kjLookup(tChild, "type") != NULL)
          kjChildRemove(targetP, tChild);
        else if (hasModifiedAt(tChild))
          bumpModifiedAt(tChild, ts, targetAllocP);
      }
    }
    else
    {
      // Replace scalar / array / type-mismatched member with a clone
      kjChildReplace(targetP, tChild, kjClone(targetAllocP, pChild));
      mutated = true;
    }

    pChild = pNext;
  }

  return mutated;
}



// -----------------------------------------------------------------------------
//
// typeHasValue -
//
static bool typeHasValue(KjNode* typeP, const char* s)
{
  if (typeP == NULL)
    return false;

  if (typeP->type == KjString)
    return (strcmp(typeP->value.s, s) == 0);

  if (typeP->type == KjArray)
  {
    for (KjNode* e = typeP->value.firstChildP; e != NULL; e = e->next)
      if (e->type == KjString && strcmp(e->value.s, s) == 0)
        return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// typeUnion - union fragment types into target's type member
//
// Per § 5.6.17.4, any fragment types not already present in the target are
// appended to the target's type list. Promotes a scalar target type to an
// array if any new types are added.
//
// Returns true if target was mutated.
//
static bool typeUnion(KjNode* target, KjNode* fragType, Kjson* targetAllocP)
{
  KjNode* tType = kjLookup(target, "type");

  if (tType == NULL)
  {
    kjChildAdd(target, kjClone(targetAllocP, fragType));
    return true;
  }

  const char* fragStrings[64];
  int         fragCount = 0;

  if (fragType->type == KjString)
  {
    fragStrings[fragCount++] = fragType->value.s;
  }
  else if (fragType->type == KjArray)
  {
    for (KjNode* e = fragType->value.firstChildP; e != NULL && fragCount < 64; e = e->next)
    {
      if (e->type == KjString)
        fragStrings[fragCount++] = e->value.s;
    }
  }

  const char* newOnes[64];
  int         newCount = 0;
  for (int i = 0; i < fragCount; i++)
  {
    if (!typeHasValue(tType, fragStrings[i]))
      newOnes[newCount++] = fragStrings[i];
  }

  if (newCount == 0)
    return false;

  if (tType->type == KjString)
  {
    KjNode* arr = kjArray(targetAllocP, "type");
    kjChildAdd(arr, kjString(targetAllocP, NULL, tType->value.s));
    kjChildReplace(target, tType, arr);
    tType = arr;
  }

  for (int i = 0; i < newCount; i++)
    kjChildAdd(tType, kjString(targetAllocP, NULL, newOnes[i]));

  return true;
}



// -----------------------------------------------------------------------------
//
// scopeReplace - replace target's scope with the fragment's scope
//
// Per § 5.6.17.4 the surgical-merge PATCH replaces scope outright when present
// in the fragment (no overwrite flag on the Merge Entity endpoint).
//
static bool scopeReplace(KjNode* target, KjNode* fragScope, Kjson* targetAllocP)
{
  KjNode* tScope = kjLookup(target, LD_VOCAB_SCOPE);

  if (isNgsildNull(fragScope))
  {
    if (tScope != NULL)
    {
      kjChildRemove(target, tScope);
      return true;
    }
    return false;
  }

  KjNode* cloneP = kjClone(targetAllocP, fragScope);
  cloneP->name   = (char*) LD_VOCAB_SCOPE;

  if (tScope != NULL)
    kjChildReplace(target, tScope, cloneP);
  else
    kjChildAdd(target, cloneP);

  return true;
}



// -----------------------------------------------------------------------------
//
// mergeAttrWrapper - merge a dataset-keyed attribute wrapper from fragment into target
//
// Both wrappers look like:
//   { "@none": {type, value, ...}, "urn:ds:1": {...}, ... }
// Each child is an attribute instance.
//
// Returns true if anything inside the wrapper was mutated.
//
static bool mergeAttrWrapper(KjNode* target, KjNode* fragment, uint64_t ts, Kjson* targetAllocP, bool deepValueMerge)
{
  bool mutated = false;

  KjNode* pChild = fragment->value.firstChildP;
  while (pChild != NULL)
  {
    KjNode* pNext  = pChild->next;
    KjNode* tChild = kjLookup(target, pChild->name);

    if (isNgsildNull(pChild))
    {
      if (tChild != NULL)
      {
        kjChildRemove(target, tChild);
        mutated = true;
      }
    }
    else if (tChild == NULL)
    {
      kjChildAdd(target, kjClone(targetAllocP, pChild));
      mutated = true;
    }
    else
    {
      if (rfc7396Merge(tChild, pChild, ts, targetAllocP, deepValueMerge))
      {
        bumpModifiedAt(tChild, ts, targetAllocP);
        mutated = true;
      }
    }

    pChild = pNext;
  }

  //
  // Per spec § 4.5.21: setting a Property's `value`, a Relationship's `object`,
  // a LanguageProperty's `languageMap` (and other type-specific primary value
  // members) to "urn:ngsi-ld:null" inside a merge means that instance is being
  // deleted. The recursive merge above already removed the null'd member from
  // the target instance — sweep the wrapper now for any instance that no longer
  // has its primary value member, and remove those instances entirely. The
  // caller (the per-attribute branch in ldEntityMerge) handles the case where
  // the wrapper ends up with zero instances by re-classifying the merge report
  // entry from "attributeModified" to "attributeDeleted".
  //
  // Storage form keeps every typed primary key (object / languageMap /
  // vocab / json / valueList / objectList) under "value" — q can't filter
  // otherwise. So one lookup is enough.
  KjNode* iChild = target->value.firstChildP;
  while (iChild != NULL)
  {
    KjNode* iNext = iChild->next;
    if (iChild->type == KjObject && kjLookup(iChild, "value") == NULL)
    {
      kjChildRemove(target, iChild);
      mutated = true;
    }
    iChild = iNext;
  }

  return mutated;
}



// -----------------------------------------------------------------------------
//
// mergeApply - shared core for both fragment-apply (replace/append) and the
// true RFC 7396 Merge Entity. deepValueMerge selects the value-leaf semantics:
//   false → replace the primary value wholesale (Update/Partial/Append/Replace)
//   true  → surgically deep-merge object values (Merge Entity, § 10.2.9)
//
static bool mergeApply(KjNode*        target,
                       KjNode*        fragment,
                       LdMergeReport* reportP,
                       uint64_t       ts,
                       Kjson*         targetAllocP,
                       bool           deepValueMerge)
{
  if (target == NULL || target->type != KjObject || fragment == NULL || fragment->type != KjObject)
    return false;

  if (reportP != NULL)
    reportP->changes = NULL;

  bool entityMutated = false;

  KjNode* fChild = fragment->value.firstChildP;
  while (fChild != NULL)
  {
    KjNode*     fNext = fChild->next;
    const char* name  = fChild->name;

    // Skip keywords we either inject ourselves or ignore at merge time
    if (name == NULL ||
        strcmp(name, "id")                 == 0 ||
        strcmp(name, "@id")                == 0 ||
        strcmp(name, LD_VOCAB_CREATED_AT)  == 0 ||
        strcmp(name, LD_VOCAB_MODIFIED_AT) == 0)
    {
      fChild = fNext;
      continue;
    }

    //
    // type — union semantics, scope — replace semantics.
    //
    // Both are reported like any other change: the database drivers persist what the report
    // names, so a merge that touches nothing but the Entity members would otherwise be merged
    // in memory, notified, and then dropped on the floor by the write. "entityModified" is the
    // reason ldEntityAttrsSet already uses for these two, and every driver treats a reason
    // other than "attributeDeleted" as "take this member from the merged Entity".
    //
    if (strcmp(name, "type") == 0 || strcmp(name, "@type") == 0)
    {
      if (typeUnion(target, fChild, targetAllocP))
      {
        entityMutated = true;
        reportAdd(reportP, "type", "entityModified", NULL);
      }
      fChild = fNext;
      continue;
    }

    if (strcmp(name, LD_VOCAB_SCOPE) == 0)
    {
      if (scopeReplace(target, fChild, targetAllocP))
      {
        entityMutated = true;

        // A removed scope has to be reported as such - the drivers only unset what is named deleted
        bool removed = (kjLookup(target, LD_VOCAB_SCOPE) == NULL);

        reportAdd(reportP, LD_VOCAB_SCOPE, (removed == true) ? "attributeDeleted" : "entityModified", NULL);
      }
      fChild = fNext;
      continue;
    }

    //
    // expiresAt — replace semantics, like scope. It arrives from ldApiEntityToDbModel as the
    // epoch-nanosecond integer the DB model stores, or as the NGSI-LD Null string (left alone
    // there, so that the delete survives the conversion).
    //
    if (strcmp(name, LD_VOCAB_EXPIRES_AT) == 0)
    {
      KjNode* tExpiresAt = kjLookup(target, LD_VOCAB_EXPIRES_AT);

      if (isNgsildNull(fChild))
      {
        if (tExpiresAt != NULL)
        {
          kjChildRemove(target, tExpiresAt);
          reportAdd(reportP, LD_VOCAB_EXPIRES_AT, "attributeDeleted", NULL);
          entityMutated = true;
        }
      }
      else
      {
        KjNode* cloneP = kjClone(targetAllocP, fChild);

        cloneP->name = (char*) LD_VOCAB_EXPIRES_AT;

        if (tExpiresAt != NULL)
          kjChildReplace(target, tExpiresAt, cloneP);
        else
          kjChildAdd(target, cloneP);

        reportAdd(reportP, LD_VOCAB_EXPIRES_AT, "entityModified", NULL);
        entityMutated = true;
      }

      fChild = fNext;
      continue;
    }

    //
    // Whatever else is an Entity member is none of the attribute machinery's business. The list
    // above is hand-written and ldIsEntityKeyword is the authority on what is not an Attribute;
    // when the two drift apart, a member falls through to the attribute branch below and is
    // merged against a stored value that is not an attribute wrapper at all - which is how a
    // plain PATCH of expiresAt came to walk an integer as if it were a list of instances.
    //
    if (ldIsEntityKeyword(name))
    {
      fChild = fNext;
      continue;
    }

    // Everything else is an attribute. The fragment's value can be:
    //   * KjString "urn:ngsi-ld:null"  → delete the whole attribute
    //   * Other scalar                  → simplified PATCH value; rewrite into
    //                                     an attribute instance whose shape
    //                                     matches the target's existing type
    //                                     (Property.value, Relationship.object,
    //                                     LanguageProperty.languageMap[<lang>])
    //   * KjObject dataset-keyed wrapper (from ldApiEntityToDbModel)
    //
    KjNode* tAttr = kjLookup(target, name);

    //
    // In the DB model every Attribute is an object of dataset-keyed instances. Anything else
    // under an attribute name is not one, and walking it as if it were reads a child pointer
    // out of a union that holds a number or a string. No payload may take the broker there.
    //
    if ((tAttr != NULL) && (tAttr->type != KjObject))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute",
              "'%s' is not an Attribute of this Entity and cannot be merged as one", name);
      return false;
    }

    if (isNgsildNull(fChild))
    {
      if (tAttr != NULL)
      {
        KjNode* preClone = kjClone(swRest.kjsonP, tAttr);
        kjChildRemove(target, tAttr);
        reportAdd(reportP, name, "attributeDeleted", preClone);
        entityMutated = true;
      }
      fChild = fNext;
      continue;
    }

    // Simplified scalar: rewrite into a dataset-keyed wrapper matching the
    // target's existing attribute type. targetDefault is the @none instance
    // of the target wrapper (or NULL if target has no such attribute).
    KjNode* fragWrapper = fChild;

    if (fChild->type != KjObject)
    {
      KjNode* targetDefault = targetDefaultInstance(tAttr);
      KjNode* newInstance   = buildInstanceFromScalar(name, targetDefault, fChild, targetAllocP);
      if (newInstance == NULL)
        return false;  // ldError already set

      // Wrap the new instance as { "@none": <instance> } so the rest of the
      // merge pipeline sees a dataset-keyed fragment wrapper.
      fragWrapper = kjObject(targetAllocP, (char*) name);
      newInstance->name = (char*) "@none";
      kjChildAdd(fragWrapper, newInstance);

      // Stamp createdAt/modifiedAt onto the instance so the DB-model invariant
      // holds even when the normalize step left the scalar untouched.
      kjChildAdd(newInstance, kjInteger(targetAllocP, LD_VOCAB_CREATED_AT,  (long long) ts));
      kjChildAdd(newInstance, kjInteger(targetAllocP, LD_VOCAB_MODIFIED_AT, (long long) ts));
    }

    // If the URL has ?observedAt=... inject that timestamp into any fragment
    // instance whose target instance previously had observedAt but where the
    // fragment itself does not.
    if (swNgsild.observedAtNs != 0 && tAttr != NULL)
      injectObservedAtIfNeeded(tAttr, fragWrapper, swNgsild.observedAtNs, targetAllocP);

    if (tAttr == NULL)
    {
      // If fragWrapper was built fresh in this function (scalar case) it is
      // already on targetAllocP and can be grafted as-is; otherwise it is
      // still in the caller's fragment tree and must be cloned.
      KjNode* toAdd = (fragWrapper == fChild) ? kjClone(targetAllocP, fragWrapper) : fragWrapper;
      kjChildAdd(target, toAdd);
      reportAdd(reportP, name, "attributeCreated", NULL);
      entityMutated = true;
    }
    else
    {
      // Attribute type must not change — reject now so the merge stops before
      // mutating anything in the target tree.
      if (!validateNoTypeChange(name, tAttr, fragWrapper))
        return false;

      KjNode* preClone = kjClone(swRest.kjsonP, tAttr);
      if (mergeAttrWrapper(tAttr, fragWrapper, ts, targetAllocP, deepValueMerge))
      {
        // mergeAttrWrapper may have stripped instances whose primary value
        // member was set to "urn:ngsi-ld:null" (§ 4.5.21). When all instances
        // are gone, the attribute itself is gone — remove it from the target
        // and report this as "attributeDeleted" so subscriptions with
        // notificationTrigger=["attributeDeleted"] match (ETSI 046_22_*).
        if (tAttr->value.firstChildP == NULL)
        {
          kjChildRemove(target, tAttr);
          reportAdd(reportP, name, "attributeDeleted", preClone);
        }
        else
        {
          reportAdd(reportP, name, "attributeModified", preClone);
        }
        entityMutated = true;
      }
    }

    fChild = fNext;
  }

  if (entityMutated)
    bumpModifiedAt(target, ts, targetAllocP);

  return true;
}



// -----------------------------------------------------------------------------
//
// ldEntityFragmentApply - apply an Entity Fragment with REPLACE/append semantics
//
// Used by Update Attributes (§ 10.2.3), Partial Attribute Update (§ 10.2.5),
// Append Attributes (§ 10.2.4) and Replace Attribute: each supplied attribute
// instance replaces the matching stored one (or is appended / null-deleted);
// the primary value is replaced wholesale, never deep-merged.
//
bool ldEntityFragmentApply(KjNode* target, KjNode* fragment, LdMergeReport* reportP, uint64_t ts, Kjson* targetAllocP)
{
  return mergeApply(target, fragment, reportP, ts, targetAllocP, false);
}



// -----------------------------------------------------------------------------
//
// ldEntityMerge - apply Merge Entity (§ 10.2.9) with true RFC 7396 semantics
//
// Surgically deep-merges object values (keeping unspecified siblings; null
// deletes a member), per IETF RFC 7396 (JSON Merge Patch).
//
bool ldEntityMerge(KjNode* target, KjNode* fragment, LdMergeReport* reportP, uint64_t ts, Kjson* targetAllocP)
{
  return mergeApply(target, fragment, reportP, ts, targetAllocP, true);
}
