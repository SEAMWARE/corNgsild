#ifndef CORNGSILD_LD_LANGUAGE_KEY_H_
#define CORNGSILD_LD_LANGUAGE_KEY_H_

//
// FILE            ldLanguageKey.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corJsonld/CorLdContext.h"                    // CorLdContext



// -----------------------------------------------------------------------------
//
// A languageMap's keys are RFC 5646 language tags (or "@none") on the wire, and
// EXPANDED inside the broker - its internal encoding, so that a q/orderBy trailing
// path addresses a LanguageProperty and a Property alike. Output compaction turns
// the keys back into tags; these two are for every place that handles a tag as a
// VALUE instead (a lang= selection, the temporal languageMaps tuples, the RFC 5646
// check) and so must convert explicitly. Nothing expanded may leave the broker.
//



// -----------------------------------------------------------------------------
//
// ldLanguageTag - the language tag a stored languageMap key stands for
//
// "@none" (and any other JSON-LD keyword) is returned as it is; anything else is
// compacted with contextP (NULL: the core context). A key that does not compact is
// returned unchanged.
//
extern const char* ldLanguageTag(const char* key, CorLdContext* contextP);



// -----------------------------------------------------------------------------
//
// ldLanguageKey - the stored languageMap key for a language tag
//
// The inverse of ldLanguageTag: "@none" as it is, anything else expanded with
// contextP (NULL: the core context). NULL on allocation failure.
//
extern const char* ldLanguageKey(const char* tag, CorLdContext* contextP, CorAlloc* kaP);

#endif  // CORNGSILD_LD_LANGUAGE_KEY_H_
