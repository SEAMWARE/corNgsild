#ifndef SWNGSILD_LDTRACELEVELS_H_
#define SWNGSILD_LDTRACELEVELS_H_

//
// FILE            ldTraceLevels.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
//
// Trace levels for swNgsild (range 200-399)
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

#endif  // SWNGSILD_LDTRACELEVELS_H_
