#ifndef SWNGSILD_LD_ACCEPT_PARSE_H_
#define SWNGSILD_LD_ACCEPT_PARSE_H_

//
// FILE            ldAcceptParse.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// q-weighted Accept-header parsing for the three NGSI-LD media types.
//
// Highest q wins. Equal q → first-listed in the Accept header wins.
// q=0 means "not acceptable" — that media type is excluded.
//
// "*/*" and "application/*" wildcards count as q for json (the spec
// default representation), with their q applied uniformly.
//
// Return values:
//   - NULL or empty Accept           → LdAcceptJson (§ 6.3.4: default)
//   - one of the three is acceptable → corresponding type, highest q wins
//   - none of the three is acceptable → LdAcceptNone  (§ 6.3.4: 406)
//
// Note: geo+json by itself counts as "acceptable" here. The route layer
// must enforce the spec's "geo+json-only is 406 outside Retrieve/Query
// Entity" rule; this helper just reports the q-winner.
//
typedef enum LdAcceptType
{
  LdAcceptNone   = -1,    // none of json/ld+json/geo+json is acceptable
  LdAcceptJson   = 0,     // application/json (default)
  LdAcceptLdJson,         // application/ld+json
  LdAcceptGeoJson         // application/geo+json
} LdAcceptType;



extern LdAcceptType ldAcceptParse(const char* acceptHeader);

#endif
