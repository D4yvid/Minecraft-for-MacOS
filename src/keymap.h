#pragma once

namespace mcpekbm {

// USB HID keyboard usage (== GCKeyCode value) -> Windows virtual-key code.
// Returns 0 for keys the game has no use for.
int hid_to_vk(long hidUsage);

// Turns scroll input of any granularity (trackpad deltas, wheel notches) into
// whole hotbar steps. A change of direction drops the stale remainder.
struct ScrollAccumulator {
  float step;
  float acc;
  int feed(float v);
};

}  // namespace mcpekbm
