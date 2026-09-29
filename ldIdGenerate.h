//
// FILE            ldIdGenerate.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORNGSILD_LDIDGENERATE_H_
#define CORNGSILD_LDIDGENERATE_H_

#include "corAlloc/CorAlloc.h"                       // CorAlloc



// -----------------------------------------------------------------------------
//
// ldIdGenerate - "urn:ngsi-ld:<kind>:<seconds, hex>:<counter, hex>", allocated in allocP
//
// The id of a subscription, registration, CSR-subscription, snapshot or notification the
// broker creates. ONE counter for all of them, incremented atomically - see ldIdGenerate.c.
//
extern char* ldIdGenerate(CorAlloc* allocP, const char* kind);

#endif  // CORNGSILD_LDIDGENERATE_H_
