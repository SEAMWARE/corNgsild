#ifndef CORNGSILD_LDTRACELEVELS_H_
#define CORNGSILD_LDTRACELEVELS_H_

//
// FILE            ldTraceLevels.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
//
// Trace levels for corNgsild (range 200-399)
//
#define LdTInit       200   // Library initialization
#define LdTTypes      201   // Type conversions
#define LdTDetect     202   // Attribute type detection
#define LdTCheckEnt   203   // Entity validation
#define LdTCheckAttr  204   // Attribute validation
#define LdTCheckSub   205   // Subscription validation
#define LdTCheckReg   206   // Registration validation
#define LdTCheckGeo   207   // GeoJSON validation
#define LdTCheckDt    208   // DateTime validation
#define LdTCheckUri   209   // URI validation
#define LdTRender     210   // Format conversion

//
// Forwarding / distributed operations (220-229).
//
#define LdTFwdReq        220  // forwarded request — request line (verb + URL path)
#define LdTFwdReqParam   221  // forwarded request — URL params (one line per param)
#define LdTFwdReqHeader  222  // forwarded request — HTTP request headers (one per line)
#define LdTFwdReqBody    223  // forwarded request — request body
#define LdTFwdRes        224  // forwarded request — response line (status)
#define LdTFwdResHeader  225  // forwarded request — HTTP response headers (one per line)
#define LdTFwdResBody    226  // forwarded request — response body

//
// Notifications (230-239).
//
#define LdTRegMatch      227  // registration matching — why a registration did or did not match,
                              // and why a matched one was still not forwarded to
#define LdTNotifReq      230  // notification — request line (verb + URL)
#define LdTNotifReqParam 231  // notification — URL params (one line per param)
#define LdTNotifHeader   232  // notification — HTTP headers (one per line)
#define LdTNotifBody     233  // notification — body
#define LdTNotifRes      234  // notification — response line (status)

#define LdTExpiry        240  // transient Entity — lazily removed after a read found it expired

#endif  // CORNGSILD_LDTRACELEVELS_H_
