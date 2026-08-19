#ifndef CORNGSILD_LDSNAPSHOTNOTIFY_H_
#define CORNGSILD_LDSNAPSHOTNOTIFY_H_

//
// FILE            ldSnapshotNotify.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// SnapshotNotification (§ 5.3.4 / § 5.16.6).
//
// Fired on every Snapshot status transition that the spec considers
// observable: capture-finished (postSnapshot / cloneSnapshot), update
// (patchSnapshot), delete (deleteSnapshot / purgeSnapshots).
//
// No-op when the Snapshot's `endpoint` member is unset.
//
#include <stdbool.h>                                     // bool
#include "corNgsild/LdSnapshotCache.h"                    // LdSnapshotCacheItem


// -----------------------------------------------------------------------------
//
// ldSnapshotNotify -
//
// itemP   : snapshot whose status changed
// deleted : when true, the SnapshotNotification's expiresAt is forced
//           to a past timestamp — § 5.3.4 says that condition signals
//           "snapshot has been deleted" to the receiver.
//
extern void ldSnapshotNotify(LdSnapshotCacheItem* itemP, bool deleted);

#endif  // CORNGSILD_LDSNAPSHOTNOTIFY_H_
