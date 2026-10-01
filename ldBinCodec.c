//
// FILE            ldBinCodec.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// What the cor format needs to know about NGSI-LD - see header.
//
#include <stdint.h>                                      // uint8_t, uint16_t, uint32_t
#include <string.h>                                      // strcmp
#include <pthread.h>                                     // pthread_once

#include "corAlloc/CorAlloc.h"                           // CorAlloc
#include "corTree/CorNode.h"                             // CorNode
#include "corTree/corTreeBuilder.h"                      // corTreeString
#include "corTree/corTreeChildPrepend.h"                 // corTreeChildPrepend
#include "corTree/corTreeBin.h"                          // CorBinCodec

#include "corNgsild/CorTerm.h"                           // CorTerm, CorTermType, CorTermLast
#include "corNgsild/LdAttrType.h"                        // LdAttrType
#include "corNgsild/ldCoreTermIds.h"                     // ldCoreTermNameV
#include "corNgsild/ldTermId.h"                          // LD_TERM_NOT_CORE
#include "corNgsild/ldBinCodec.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// termHash - core-term name -> id, built once from ldCoreTermNameV
//
// Its own, and not corLdCoreLookup: an exact match on the NAME is what makes the round trip lossless
// - an expanded core IRI is a different string, and must come back as that string.
//
static uint16_t        termHash[1024];             // id, 0 = empty - open addressing
static pthread_once_t  termHashOnce = PTHREAD_ONCE_INIT;

static uint32_t hash(const char* s)
{
  uint32_t h = 2166136261u;

  while (*s != 0)
  {
    h ^= (uint8_t) *s++;
    h *= 16777619u;
  }

  return h;
}

static void termHashBuild(void)
{
  for (int id = 1; id < CorTermLast; id++)
  {
    if (ldCoreTermNameV[id] == NULL)
      continue;

    uint32_t ix = hash(ldCoreTermNameV[id]) & 1023;

    while (termHash[ix] != 0)
      ix = (ix + 1) & 1023;

    termHash[ix] = (uint16_t) id;
  }
}

static uint16_t termLookup(const char* s)
{
  pthread_once(&termHashOnce, termHashBuild);

  for (uint32_t ix = hash(s) & 1023; termHash[ix] != 0; ix = (ix + 1) & 1023)
  {
    if (strcmp(ldCoreTermNameV[termHash[ix]], s) == 0)
      return termHash[ix];
  }

  return 0;
}



// -----------------------------------------------------------------------------
//
// The callbacks
//
static uint16_t nameTermId(CorNode* nodeP)
{
  //
  // A node corNgsild has already classified carries the answer: "not a core term", or an id - which
  // is used only if the name IS that term's name, exactly (an expanded core IRI could carry the same
  // id, and must travel as the string it is). One strcmp instead of a hash and a probe.
  //
  uint16_t id = nodeP->termId;

  if (id == LD_TERM_NOT_CORE)
    return 0;

  if ((id != 0) && (id < CorTermLast))
    return (strcmp(nodeP->name, ldCoreTermNameV[id]) == 0) ? id : 0;

  return termLookup(nodeP->name);
}

static uint16_t valueTermId(const char* value)
{
  return termLookup(value);
}

static const char* termName(uint16_t termId)
{
  return ((termId > 0) && (termId < CorTermLast)) ? ldCoreTermNameV[termId] : NULL;
}



// -----------------------------------------------------------------------------
//
// attrTypeName - the 8 attribute types, by LdAttrType - which is the kind on the wire
//
static const char* attrTypeName[] =
{
  NULL,                     // LdAttrNone
  "Property",
  "Relationship",
  "GeoProperty",
  "LanguageProperty",
  "VocabProperty",
  "ListProperty",
  "ListRelationship",
  "JsonProperty"
};



// -----------------------------------------------------------------------------
//
// objectKind - an attribute: "type" first, and one of the 8 types - the member becomes the kind
//
// FIRST, so that decoding puts it back exactly where it was. An attribute built any other way is
// written as it is: still lossless, a few bytes larger.
//
static uint8_t objectKind(CorNode* objectP, CorNode** foldedP)
{
  CorNode* firstP = objectP->value.head;

  *foldedP = NULL;

  if ((firstP == NULL) || (firstP->type != CorString) || (strcmp(firstP->name, "type") != 0))
    return 0;

  for (int kind = LdAttrProperty; kind <= LdAttrJsonProperty; kind++)
  {
    if (strcmp(firstP->value.s, attrTypeName[kind]) == 0)
    {
      *foldedP = firstP;
      return (uint8_t) kind;
    }
  }

  return 0;
}



// -----------------------------------------------------------------------------
//
// objectKindExpand - the "type" member back, first
//
static void objectKindExpand(CorNode* objectP, uint8_t kind, CorAlloc* kaP)
{
  if ((kind < LdAttrProperty) || (kind > LdAttrJsonProperty))
    return;

  CorNode* typeP = corTreeString(kaP, "type", attrTypeName[kind]);

  if (typeP == NULL)
    return;

  typeP->termId = CorTermType;
  corTreeChildPrepend(objectP, typeP);
}



// -----------------------------------------------------------------------------
//
// ldBinCodec -
//
const CorBinCodec ldBinCodec =
{
  nameTermId,
  valueTermId,
  termName,
  objectKind,
  objectKindExpand
};



// -----------------------------------------------------------------------------
//
// ldBinNamespaceV - APPEND-ONLY
//
const char* ldBinNamespaceV[] =
{
  "https://uri.etsi.org/ngsi-ld/default-context/",
  "https://uri.etsi.org/ngsi-ld/",
  "urn:ngsi-ld:"
};

const int ldBinNamespaces = sizeof(ldBinNamespaceV) / sizeof(ldBinNamespaceV[0]);
