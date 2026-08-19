#ifndef CORNGSILD_LD_EXPIRES_AT_PROPAGATE_H_
#define CORNGSILD_LD_EXPIRES_AT_PROPAGATE_H_

//
// FILE            ldExpiresAtPropagate.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// § 4.5.5.2 — when an Entity arrives from a registered Context Source
// with a top-level expiresAt, propagate it to each Attribute. If the
// Attribute already carries an expiresAt further in the future, shorten
// it to the entity-level value. Entity-level expiresAt itself is left
// in place — both the entity-wide and per-attribute expiry are part of
// the assembled view.
//
// Operates on the post-apiAttrToStorageWrap shape (keys are expanded
// IRIs at the attribute level, but system attrs like "expiresAt" stay
// short), and on the DB model, whose expiresAt is epoch-nanoseconds
// rather than an ISO string (the Snapshot capture path). An instance
// inherits the shape of the Entity-level value it copies. Idempotent.
//
#include "kjson/KjNode.h"                                 // KjNode

struct Kjson;
extern void ldExpiresAtPropagate(KjNode* entityP, struct Kjson* kjsonP);

#endif
