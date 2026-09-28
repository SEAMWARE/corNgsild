#ifndef CORNGSILD_LD_PARSE_H_
#define CORNGSILD_LD_PARSE_H_

//
// FILE            ldParse.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool

#include "corTree/CorNode.h"                            // CorNode
#include "corJson/CorJson.h"                            // CorJson



// -----------------------------------------------------------------------------
//
// ldParseKeyHook - corJson's key hook (CorJson.keyF) for NGSI-LD request bodies
//
// Called the moment a member's name is final, before its value is parsed: stamps the
// node with its term id - the core term it is (corLdCoreLookup, one probe, no
// allocation), or LD_TERM_NOT_CORE. Every member of the body arrives stamped, so
// ldTermId never has to look a request node up, and the expander uses the stamp
// instead of expanding a core term's name again.
//
// Stamping only (step B.3a): no renaming, no duplicate check, no deferred list - those
// need to know where values and JSON literals are, which comes with the deferred pass.
//
extern bool ldParseKeyHook(CorJson* corJsonP, CorNode* containerP, CorNode* nodeP, int depth);



// -----------------------------------------------------------------------------
//
// ldPrePayloadParseHook - corRest's pre-parse hook: the key hook for NGSI-LD bodies only
//
// A route with an NGSI-LD operation carries an NGSI-LD payload; any other body (an
// @context document posted to /jsonldContexts, ...) is parsed as plain JSON.
//
extern void ldPrePayloadParseHook(void);

#endif  // CORNGSILD_LD_PARSE_H_
