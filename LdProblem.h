//
// FILE            LdProblem.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#ifndef LD_PROBLEM_H
#define LD_PROBLEM_H



// -----------------------------------------------------------------------------
//
// Error type URIs (RFC 7807)
//
#define LD_ERROR_BAD_REQUEST_DATA   "https://uri.etsi.org/ngsi-ld/errors/BadRequestData"
#define LD_ERROR_RESOURCE_NOT_FOUND "https://uri.etsi.org/ngsi-ld/errors/ResourceNotFound"
#define LD_ERROR_ALREADY_EXISTS     "https://uri.etsi.org/ngsi-ld/errors/AlreadyExists"
#define LD_ERROR_CONFLICT           "https://uri.etsi.org/ngsi-ld/errors/Conflict"
#define LD_ERROR_OP_NOT_SUPPORTED   "https://uri.etsi.org/ngsi-ld/errors/OperationNotSupported"
#define LD_ERROR_INVALID_REQUEST    "https://uri.etsi.org/ngsi-ld/errors/InvalidRequest"
#define LD_ERROR_INTERNAL_ERROR     "https://uri.etsi.org/ngsi-ld/errors/InternalError"
// TS 104-176 § 6.3.18 names only the HTTP status "508 Loop Detected"; no ProblemDetails
// type is registered, so this descriptive URI fills the RFC 7807 'type' slot.
#define LD_ERROR_LOOP_DETECTED      "https://uri.etsi.org/ngsi-ld/errors/LoopDetected"
#define LD_ERROR_NONEXISTENT_TENANT      "https://uri.etsi.org/ngsi-ld/errors/NonexistentTenant"
#define LD_ERROR_LD_CONTEXT_NOT_AVAILABLE "https://uri.etsi.org/ngsi-ld/errors/LdContextNotAvailable"

#endif
