#ifndef CORNGSILD_LDFORWARDING_H_
#define CORNGSILD_LDFORWARDING_H_

//
// FILE            LdForwarding.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Generic forwarding-transport abstraction for distributed operations
// (NGSI-LD § 4.3.6). The dispatcher in corNgsild builds an
// LdForwardRequest, looks up the plugin matching the endpoint URL's
// scheme, and calls send(). HTTP is the default transport (built into
// coraine); future transports (e.g. a binary RPC scheme like
// "corBin://host:port") plug in by registering for their own scheme.
//
// The interface is deliberately transport-agnostic: CorRestVerb /
// CorRestKeyValue are reused as semantic carriers (verb = create/read/
// update/delete-shaped op, key-value = string headers), but no
// HTTP-specific field leaks across the boundary.
//
#include <stdbool.h>                                   // bool

#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corTree/CorNode.h"                           // CorNode
#include "corRest/CorRestVerb.h"                         // CorRestVerb
#include "corRest/CorRestKeyValue.h"                     // CorRestKeyValue



// -----------------------------------------------------------------------------
//
// LdForwardRequest - what the dispatcher hands to a forwarding plugin
//
typedef struct LdForwardRequest
{
  const char*      endpoint;          // full URL incl. scheme (http://h/p, corBin://h:p, ...)
  CorRestVerb       verb;              // semantic op shape (GET/POST/PATCH/PUT/DELETE)
  CorRestKeyValue*  headerV;           // outbound headers (Via, NGSILD-Tenant, contextSourceInfo-derived, ...)
  int              headerCount;
  const char*      body;              // request body (NULL for GET / DELETE)
  int              bodyLen;
  int              connectTimeoutMs;  // 0 = plugin default
  int              requestTimeoutMs;  // 0 = plugin default — comes from RegistrationManagementInfo.timeout
} LdForwardRequest;



// -----------------------------------------------------------------------------
//
// LdForwardResponse - the upstream reply, normalized
//
// Headers + body lifetime is bound to allocP (the caller-provided arena
// the plugin allocates into). Caller must keep allocP alive while it
// references any field in this struct.
//
typedef struct LdForwardResponse
{
  int              statusCode;        // HTTP-style status (200/201/204/400/404/500/...)
  CorRestKeyValue*  headerV;           // borrowed into allocP
  int              headerCount;
  char*            body;              // borrowed into allocP
  int              bodyLen;
  CorNode*         bodyTree;          // a transport that carries trees (cor://) answers with this instead of body - in allocP
  CorAlloc*        allocP;            // arena that headers + body live in
  int              error;             // 0 = transport ok (statusCode is meaningful); nonzero = transport-level failure
  char             errorDetail[256];  // human-readable transport error
} LdForwardResponse;



// -----------------------------------------------------------------------------
//
// LdForwardSendFunc - the plugin's send entry point
//
// Returns 0 on transport success (statusCode is the upstream status) or
// nonzero on transport failure (errorDetail set, statusCode undefined).
//
typedef int (*LdForwardSendFunc)(LdForwardRequest* req, LdForwardResponse* resp);



// -----------------------------------------------------------------------------
//
// LdForwardStartFunc / LdForwardWaitFunc - a request sent now, its response collected later
//
// For a fan-out: every request started before any is waited for, so they are in flight at the same
// time. start returns a handle, or NULL with errorDetail filled; wait fills resp as send does (and
// returns as send does) and frees the handle - every handle must be waited for, once. kaP is the
// caller's: the request is built in it; resp->allocP is where the response goes.
//
typedef void* (*LdForwardStartFunc)(LdForwardRequest* req, CorAlloc* kaP, char* errorDetail, int errorDetailSize);
typedef int   (*LdForwardWaitFunc)(void* handle, LdForwardResponse* resp);



// -----------------------------------------------------------------------------
//
// LdForwardingPlugin - one registered transport
//
// schemes[] is a NULL-terminated string array of URL schemes the plugin
// handles (e.g. {"http","https",NULL}). The same plugin may serve
// multiple schemes (HTTP/HTTPS share a single implementation).
//
typedef struct LdForwardingPlugin
{
  const char*         alias;        // short name, "http" / "corBin" — for diagnostics
  const char* const*  schemes;      // NULL-terminated list of URL schemes
  LdForwardSendFunc   send;
  LdForwardStartFunc  start;        // optional, with wait: a fan-out starts every request before it waits for any
  LdForwardWaitFunc   wait;
} LdForwardingPlugin;

#endif  // CORNGSILD_LDFORWARDING_H_
