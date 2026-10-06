#pragma once

namespace mcpekbm {

// USB HID keyboard usage (== GCKeyCode value) -> Windows virtual-key code.
// Returns 0 for keys the game has no use for.
int hid_to_vk(long hidUsage);

// Turns scroll input of any granularity (trackpad deltas, wheel notches) into
// whole hotbar steps. A change of direction or a pause drops the stale remainder,
// and notches are rate-limited so trackpad momentum cannot spin the hotbar.
struct ScrollAccumulator {
  float step;
  float acc = 0;
  double lastEvent = -1e9, lastNotch = -1e9;
  int feed(float v, double nowSec);
};

}  // namespace mcpekbm
