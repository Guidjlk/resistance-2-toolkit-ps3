/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Guidjlk */
#ifndef R2TK_NAVIGATION_H
#define R2TK_NAVIGATION_H
#include <stdint.h>

typedef struct {
  int direction;
  uint64_t next_repeat;
} NavigationRepeat;

/* Move once on press, then repeat only navigation after a short delay.
 * Scheduling from now avoids a burst of moves after a slow frame. Passing
 * zero releases the hold, including while a confirmation or credits is open. */
static inline int navigation_move(NavigationRepeat *state, int direction,
                                  uint64_t now) {
  if (!direction) {
    state->direction = 0;
    state->next_repeat = 0;
    return 0;
  }
  if (direction != state->direction) {
    state->direction = direction;
    state->next_repeat = now + 400000;
    return direction;
  }
  if (now >= state->next_repeat) {
    state->next_repeat = now + 110000;
    return direction;
  }
  return 0;
}
#endif
