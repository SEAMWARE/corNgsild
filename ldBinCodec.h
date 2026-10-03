#ifndef CORNGSILD_LDBINCODEC_H_
#define CORNGSILD_LDBINCODEC_H_

//
// FILE            ldBinCodec.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// What the cor format needs to know about NGSI-LD - corTree's codec knows nothing about it.
// See coraine's doc/cor-protocol-details.md § 4.1-4.4.
//
#include "corTree/corTreeBin.h"                       // CorBinCodec



// -----------------------------------------------------------------------------
//
// ldBinCodec - the callbacks
//
// - a name or a string value that is EXACTLY a core term's name travels as its CorTerm id
// - an attribute whose first member is "type": "<one of the 8 attribute types>" is one node: its
//   kind is the LdAttrType, and the member is not written; decoding puts it back, first
//
extern const CorBinCodec ldBinCodec;



// -----------------------------------------------------------------------------
//
// ldBinNamespaceV / ldBinNamespaces - the fixed first entries of every namespace table
//
// Both ends preload them (corTreeBinTablesPreload), in this order. APPEND-ONLY: a peer with a
// shorter list uses the shorter one (cor:// HELLO).
//
extern const char* ldBinNamespaceV[];
extern const int   ldBinNamespaces;

#endif  // CORNGSILD_LDBINCODEC_H_
