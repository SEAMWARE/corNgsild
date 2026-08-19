#ifndef CORNGSILD_LD_REQUEST_SUBSTITUTE_H_
#define CORNGSILD_LD_REQUEST_SUBSTITUTE_H_

//
// FILE            ldRequestSubstitute.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// § 4.3.6.5 / § 6.3.18 — when a contextSourceInfo or
// notification.endpoint.receiverInfo entry has value "urn:ngsi-ld:request",
// the broker substitutes the value of the same-named HTTP header on
// the triggering request. Returns the verbatim value otherwise, or
// NULL when the request had no such header.
//
// MUST be called only from a request-handling thread (uses corRest.in).
//

extern const char* ldRequestSubstitute(const char* key, const char* value);

#endif
