#ifndef CORNGSILD_LDINIT_H_
#define CORNGSILD_LDINIT_H_

//
// FILE            ldInit.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
//
#include "kjson/KjNode.h"                              // KjNode



// -----------------------------------------------------------------------------
//
// ldInit -
//
extern int ldInit(void);



// -----------------------------------------------------------------------------
//
// ldTypedValueCheck - validate a JSON-LD typed @value against its @type.
//
// Shared by the JSON-LD expansion value-check callback (attrContext=false: a
// context-coerced primitive or a registration's free value-object) and by
// ldCheckAttribute (attrContext=true: a typed value-object in an entity Property
// value), so the same @value is validated identically wherever it appears, with
// the error phrased for its context. Datatypes are keyed off the xsd local name
// (short `xsd:foo` or the XMLSchema# IRI); callers map NGSI-LD `DateTime` onto
// xsd:dateTime. An unknown datatype passes — JSON-LD leaves its semantics to the
// application. Returns true (no error) for a non-typed value.
//
extern bool ldTypedValueCheck(const char* subject, const char* datatype, KjNode* valueP, bool attrContext);



// -----------------------------------------------------------------------------
//
// ldCleanup -
//
extern void ldCleanup(void);

#endif  // CORNGSILD_LDINIT_H_
