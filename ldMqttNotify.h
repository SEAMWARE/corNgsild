#ifndef CORNGSILD_LDMQTTNOTIFY_H_
#define CORNGSILD_LDMQTTNOTIFY_H_

//
// FILE            ldMqttNotify.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// MQTT notification delivery for the Coraine stack — § 7 of NGSI-LD v1.9.1.
//
// Backed by libmosquitto. The broker is the MQTT *client*: it connects
// to the subscriber-supplied broker, publishes one message, and
// disconnects. The subscriber's `endpoint.uri` carries the broker host,
// port, and topic; QoS / MQTT version come from `endpoint.notifierInfo`.
//
// URI format (§ 7.2):
//   mqtt[s]://[<user>[:<pass>]@]<host>[:<port>]/<topic>[/<subtopic>]*
//
// MQTT message body (§ 7.2):
//   { "metadata": { Content-Type, Link, ...receiverInfo },
//     "body":     { ...the Notification... } }
//
#include <stdbool.h>                                     // bool

#include "kjson/KjNode.h"                                // KjNode



// -----------------------------------------------------------------------------
//
// ldMqttInit / ldMqttCleanup - global libmosquitto setup / teardown.
//
// ldMqttInit must be called once at broker startup, after the Cor-Libs are
// initialised and before any notifications are dispatched.
//
extern int  ldMqttInit(void);
extern void ldMqttCleanup(void);

// Accept a self-signed cert on mqtts:// endpoints (set under --insecureNotif).
extern void ldMqttTlsInsecureSet(bool onoff);



// -----------------------------------------------------------------------------
//
// ldIsMqttUri - true if `uri` starts with "mqtt://" or "mqtts://".
//
extern bool ldIsMqttUri(const char* uri);



// -----------------------------------------------------------------------------
//
// ldMqttNotify - publish one notification to a subscriber's MQTT broker.
//
// notifBodyJson  - the rendered Notification JSON to put under "body".
// contentType    - "application/json" or "application/ld+json".
// linkHeader     - RFC 5988 Link header value (NULL allowed).
// receiverInfo   - KjArray of {key, value} pairs to copy into "metadata".
// notifierInfo   - KjArray of {key, value} pairs; honours MQTT-QoS, MQTT-Version.
//
// Returns true on success. On failure, ldError is NOT set — the caller
// should bump failure counters as it does for HTTP failures.
//
extern bool ldMqttNotify(const char* uri,
                         const char* notifBodyJson,
                         const char* contentType,
                         const char* linkHeader,
                         KjNode*     receiverInfo,
                         KjNode*     notifierInfo);

#endif  // CORNGSILD_LDMQTTNOTIFY_H_
