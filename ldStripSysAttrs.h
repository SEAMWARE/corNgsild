//
// FILE            ldStripSysAttrs.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#ifndef LD_STRIP_SYSATTRS_H
#define LD_STRIP_SYSATTRS_H

#include "corTree/CorNode.h"                        // CorNode

// -----------------------------------------------------------------------------
//
// ldStripSysAttrs - remove createdAt/modifiedAt from entity tree (recursively)
//
extern void ldStripSysAttrs(CorNode* treeP);

#endif
