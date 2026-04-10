#ifndef SWNGSILD_LDPARAMSVALIDATE_H_
#define SWNGSILD_LDPARAMSVALIDATE_H_

//
// FILE            ldParamsValidate.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

// -----------------------------------------------------------------------------
//
// ldParamsValidate - validate cross-parameter constraints on URL params
//
// Returns true if an error was detected (error response already set).
// Returns false if all params are valid.
//
extern bool ldParamsValidate(void);

#endif  // SWNGSILD_LDPARAMSVALIDATE_H_
