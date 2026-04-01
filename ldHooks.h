#ifndef SWNGSILD_LDHOOKS_H_
#define SWNGSILD_LDHOOKS_H_

//
// FILE            ldHooks.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//



// -----------------------------------------------------------------------------
//
// ldHooksRegister - register all NGSI-LD hooks with swRest
//
// Registers requestStart, parse, render, and param hooks.
// Called from ldInit().
//
extern void ldHooksRegister(void);

#endif  // SWNGSILD_LDHOOKS_H_
