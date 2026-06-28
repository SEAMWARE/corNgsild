#ifndef SWNGSILD_LD_URL_WILDCARD_CHECK_H_
#define SWNGSILD_LD_URL_WILDCARD_CHECK_H_

//
// FILE            ldUrlWildcardCheck.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Per-route wildcard validation, split in two:
//
//   * ldUrlWildcardOptionsInit(service)  — invoked once per SwRestService at
//     init time. Walks the URL pattern and stores per-slot validation bits
//     in service->options. Cached for the lifetime of the process.
//   * ldUrlWildcardCheck()               — pre-service hook. Reads the
//     cached bits and validates the actual wildcard values from the
//     incoming request. No URL re-scanning per request.
//
// The slot-validation flags live in the `wildcards` group of
// SwRestService.options (a SwRestServiceOptions bit-struct, see swRest).
//

#include <stdbool.h>

#include "swRest/SwRestService.h"

extern void ldUrlWildcardOptionsInit(SwRestService* service);
extern bool ldUrlWildcardCheck(void);

#endif  // SWNGSILD_LD_URL_WILDCARD_CHECK_H_
