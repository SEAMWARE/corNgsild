#ifndef CORNGSILD_LDPARAMSVALIDATE_H_
#define CORNGSILD_LDPARAMSVALIDATE_H_

//
// FILE            ldParamsValidate.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//

// -----------------------------------------------------------------------------
//
// ldParamsValidate - validate cross-parameter constraints on URL params
//
// Returns true if an error was detected (error response already set).
// Returns false if all params are valid.
//
extern bool ldParamsValidate(void);

#endif  // CORNGSILD_LDPARAMSVALIDATE_H_
