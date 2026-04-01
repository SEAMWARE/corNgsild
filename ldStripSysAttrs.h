//
// FILE            ldStripSysAttrs.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#ifndef LD_STRIP_SYSATTRS_H
#define LD_STRIP_SYSATTRS_H

#include "kjson/KjNode.h"                           // KjNode

// -----------------------------------------------------------------------------
//
// ldStripSysAttrs - remove createdAt/modifiedAt from entity tree (recursively)
//
extern void ldStripSysAttrs(KjNode* treeP);

#endif
