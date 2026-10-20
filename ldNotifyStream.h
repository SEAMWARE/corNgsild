#ifndef CORNGSILD_LDNOTIFYSTREAM_H_
#define CORNGSILD_LDNOTIFYSTREAM_H_
//
// FILE            ldNotifyStream.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Notifications on an event stream - a Subscription whose notification.endpoint.uri is
// "urn:ngsi-ld:self" is the response to the POST that made it (server-sent events, coraine doc/sse.md).
//
// Not a transport (ldNotifyTransport.h): there is no address to send to. The rendered Notification is
// handed to the application's hook, which writes it on the stream, and the counters say what
// reached a reader: timesSent, lastNotification and lastSuccess move when the application says an event
// was written on a stream (ldNotifyStreamSent), not when it was handed over. A hook that could not keep
// it (out of memory) is a failure, as for any notification.
//
// Built with COR_WITH_EVENT_STREAM=1 (the default); 0 leaves the code out: the endpoint is refused as an
// unknown scheme, and an application calling these does not link.
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint64_t

#include "corNgsild/LdSubCache.h"                        // LdSubCache



// -----------------------------------------------------------------------------
//
// LdNotifyStreamPut - keep one rendered Notification for the subscription's stream; false: not kept
//
// tenantP - the subscription's tenant (corNgsild.tenantP, the broker's type). csrSub - a context source
// subscription's (§ 5.11). json - the Notification, one line.
//
typedef bool (*LdNotifyStreamPut)(void* tenantP, const char* subId, bool csrSub, const char* json);



// -----------------------------------------------------------------------------
//
// ldNotifyStreamSet - install the application's hook (once, at startup); none: the endpoint is refused
//
extern void ldNotifyStreamSet(LdNotifyStreamPut putFn);



// -----------------------------------------------------------------------------
//
// ldNotifyIsEventStream - "urn:ngsi-ld:self"
//
extern bool ldNotifyIsEventStream(const char* uri);



// -----------------------------------------------------------------------------
//
// ldNotifyStreamOn - a hook is installed: the endpoint may be used
//
extern bool ldNotifyStreamOn(void);



// -----------------------------------------------------------------------------
//
// ldNotifyStreamHand - the Notification to the hook, nothing counted: the caller counts - the periodic
// loop (ldPernotLoop), whose counters are its own. false: not kept.
//
extern bool ldNotifyStreamHand(void* tenantP, const char* subId, bool csrSub, const char* json);



// -----------------------------------------------------------------------------
//
// ldNotifyStreamPut - the Notification to the hook (the counters of a failure updated here)
//
extern void ldNotifyStreamPut(LdSubCacheItem* itemP, bool csrSub, const char* json, uint64_t now);



// -----------------------------------------------------------------------------
//
// ldNotifyStreamSent - 'n' events of the subscription written on its stream, at 'when' (epoch ns):
// timesSent, lastNotification, lastSuccess. From any thread; a subscription no longer in the cache is
// left alone.
//
extern void ldNotifyStreamSent(LdSubCache* cacheP, const char* subId, bool csrSub, int n, uint64_t when);

#endif  // CORNGSILD_LDNOTIFYSTREAM_H_
