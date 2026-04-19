#ifndef SWNGSILD_LD_PROBE_SOURCE_IDENTITY_H_
#define SWNGSILD_LD_PROBE_SOURCE_IDENTITY_H_

//
// FILE            ldProbeSourceIdentity.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Probe a Context Source's /info/sourceIdentity endpoint (§ 5.15 /
// § 6.33) and return its contextSourceAlias. Used at CSR-creation time
// to learn the target's Via pseudonym so dispatch can skip CSRs whose
// alias is already in the incoming Via chain (§ 5.12 match rule:
// "no registration shall match if the CSourceRegistration
// contextSourceAlias can be found within the listing of previously
// encountered Context Sources").
//

#include <stdbool.h>                                    // bool



// -----------------------------------------------------------------------------
//
// ldProbeSourceIdentity - GET <endpoint>/info/sourceIdentity
//
// endpoint    Full base URL of the CSR endpoint (host+port, no path).
// tenant      NGSILD-Tenant header to send. NULL / empty = default tenant.
// timeoutMs   Request timeout in milliseconds. 0 = plugin default.
//
// Returns a strdup'd contextSourceAlias string on success (caller owns:
// free() when done). Returns NULL on any failure (transport error, non-
// 2xx, 501 Not Implemented, malformed body, missing field). Failures
// are silent — callers fall back to reactive Via-based loop detection.
//
extern char* ldProbeSourceIdentity(const char* endpoint,
                                   const char* tenant,
                                   int         timeoutMs);

#endif  // SWNGSILD_LD_PROBE_SOURCE_IDENTITY_H_
