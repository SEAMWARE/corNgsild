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



// -----------------------------------------------------------------------------
//
// ldAcceptPrecondition - § 6.2.2 Accept-header content-negotiation precondition
//
// Call from the pre-service hook, before the service routine. Returns true when
// the Accept header is acceptable; on an unacceptable Accept it sets a 406 (with
// the available-representations list) and returns false, so the caller aborts
// dispatch and the operation is never performed (no write side effect).
//
extern bool ldAcceptPrecondition(void);

#endif  // SWNGSILD_LDHOOKS_H_
