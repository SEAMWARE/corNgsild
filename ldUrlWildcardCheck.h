#ifndef CORNGSILD_LD_URL_WILDCARD_CHECK_H_
#define CORNGSILD_LD_URL_WILDCARD_CHECK_H_

//
// FILE            ldUrlWildcardCheck.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Per-route wildcard validation, split in two:
//
//   * ldUrlWildcardOptionsInit(service)  — invoked once per CorRestService at
//     init time. Walks the URL pattern and stores per-slot validation bits
//     in service->options. Cached for the lifetime of the process.
//   * ldUrlWildcardCheck()               — pre-service hook. Reads the
//     cached bits and validates the actual wildcard values from the
//     incoming request. No URL re-scanning per request.
//
// The slot-validation flags live in the `wildcards` group of
// CorRestService.options (a CorRestServiceOptions bit-struct, see corRest).
//

#include <stdbool.h>

#include "corRest/CorRestService.h"

extern void ldUrlWildcardOptionsInit(CorRestService* service);
extern bool ldUrlWildcardCheck(void);

#endif  // CORNGSILD_LD_URL_WILDCARD_CHECK_H_
