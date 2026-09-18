#ifndef CORNGSILD_LD_NAME_CONTENT_CHECK_H_
#define CORNGSILD_LD_NAME_CONTENT_CHECK_H_

//
// FILE            ldNameContentCheck.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// § 4.6.2 / § 4.6.4 — name + content validation, applied to the raw
// payload BEFORE JSON-LD expansion. Post-expansion the names are full
// IRIs governed by URI rules.
//
// Both clauses are SHOULD-level in the spec; we choose to reject for
// defense-in-depth (script-injection containment).
//
//   § 4.6.2 — names (Type / Property / Relationship) match
//             unicodeLetter (unicodeLetter | unicodeNumber | "_")*
//             with optional "prefix:name" form.
//
//   § 4.6.4 — string Property values shall not contain any of:
//             <  >  "  '  =  ;  (  )
//
#include <stdbool.h>                                     // bool

#include "kjson/KjNode.h"                                // KjNode



// -----------------------------------------------------------------------------
//
// ldIsValidName - true if `name` matches the § 4.6.2 grammar.
//
// "prefix:name" is allowed (single ':' in the middle, both halves valid
// names individually). Strings that look like IRIs (containing "://")
// are accepted unconditionally — JSON-LD pre-expanded names are full
// URIs and need URI rules, not name rules.
//
extern bool ldIsValidName(const char* name);


#endif  // CORNGSILD_LD_NAME_CONTENT_CHECK_H_
