/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Guidjlk */
#ifndef R2TK_ENVIRONMENT_H
#define R2TK_ENVIRONMENT_H

/* RPCS3 currently reports PID 1 and parent PID 0. This is a heuristic,
 * not a supported emulator-identification API. Use it only for UI advice;
 * unlock operations and backups must not depend on this result. */
static inline int environment_likely_rpcs3(int pid, int parent_pid) {
  return pid == 1 && parent_pid == 0;
}

#endif
