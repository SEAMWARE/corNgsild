#ifndef CORNGSILD_LDNOTIFYTRANSPORT_H_
#define CORNGSILD_LDNOTIFYTRANSPORT_H_

//
// FILE            ldNotifyTransport.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Notification delivery to anything that is not HTTP - mqtt://, mqtts://, and whatever comes next.
//
// This library carries no transport but HTTP. A Subscription whose notification.endpoint.uri has
// another scheme is delivered through a hook the APPLICATION sets - in coraine, the bridge plugin
// that claims the scheme (mqtt.so for mqtt/mqtts). What stays here is what NGSI-LD itself says:
// the message, which for a non-HTTP binding is the TS 104 243 envelope
//
//   { "metadata": { "Content-Type": ..., "Link": ..., ...receiverInfo }, "body": <the Notification> }
//
// and the transport settings, notifierInfo (MQTT-QoS, MQTT-Version), passed on as they are.
//
#include <stdbool.h>                                     // bool

#include "corTree/CorNode.h"                             // CorNode



// -----------------------------------------------------------------------------
//
// LdNotifyTransportHas - does a loaded transport deliver to this URI's scheme?
//
typedef bool (*LdNotifyTransportHas)(const char* uri);



// -----------------------------------------------------------------------------
//
// LdNotifyTransportSend - deliver one message; true when it was handed over (or acknowledged)
//
// notifierInfoJson - notification.endpoint.notifierInfo as JSON text, or NULL
//
typedef bool (*LdNotifyTransportSend)(const char* uri, const char* payload, const char* notifierInfoJson);



// -----------------------------------------------------------------------------
//
// ldNotifyTransportSet - install the application's hooks (once, at startup)
//
// With none installed, no non-HTTP scheme is supported: such a Subscription is refused.
//
extern void ldNotifyTransportSet(LdNotifyTransportHas hasFn, LdNotifyTransportSend sendFn);



// -----------------------------------------------------------------------------
//
// ldNotifyIsHttp - "http://" or "https://"
//
extern bool ldNotifyIsHttp(const char* uri);



// -----------------------------------------------------------------------------
//
// ldNotifyTransportHas - a non-HTTP URI some loaded transport delivers to
//
extern bool ldNotifyTransportHas(const char* uri);



// -----------------------------------------------------------------------------
//
// ldNotifyTransportSend - wrap a rendered Notification in the binding envelope and deliver it
//
// notifBodyJson - the rendered Notification, put under "body"
// contentType   - the notification's media type, under metadata "Content-Type"
// linkHeader    - the Link to the @context (NULL allowed), under metadata "Link"
// receiverInfo  - CorArray of {key, value}, each copied into "metadata"
// notifierInfo  - CorArray of {key, value}, handed to the transport as JSON text
//
// Returns true on success; failures are counted by the caller, as for HTTP.
//
extern bool ldNotifyTransportSend(const char* uri,
                                  const char* notifBodyJson,
                                  const char* contentType,
                                  const char* linkHeader,
                                  CorNode*    receiverInfo,
                                  CorNode*    notifierInfo);

#endif  // CORNGSILD_LDNOTIFYTRANSPORT_H_
