#ifndef CORNGSILD_LDRECEIVERINFOHEADERS_H_
#define CORNGSILD_LDRECEIVERINFOHEADERS_H_

//
// FILE            ldReceiverInfoHeaders.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool

#include "corTree/CorNode.h"                             // CorNode
#include "corRest/corRestClient.h"                       // CorRestClientRequest



// -----------------------------------------------------------------------------
//
// ldReceiverInfoHeadersAdd - notification.endpoint.receiverInfo (§ 5.2.15) as the headers of an outgoing notification
//
// Each {key, value} of the array is a request header (Authorization, X-*, ...), except the ones the
// broker emits itself, once:
//   Content-Type, an @context Link  - absorbed into the subscription's endpointAccept / contextUrl
//   Prefer                          - a rendering directive for the broker (§ 6.5.2), not for the receiver
//   NGSILD-Tenant, NGSILD-Snapshot  - broker-managed (§ 6.4.8 / § 6.4.9: "cannot be overridden")
//
// A value "urn:ngsi-ld:request" (§ 4.3.6.5) takes the same-named header of the request that triggered
// the notification - with fromRequest true, on the thread that serves it. With fromRequest false (no
// triggering request on this thread: a periodic tick, a notification re-dispatched from a Context
// Source) the entry is left out. receiverInfo NULL or not an array: nothing.
//
extern void ldReceiverInfoHeadersAdd(CorRestClientRequest* reqP, CorNode* receiverInfo, bool fromRequest);

#endif  // CORNGSILD_LDRECEIVERINFOHEADERS_H_
