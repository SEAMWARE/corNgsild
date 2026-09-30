#ifndef CORNGSILD_LDQEXPANDVALUES_H_
#define CORNGSILD_LDQEXPANDVALUES_H_

//
// FILE            ldQExpandValues.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool

#include "corAlloc/CorAlloc.h"                           // CorAlloc
#include "corJsonld/CorLdContext.h"                      // CorLdContext
#include "corNgsild/LdQ.h"                               // LdQNode



// -----------------------------------------------------------------------------
//
// ldQExpandValues - apply expandValues to a parsed q: expand the values of the attributes it names
//
// A Query (§ 5.2.6.5.1) and a Subscription (§ 5.2.6.5.2) both carry expandValues: "Values of the
// identified attributes should be expanded against the supplied @context". So, for each term whose
// attribute expandValues names (and whose operator is not a pattern):
//
//   - a quoted string, or a string item of a value list, is expanded with contextP;
//   - a bare word (LdQBareWord - see ldQParseBareWords) is expanded the same way, and becomes a
//     string: from here on it is one.
//
// A bare word whose attribute expandValues does NOT name has no meaning - it is not a Value of the
// grammar - and is rejected: 400 BadRequestData, the words ldQParse uses, and false. With no
// expandValues at all (evV NULL) that is every bare word.
//
// Both sides of the comparison end up expanded: term.attr already is (the parser expands it).
//
extern bool ldQExpandValues(LdQNode* qExpr, char** evV, CorLdContext* contextP, CorAlloc* kaP);



// -----------------------------------------------------------------------------
//
// ldQLangProperties - a q [..] under a LanguageProperty is a LANGUAGE TAG, not a term
//
// Not NGSI-LD (spec-doubts #134). § 7.2.3.2 makes every segment of a q path a short name to expand -
// right for a Property's compound value, whose member names are JSON-LD - while § 7.2.3.4 uses the
// same bracket for the language of a LanguageProperty, whose languageMap keys are RFC 5646 tags,
// stored as tags. Only the attribute's TYPE tells the two apart, and q does not carry it: the
// query does, in langProperties (a URL param, a Query body member, a Subscription member), the way
// expandValues says which values are vocabulary. So for every term whose attribute lpV names, the
// value path goes back to the segments as sent (valuePathRawV). No guessing from the segment - `es`
// may as well be a member of a compound value.
//
extern void ldQLangProperties(LdQNode* qExpr, char** lpV);

#endif  // CORNGSILD_LDQEXPANDVALUES_H_
