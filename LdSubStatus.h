#ifndef CORNGSILD_LD_SUB_STATUS_H_
#define CORNGSILD_LD_SUB_STATUS_H_

//
// FILE            LdSubStatus.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

// -----------------------------------------------------------------------------
//
// LdSubStatus - subscription lifecycle status (§ 5.2.12)
//
// The wire form is a string ("active"/"paused"/"expired"/"failed"); the
// cache keeps the enum so status checks in the notification hot paths are
// integer compares, not strcmps.
//
typedef enum LdSubStatus
{
  LdSubStatusActive = 0,
  LdSubStatusPaused,
  LdSubStatusExpired,
  LdSubStatusFailed
} LdSubStatus;

extern LdSubStatus  ldSubStatusFromString(const char* s);
extern const char*  ldSubStatusToString(LdSubStatus status);

#endif
