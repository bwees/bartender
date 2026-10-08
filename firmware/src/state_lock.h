#pragma once

#include <cstdint>

// The library and config are written by the HTTP task on core 0 and read by the
// HMI on core 1. Without this, a recipe edit from the portal can rewrite the
// vector the menu is walking. Every access to either document takes the guard.
class StateGuard {
 public:
  StateGuard();
  ~StateGuard();
  StateGuard(const StateGuard&) = delete;
  StateGuard& operator=(const StateGuard&) = delete;
};

// Bumped on every accepted write. The HMI compares it to decide when to rebuild,
// which is what makes a portal edit appear on the screen without a reboot.
uint32_t stateRevision();
void bumpStateRevision();
