#ifndef SWNGSILD_LDFORWARDING_OPS_H_
#define SWNGSILD_LDFORWARDING_OPS_H_

//
// FILE            ldForwarding.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Forwarding-plugin registry. The dispatcher looks up the plugin
// matching an endpoint URL's scheme; plugins register at broker
// startup.
//
#include <stdbool.h>                                   // bool

#include "swNgsild/LdForwarding.h"                     // LdForwardingPlugin



// -----------------------------------------------------------------------------
//
// ldForwardingRegister - add a plugin to the registry
//
// All schemes the plugin advertises become resolvable via
// ldForwardingForScheme(). Returns true on success, false if a scheme
// is already claimed by another plugin (registration refused — first
// plugin wins).
//
extern bool ldForwardingRegister(const LdForwardingPlugin* plugin);



// -----------------------------------------------------------------------------
//
// ldForwardingForScheme - look up the plugin handling a URL scheme
//
// scheme is matched case-insensitively against each plugin's schemes[]
// list. Returns NULL if no plugin claims the scheme.
//
extern const LdForwardingPlugin* ldForwardingForScheme(const char* scheme);



// -----------------------------------------------------------------------------
//
// ldForwardingForEndpoint - convenience: extract scheme from an endpoint URL
// and look up the plugin in one call. Returns NULL if endpoint is
// malformed (no "scheme:" prefix) or no plugin claims that scheme.
//
extern const LdForwardingPlugin* ldForwardingForEndpoint(const char* endpoint);

#endif  // SWNGSILD_LDFORWARDING_OPS_H_
