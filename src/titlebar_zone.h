#pragma once

namespace mcpekbm {

// Window-space points (origin top-left) where hovering reveals the traffic lights.
inline bool in_titlebar_hot_zone(double x, double y) {
  return x >= 0 && y >= 0 && x < 80 && y < 30;
}

}  // namespace mcpekbm
