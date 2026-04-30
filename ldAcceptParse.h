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
// q=0 means "not acceptable" — that media type is excluded. If none of
// the three is acceptable, returns LdAcceptJson (the spec default).
//
// "*/*" and "application/*" wildcards count as q for json (the spec
// default representation), with their q applied uniformly.
//
typedef enum LdAcceptType
{
  LdAcceptJson = 0,    // application/json (default)
  LdAcceptLdJson,      // application/ld+json
  LdAcceptGeoJson      // application/geo+json
} LdAcceptType;



extern LdAcceptType ldAcceptParse(const char* acceptHeader);

#endif
