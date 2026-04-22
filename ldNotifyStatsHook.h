#ifndef SWNGSILD_LDNOTIFYSTATSHOOK_H_
#define SWNGSILD_LDNOTIFYSTATSHOOK_H_

//
// FILE            ldNotifyStatsHook.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Optional callback fired each time a notification is POSTed — for
// the broker's metrics layer (Prometheus counters, etc.) to bump a
// success / failure counter without swNgsild depending on it.
//
// csrSub == true  → CSR-subscription notification (§ 5.11)
// csrSub == false → entity-subscription notification (§ 5.8)
// success         → HTTP 2xx reply received
//
#include <stdbool.h>                                // bool



typedef void (*LdNotifyStatsHook)(bool csrSub, bool success);



extern void ldNotifyStatsHookSet(LdNotifyStatsHook fn);
extern void ldNotifyStatsHookInvoke(bool csrSub, bool success);

#endif  // SWNGSILD_LDNOTIFYSTATSHOOK_H_
