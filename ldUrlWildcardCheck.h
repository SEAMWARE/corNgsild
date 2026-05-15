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
// The options bitmap is opaque to swRest; the bit constants live in this
// header and stay inside the swNgsild layer.
//

#include <stdbool.h>

#include "swRest/SwRestService.h"

// Slot-validation bits stored in SwRestService.options.
#define LD_WC_URI_AT_0    (1ULL << 0)   // wildcard[0] must be a valid URI (entity/sub/reg/entityMap id)
#define LD_WC_NAME_AT_1   (1ULL << 1)   // wildcard[1] must be a valid NCName/URI (§ 4.6.2 attr name)
#define LD_WC_URI_AT_2    (1ULL << 2)   // wildcard[2] must be a valid URI (instance id)

extern void ldUrlWildcardOptionsInit(SwRestService* service);
extern bool ldUrlWildcardCheck(void);

#endif  // SWNGSILD_LD_URL_WILDCARD_CHECK_H_
