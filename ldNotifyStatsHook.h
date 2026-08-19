#ifndef CORNGSILD_LDNOTIFYSTATSHOOK_H_
#define CORNGSILD_LDNOTIFYSTATSHOOK_H_

//
// FILE            ldNotifyStatsHook.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Optional callback fired each time a notification is POSTed — for
// the broker's metrics layer (Prometheus counters, etc.) to bump a
// success / failure counter without corNgsild depending on it.
//
// csrSub == true  → CSR-subscription notification (§ 5.11)
// csrSub == false → entity-subscription notification (§ 5.8)
// success         → HTTP 2xx reply received
//
#include <stdbool.h>                                // bool



typedef void (*LdNotifyStatsHook)(bool csrSub, bool success);



extern void ldNotifyStatsHookSet(LdNotifyStatsHook fn);
extern void ldNotifyStatsHookInvoke(bool csrSub, bool success);

#endif  // CORNGSILD_LDNOTIFYSTATSHOOK_H_
