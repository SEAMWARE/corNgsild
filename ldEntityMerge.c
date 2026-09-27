//
// FILE            ldEntityMerge.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
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
//                   stores, corRest.kallocP for a request-scoped buffer).
//   corRest.kallocP — used for the merge report (per-attribute change records
//                   with cloned preValue subtrees). The report is always
//                   consumed within the same request.
//

#include <stdbool.h>                                  // bool
#include <string.h>                                   // strcmp

#include "kalloc/KAlloc.h"                            // KAlloc
#include "corTree/CorNode.h"                          // CorNode
#include "corTree/corTreeBuilder.h"                   // corTreeObject, corTreeArray, corTreeString, corTreeInteger, corTreeChildAdd, corTreeChildRemove
#include "corTree/corTreeClone.h"                     // corTreeClone
#include "corTree/corTreeLookup.h"                    // corTreeLookup
#include "corTree/corTreeChildReplace.h"              // corTreeChildReplace
#include "corRest/corRest.h"                            // corRest

#include "corJsonld/corLdExpand.h"                      // corLdExpand
#include "corJsonld/corLdInit.h"                        // corLdCoreContext

#include "corNgsild/LdVocab.h"                         // LD_VOCAB_*
#include "corNgsild/LdAttrType.h"                      // LdAttrType, LdAttr*
#include "corNgsild/ldAttrTypeDetect.h"                // ldAttrTypeDetect
#include "corNgsild/ldTypes.h"                         // ldAttrTypeFromString, ldAttrTypeToString
#include "corNgsild/ldError.h"                         // ldError
#include "corNgsild/LdProblem.h"                       // LD_ERROR_*
#include "corNgsild/CorNgsild.h"                        // corNgsild (lang, observedAtNs)
#include "corNgsild/ldIsEntityKeyword.h"               // ldIsEntityKeyword
#include "corNgsild/ldEntityMerge.h"                   // Own interface



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
static inline bool isNgsildNull(const CorNode* nodeP)
{
  if (nodeP == NULL)
    return false;
  if (nodeP->type == CorString)
    return strcmp(nodeP->value.s, LD_VOCAB_NGSILD_NULL) == 0;
  if (nodeP->type == CorObject)
  {
    CorNode* firstP = nodeP->value.firstChildP;
    if (firstP == NULL || firstP->next != NULL) return false;   // exactly one child
    if (firstP->name == NULL || strcmp(firstP->name, "@none") != 0) return false;
    return firstP->type == CorString && strcmp(firstP->value.s, LD_VOCAB_NGSILD_NULL) == 0;
  }
  return false;
}





// -----------------------------------------------------------------------------
//
// bumpModifiedAt - set or add a modifiedAt timestamp on an object node
//
// If the object already has a modifiedAt child, its integer value is updated
// in place (no allocation). Otherwise a new CorInt child is allocated from the
// target allocator (so it lives alongside the object it is attached to).
// createdAt is left alone.
//
static void bumpModifiedAt(CorNode* objP, uint64_t ts, KAlloc* targetAllocP)
{
  if (objP == NULL || objP->type != CorObject)
    return;

  CorNode* mP = corTreeLookup(objP, LD_VOCAB_MODIFIED_AT);
  if (mP != NULL)
  {
    mP->type    = CorInt;
    mP->value.i = (long long) ts;
  }
  else
  {
    corTreeChildAdd(objP, corTreeInteger(targetAllocP, LD_VOCAB_MODIFIED_AT, (long long) ts));
  }
}



// -----------------------------------------------------------------------------
//
// hasModifiedAt - true if objP carries a modifiedAt child
//
// Used to detect "this container is an attribute-instance-like object" so we
// know to cascade timestamp bumps. Raw values (CorObject holding e.g. a JSON
// value like {street, city}) do not carry timestamps.
//
static bool hasModifiedAt(CorNode* objP)
{
  return (objP != NULL && objP->type == CorObject && corTreeLookup(objP, LD_VOCAB_MODIFIED_AT) != NULL);
}



// -----------------------------------------------------------------------------
//
// targetDefaultInstance - return the @none instance of an attribute wrapper,
// or NULL if the wrapper has no default instance (only named datasetId entries).
//
static CorNode* targetDefaultInstance(CorNode* wrapperP)
{
  if (wrapperP == NULL || wrapperP->type != CorObject)
    return NULL;

  return corTreeLookup(wrapperP, "@none");
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
static bool validateNoTypeChange(const char* attrName, CorNode* tWrapper, CorNode* fWrapper)
{
  if (tWrapper == NULL || fWrapper == NULL || fWrapper->type != CorObject)
    return true;

  for (CorNode* fInst = fWrapper->value.firstChildP; fInst != NULL; fInst = fInst->next)
  {
    if (fInst->type != CorObject)
      continue;

    CorNode* fType = corTreeLookup(fInst, "type");
    if (fType == NULL || fType->type != CorString)
      continue;

    CorNode* tInst = corTreeLookup(tWrapper, fInst->name);
    if (tInst == NULL || tInst->type != CorObject)
      continue;

    CorNode* tType = corTreeLookup(tInst, "type");
    if (tType == NULL || tType->type != CorString)
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
// typeChangeToProperty - the fragment value can only be read as a Property
//
// § 5.3.2.3 (Concise-to-Normalized Expansion) step 3 is unconditional: a JSON
// primitive expands to a Property. So a bare value that cannot be read as the
// target's type is not a malformed something-else — it is a Property landing on
// an Attribute that is not one, which Merge Entity refuses as a type change.
//
static void typeChangeToProperty(const char* attrName, LdAttrType targetType)
{
  ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Attribute Type Change",
          "cannot change attribute '%s' type from '%s' to 'Property' in a Merge Entity operation",
          attrName, ldAttrTypeToString(targetType));
}



// -----------------------------------------------------------------------------
//
// buildInstanceFromScalar - construct an attribute-instance object from a
// simplified-form value, shaped to match the target attribute's type.
//
// Only reached for a request that declared ?format=simplified (or the deprecated
// ?options=keyValues) — see ldNormalizeInput. § 10.2.9.4 then says: "If the
// Attribute to be merged is represented in a simplified representation, the type
// of any pre-existing Attribute in the target entity shall be preserved." That
// holds for EVERY Attribute type, so each one below reads the bare value as its
// own § 5.3.2.4 simplified form: a scalar for Property/Relationship/Vocab, an
// array for the two List types, anything at all for a JsonProperty.
//
// Where the bare value cannot be that type's simplified form, the fragment is
// simply a Property (§ 5.3.2.3 step 3) and the merge is an Attribute type change.
//
// Returns NULL with ldError() set on error.
//
static CorNode* buildInstanceFromScalar(const char* attrName,
                                       CorNode*    targetInstance,
                                       CorNode*    fragScalar,
                                       KAlloc*     targetAllocP)
{
  LdAttrType targetType = (targetInstance != NULL)
                            ? ldAttrTypeDetect(targetInstance)
                            : LdAttrProperty;  // no target → default to Property

  CorNode* inst = corTreeObject(targetAllocP, NULL);

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
    corTreeChildAdd(inst, corTreeString(targetAllocP, "type", "Property"));
    CorNode* valueP = corTreeClone(targetAllocP, fragScalar);
    valueP->name = (char*) "value";
    corTreeChildAdd(inst, valueP);
    break;
  }

  case LdAttrRelationship:
  {
    // § 5.3.2.4 EXAMPLE 6 — the simplified form of a Relationship is its object URI.
    if (fragScalar->type != CorString)
    {
      typeChangeToProperty(attrName, targetType);
      return NULL;
    }
    corTreeChildAdd(inst, corTreeString(targetAllocP, "type", "Relationship"));
    CorNode* objP = corTreeClone(targetAllocP, fragScalar);
    objP->name = (char*) "value";
    corTreeChildAdd(inst, objP);
    break;
  }

  case LdAttrLanguageProperty:
  {
    if (corNgsild.lang == NULL || corNgsild.lang[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Missing lang",
              "simplified value for LanguageProperty '%s' requires the 'lang' URL parameter", attrName);
      return NULL;
    }
    //
    // The HTTP binding's own words for the lang parameter: "when ... the value is
    // supplied as a string OR STRING ARRAY in the payload body". § 5.2.6.4.6 lets a
    // languageMap entry hold either, so both go in under the language tag as they come.
    //
    if (fragScalar->type != CorString && fragScalar->type != CorArray)
    {
      typeChangeToProperty(attrName, targetType);
      return NULL;
    }

    if (fragScalar->type == CorArray)
    {
      for (CorNode* elemP = fragScalar->value.firstChildP; elemP != NULL; elemP = elemP->next)
      {
        if (elemP->type != CorString)
        {
          typeChangeToProperty(attrName, targetType);
          return NULL;
        }
      }
    }

    corTreeChildAdd(inst, corTreeString(targetAllocP, "type", "LanguageProperty"));

    // The merge path replaces the whole "value" wholesale (§ 4.5.21 — see
    // replaceWhole in rfc7396Merge). Pre-seed the constructed languageMap with
    // the target's existing entries so the single-lang URL-param update adds/
    // overrides just one key instead of wiping en, fr, etc.
    CorNode* languageMap = corTreeObject(targetAllocP, "value");
    if (targetInstance != NULL)
    {
      CorNode* targetValue = corTreeLookup(targetInstance, "value");
      if (targetValue != NULL && targetValue->type == CorObject)
      {
        for (CorNode* langP = targetValue->value.firstChildP; langP != NULL; langP = langP->next)
        {
          if (langP->name != NULL && strcmp(langP->name, corNgsild.lang) == 0)
            continue;  // skip — will be overwritten by the URL-param entry below
          corTreeChildAdd(languageMap, corTreeClone(targetAllocP, langP));
        }
      }
    }
    CorNode* langEntryP = corTreeClone(targetAllocP, fragScalar);
    langEntryP->name = (char*) corNgsild.lang;
    corTreeChildAdd(languageMap, langEntryP);
    corTreeChildAdd(inst, languageMap);
    break;
  }

  case LdAttrVocabProperty:
  {
    // § 5.3.2.4 EXAMPLE 5 — a VocabProperty's simplified value is its vocab term.
    if (fragScalar->type != CorString)
    {
      typeChangeToProperty(attrName, targetType);
      return NULL;
    }

    //
    // A vocab is stored @vocab-EXPANDED ("go" → ".../default-context/go"): the core
    // context types the "vocab" term as @type:@vocab, so corLdExpandTree expands it
    // wherever it appears under that key, and corLdCompactTree compacts it back on
    // the way out. A bare scalar never sat under a "vocab" key, so it arrives here
    // unexpanded and has to be expanded by hand — otherwise the same term written
    // simplified and written concise would be two different values in the database.
    //
    CorLdContext* ctxP     = (corNgsild.contextP != NULL) ? corNgsild.contextP : corLdCoreContext();
    const char*  vocabIri = corLdExpand(ctxP, fragScalar->value.s, &corRest.kalloc, NULL, NULL);

    corTreeChildAdd(inst, corTreeString(targetAllocP, "type", "VocabProperty"));
    corTreeChildAdd(inst, corTreeString(targetAllocP, "value", (char*) ((vocabIri != NULL) ? vocabIri : fragScalar->value.s)));
    break;
  }

  case LdAttrJsonProperty:
  {
    //
    // § 5.2.6.4.10 — a JsonProperty holds raw JSON, so every bare value is a valid
    // one and nothing needs checking. This is also the one type whose value is NOT
    // descended into: inside a JSON value nothing is a sub-attribute.
    //
    corTreeChildAdd(inst, corTreeString(targetAllocP, "type", "JsonProperty"));
    CorNode* jsonP = corTreeClone(targetAllocP, fragScalar);
    jsonP->name = (char*) "value";
    corTreeChildAdd(inst, jsonP);
    break;
  }

  case LdAttrListProperty:
  case LdAttrListRelationship:
  {
    //
    // § 5.3.2.4 EXAMPLE 8/9 — both List types wear a bare ARRAY in simplified form:
    // an ordered array of values for a ListProperty, of object URIs for a
    // ListRelationship. A bare scalar is not one, and is therefore a Property.
    //
    if (fragScalar->type != CorArray)
    {
      typeChangeToProperty(attrName, targetType);
      return NULL;
    }

    if (targetType == LdAttrListRelationship)
    {
      for (CorNode* elemP = fragScalar->value.firstChildP; elemP != NULL; elemP = elemP->next)
      {
        if (elemP->type != CorString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid ListRelationship",
                  "simplified value for ListRelationship '%s' must be an array of URI strings", attrName);
          return NULL;
        }
      }
    }

    corTreeChildAdd(inst, corTreeString(targetAllocP, "type",
                              (char*) ((targetType == LdAttrListProperty) ? "ListProperty" : "ListRelationship")));
    CorNode* listP = corTreeClone(targetAllocP, fragScalar);
    listP->name = (char*) "value";
    corTreeChildAdd(inst, listP);
    break;
  }

  case LdAttrGeoProperty:
  {
    //
    // § 5.3.2.4 EXAMPLE 2 — a GeoProperty's simplified value is a GeoJSON geometry,
    // which is a JSON OBJECT and so never reaches this function (only non-objects
    // do). Whatever bare value did arrive cannot be a geometry, so it is a Property.
    //
    typeChangeToProperty(attrName, targetType);
    return NULL;
  }

  default:
    //
    // ldAttrTypeDetect could not name the stored Attribute's type — nothing to
    // preserve, so § 5.3.2.3 step 3 stands on its own: the value is a Property.
    //
    corTreeChildAdd(inst, corTreeString(targetAllocP, "type", "Property"));
    CorNode* fallbackP = corTreeClone(targetAllocP, fragScalar);
    fallbackP->name = (char*) "value";
    corTreeChildAdd(inst, fallbackP);
    break;
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
static void injectObservedAtIfNeeded(CorNode* wrapperTarget, CorNode* wrapperFragment,
                                     uint64_t observedAtNs, KAlloc* targetAllocP)
{
  if (observedAtNs == 0 || wrapperTarget == NULL || wrapperFragment == NULL)
    return;

  for (CorNode* fragInst = wrapperFragment->value.firstChildP; fragInst != NULL; fragInst = fragInst->next)
  {
    if (fragInst->type != CorObject)
      continue;

    CorNode* tgtInst = corTreeLookup(wrapperTarget, fragInst->name);
    if (tgtInst == NULL || tgtInst->type != CorObject)
      continue;

    if (corTreeLookup(tgtInst, LD_VOCAB_OBSERVED_AT) == NULL)
      continue;  // target had no observedAt — spec says don't inject

    if (corTreeLookup(fragInst, LD_VOCAB_OBSERVED_AT) != NULL)
      continue;  // fragment has its own observedAt — wins

    corTreeChildAdd(fragInst, corTreeInteger(targetAllocP, LD_VOCAB_OBSERVED_AT, (long long) observedAtNs));
  }
}



// -----------------------------------------------------------------------------
//
// reportAdd - append a change record to the merge report
//
// preClone is optional; if non-NULL it is added as a "preValue" member. All
// report allocations use the request-scoped arena.
//
static void reportAdd(LdMergeReport* reportP, const char* attrName, const char* reason, CorNode* preClone)
{
  if (reportP == NULL)
    return;

  if (reportP->changes == NULL)
    reportP->changes = corTreeArray(corRest.kallocP, NULL);

  CorNode* rec = corTreeObject(corRest.kallocP, NULL);
  corTreeChildAdd(rec, corTreeString(corRest.kallocP, "attr", attrName));
  corTreeChildAdd(rec, corTreeString(corRest.kallocP, "reason", reason));
  if (preClone != NULL)
  {
    preClone->name = (char*) "preValue";
    corTreeChildAdd(rec, preClone);
  }

  corTreeChildAdd(reportP->changes, rec);
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
void ldEntityReplaceReport(CorNode* oldEntityP, CorNode* newEntityP, LdMergeReport* reportP)
{
  if ((oldEntityP == NULL) || (newEntityP == NULL) || (reportP == NULL))
    return;

  // new vs old → attributeCreated (new-only) / attributeModified (in both)
  for (CorNode* nAttrP = newEntityP->value.firstChildP; nAttrP != NULL; nAttrP = nAttrP->next)
  {
    if ((nAttrP->name == NULL) || ldIsEntityKeyword(nAttrP->name) || (strcmp(nAttrP->name, "_id") == 0))
      continue;

    CorNode* oAttrP = corTreeLookup(oldEntityP, nAttrP->name);
    if (oAttrP == NULL)
      reportAdd(reportP, nAttrP->name, "attributeCreated", NULL);
    else
      reportAdd(reportP, nAttrP->name, "attributeModified", corTreeClone(corRest.kallocP, oAttrP));
  }

  // old not in new → attributeDeleted
  for (CorNode* oAttrP = oldEntityP->value.firstChildP; oAttrP != NULL; oAttrP = oAttrP->next)
  {
    if ((oAttrP->name == NULL) || ldIsEntityKeyword(oAttrP->name) || (strcmp(oAttrP->name, "_id") == 0))
      continue;

    if (corTreeLookup(newEntityP, oAttrP->name) == NULL)
      reportAdd(reportP, oAttrP->name, "attributeDeleted", corTreeClone(corRest.kallocP, oAttrP));
  }
}



// -----------------------------------------------------------------------------
//
// rfc7396Merge - apply RFC 7396 merge with NGSI-LD null semantics
//
// Recursively merges patchP into targetP, both CorObjects. Returns true if
// targetP (or any descendant) was actually mutated. For any object encountered
// that carries a modifiedAt child (an attribute-instance-like container), its
// modifiedAt is bumped when a mutation happens at or below it.
//
// patchP is not mutated; nodes that need to be inserted into targetP are
// cloned with targetAllocP.
//
static bool rfc7396Merge(CorNode* targetP, CorNode* patchP, uint64_t ts, KAlloc* targetAllocP, bool deepValueMerge)
{
  bool mutated = false;

  CorNode* pChild = patchP->value.firstChildP;
  while (pChild != NULL)
  {
    CorNode* pNext = pChild->next;

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

    CorNode* tChild = corTreeLookup(targetP, pChild->name);

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
        corTreeChildRemove(targetP, tChild);
        mutated = true;
      }
    }
    else if (tChild == NULL)
    {
      corTreeChildAdd(targetP, corTreeClone(targetAllocP, pChild));
      mutated = true;
    }
    else if (!replaceWhole && tChild->type == CorObject && pChild->type == CorObject)
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
        CorNode* pValueP = corTreeLookup(pChild, "value");
        if (pValueP != NULL && isNgsildNull(pValueP) && corTreeLookup(tChild, "type") != NULL)
          corTreeChildRemove(targetP, tChild);
        else if (hasModifiedAt(tChild))
          bumpModifiedAt(tChild, ts, targetAllocP);
      }
    }
    else
    {
      // Replace scalar / array / type-mismatched member with a clone
      corTreeChildReplace(targetP, tChild, corTreeClone(targetAllocP, pChild));
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
static bool typeHasValue(CorNode* typeP, const char* s)
{
  if (typeP == NULL)
    return false;

  if (typeP->type == CorString)
    return (strcmp(typeP->value.s, s) == 0);

  if (typeP->type == CorArray)
  {
    for (CorNode* e = typeP->value.firstChildP; e != NULL; e = e->next)
      if (e->type == CorString && strcmp(e->value.s, s) == 0)
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
static bool typeUnion(CorNode* target, CorNode* fragType, KAlloc* targetAllocP)
{
  CorNode* tType = corTreeLookup(target, "type");

  if (tType == NULL)
  {
    corTreeChildAdd(target, corTreeClone(targetAllocP, fragType));
    return true;
  }

  const char* fragStrings[64];
  int         fragCount = 0;

  if (fragType->type == CorString)
  {
    fragStrings[fragCount++] = fragType->value.s;
  }
  else if (fragType->type == CorArray)
  {
    for (CorNode* e = fragType->value.firstChildP; e != NULL && fragCount < 64; e = e->next)
    {
      if (e->type == CorString)
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

  if (tType->type == CorString)
  {
    CorNode* arr = corTreeArray(targetAllocP, "type");
    corTreeChildAdd(arr, corTreeString(targetAllocP, NULL, tType->value.s));
    corTreeChildReplace(target, tType, arr);
    tType = arr;
  }

  for (int i = 0; i < newCount; i++)
    corTreeChildAdd(tType, corTreeString(targetAllocP, NULL, newOnes[i]));

  return true;
}



// -----------------------------------------------------------------------------
//
// scopeReplace - replace target's scope with the fragment's scope
//
// Per § 5.6.17.4 the surgical-merge PATCH replaces scope outright when present
// in the fragment (no overwrite flag on the Merge Entity endpoint).
//
static bool scopeReplace(CorNode* target, CorNode* fragScope, KAlloc* targetAllocP)
{
  CorNode* tScope = corTreeLookup(target, LD_VOCAB_SCOPE);

  if (isNgsildNull(fragScope))
  {
    if (tScope != NULL)
    {
      corTreeChildRemove(target, tScope);
      return true;
    }
    return false;
  }

  CorNode* cloneP = corTreeClone(targetAllocP, fragScope);
  cloneP->name   = (char*) LD_VOCAB_SCOPE;

  if (tScope != NULL)
    corTreeChildReplace(target, tScope, cloneP);
  else
    corTreeChildAdd(target, cloneP);

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
static bool mergeAttrWrapper(CorNode* target, CorNode* fragment, uint64_t ts, KAlloc* targetAllocP, bool deepValueMerge)
{
  bool mutated = false;

  CorNode* pChild = fragment->value.firstChildP;
  while (pChild != NULL)
  {
    CorNode* pNext = pChild->next;
    CorNode* tChild = corTreeLookup(target, pChild->name);

    if (isNgsildNull(pChild))
    {
      if (tChild != NULL)
      {
        corTreeChildRemove(target, tChild);
        mutated = true;
      }
    }
    else if (tChild == NULL)
    {
      corTreeChildAdd(target, corTreeClone(targetAllocP, pChild));
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
  CorNode* iChild = target->value.firstChildP;
  while (iChild != NULL)
  {
    CorNode* iNext = iChild->next;
    if (iChild->type == CorObject && corTreeLookup(iChild, "value") == NULL)
    {
      corTreeChildRemove(target, iChild);
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
static bool mergeApply(CorNode*       target,
                       CorNode*       fragment,
                       LdMergeReport* reportP,
                       uint64_t       ts,
                       KAlloc*        targetAllocP,
                       bool           deepValueMerge)
{
  if (target == NULL || target->type != CorObject || fragment == NULL || fragment->type != CorObject)
    return false;

  if (reportP != NULL)
    reportP->changes = NULL;

  bool entityMutated = false;

  CorNode* fChild = fragment->value.firstChildP;
  while (fChild != NULL)
  {
    CorNode*    fNext = fChild->next;
    const char* name  = fChild->name;

    // A nameless node is nothing this loop can act on; every other Entity member is
    // either handled just below (type, scope, expiresAt) or skipped by the shared test.
    if (name == NULL)
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
        bool removed = (corTreeLookup(target, LD_VOCAB_SCOPE) == NULL);

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
      CorNode* tExpiresAt = corTreeLookup(target, LD_VOCAB_EXPIRES_AT);

      if (isNgsildNull(fChild))
      {
        if (tExpiresAt != NULL)
        {
          corTreeChildRemove(target, tExpiresAt);
          reportAdd(reportP, LD_VOCAB_EXPIRES_AT, "attributeDeleted", NULL);
          entityMutated = true;
        }
      }
      else
      {
        CorNode* cloneP = corTreeClone(targetAllocP, fChild);

        cloneP->name = (char*) LD_VOCAB_EXPIRES_AT;

        if (tExpiresAt != NULL)
          corTreeChildReplace(target, tExpiresAt, cloneP);
        else
          corTreeChildAdd(target, cloneP);

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
    if (ldIsNotAttributeName(name))
    {
      fChild = fNext;
      continue;
    }

    // Everything else is an attribute. The fragment's value can be:
    //   * CorString "urn:ngsi-ld:null" → delete the whole attribute
    //   * Other scalar                  → simplified PATCH value; rewrite into
    //                                     an attribute instance whose shape
    //                                     matches the target's existing type
    //                                     (Property.value, Relationship.object,
    //                                     LanguageProperty.languageMap[<lang>])
    //   * CorObject dataset-keyed wrapper (from ldApiEntityToDbModel)
    //
    CorNode* tAttr = corTreeLookup(target, name);

    //
    // In the DB model every Attribute is an object of dataset-keyed instances. Anything else
    // under an attribute name is not one, and walking it as if it were reads a child pointer
    // out of a union that holds a number or a string. No payload may take the broker there.
    //
    if ((tAttr != NULL) && (tAttr->type != CorObject))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute",
              "'%s' is not an Attribute of this Entity and cannot be merged as one", name);
      return false;
    }

    if (isNgsildNull(fChild))
    {
      if (tAttr != NULL)
      {
        CorNode* preClone = corTreeClone(corRest.kallocP, tAttr);
        corTreeChildRemove(target, tAttr);
        reportAdd(reportP, name, "attributeDeleted", preClone);
        entityMutated = true;
      }
      fChild = fNext;
      continue;
    }

    // Simplified scalar: rewrite into a dataset-keyed wrapper matching the
    // target's existing attribute type. targetDefault is the @none instance
    // of the target wrapper (or NULL if target has no such attribute).
    CorNode* fragWrapper = fChild;

    if (fChild->type != CorObject)
    {
      CorNode* targetDefault = targetDefaultInstance(tAttr);
      CorNode* newInstance  = buildInstanceFromScalar(name, targetDefault, fChild, targetAllocP);
      if (newInstance == NULL)
        return false;  // ldError already set

      // Wrap the new instance as { "@none": <instance> } so the rest of the
      // merge pipeline sees a dataset-keyed fragment wrapper.
      fragWrapper = corTreeObject(targetAllocP, (char*) name);
      newInstance->name = (char*) "@none";
      corTreeChildAdd(fragWrapper, newInstance);

      // Stamp createdAt/modifiedAt onto the instance so the DB-model invariant
      // holds even when the normalize step left the scalar untouched.
      corTreeChildAdd(newInstance, corTreeInteger(targetAllocP, LD_VOCAB_CREATED_AT, (long long) ts));
      corTreeChildAdd(newInstance, corTreeInteger(targetAllocP, LD_VOCAB_MODIFIED_AT, (long long) ts));
    }

    // If the URL has ?observedAt=... inject that timestamp into any fragment
    // instance whose target instance previously had observedAt but where the
    // fragment itself does not.
    if (corNgsild.observedAtNs != 0 && tAttr != NULL)
      injectObservedAtIfNeeded(tAttr, fragWrapper, corNgsild.observedAtNs, targetAllocP);

    if (tAttr == NULL)
    {
      // If fragWrapper was built fresh in this function (scalar case) it is
      // already on targetAllocP and can be grafted as-is; otherwise it is
      // still in the caller's fragment tree and must be cloned.
      CorNode* toAdd = (fragWrapper == fChild) ? corTreeClone(targetAllocP, fragWrapper) : fragWrapper;
      corTreeChildAdd(target, toAdd);
      reportAdd(reportP, name, "attributeCreated", NULL);
      entityMutated = true;
    }
    else
    {
      // Attribute type must not change — reject now so the merge stops before
      // mutating anything in the target tree.
      if (!validateNoTypeChange(name, tAttr, fragWrapper))
        return false;

      CorNode* preClone = corTreeClone(corRest.kallocP, tAttr);
      if (mergeAttrWrapper(tAttr, fragWrapper, ts, targetAllocP, deepValueMerge))
      {
        // mergeAttrWrapper may have stripped instances whose primary value
        // member was set to "urn:ngsi-ld:null" (§ 4.5.21). When all instances
        // are gone, the attribute itself is gone — remove it from the target
        // and report this as "attributeDeleted" so subscriptions with
        // notificationTrigger=["attributeDeleted"] match (ETSI 046_22_*).
        if (tAttr->value.firstChildP == NULL)
        {
          corTreeChildRemove(target, tAttr);
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
bool ldEntityFragmentApply(CorNode* target, CorNode* fragment, LdMergeReport* reportP, uint64_t ts, KAlloc* targetAllocP)
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
bool ldEntityMerge(CorNode* target, CorNode* fragment, LdMergeReport* reportP, uint64_t ts, KAlloc* targetAllocP)
{
  return mergeApply(target, fragment, reportP, ts, targetAllocP, true);
}
