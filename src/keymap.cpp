#include "keymap.h"

namespace mcpekbm {

int hid_to_vk(long u) {
  if (u >= 0x04 && u <= 0x1D) return 'A' + (int)(u - 0x04);
  if (u >= 0x1E && u <= 0x26) return '1' + (int)(u - 0x1E);
  if (u >= 0x3A && u <= 0x45) return 112 + (int)(u - 0x3A);  // F1..F12
  switch (u) {
    case 0x27: return '0';
    case 0x28: return 13;   // Return
    case 0x29: return 27;   // Escape
    case 0x2A: return 8;    // Backspace
    case 0x2B: return 9;    // Tab
    case 0x2C: return 32;   // Space
    case 0x2D: return 189;  // -
    case 0x2E: return 187;  // =
    case 0x2F: return 219;  // [
    case 0x30: return 221;  // ]
    case 0x31: return 220;  // backslash
    case 0x33: return 186;  // ;
    case 0x34: return 222;  // '
    case 0x35: return 192;  // `
    case 0x36: return 188;  // ,
    case 0x37: return 190;  // .
    case 0x38: return 191;  // /
    case 0x39: return 20;   // Caps Lock
    case 0x49: return 45;   // Insert
    case 0x4A: return 36;   // Home
    case 0x4B: return 33;   // Page Up
    case 0x4C: return 46;   // Delete (forward)
    case 0x4D: return 35;   // End
    case 0x4E: return 34;   // Page Down
    case 0x4F: return 39;   // Right
    case 0x50: return 37;   // Left
    case 0x51: return 40;   // Down
    case 0x52: return 38;   // Up
    case 0xE0: case 0xE4: return 17;  // Ctrl
    case 0xE1: case 0xE5: return 16;  // Shift
    case 0xE2: case 0xE6: return 18;  // Alt / Option
    case 0xE3: return 91;             // Left Cmd
    case 0xE7: return 92;             // Right Cmd
    default: return 0;
  }
}

int ScrollAccumulator::feed(float v) {
  if ((v > 0 && acc < 0) || (v < 0 && acc > 0)) acc = 0;  // direction change
  acc += v;
  int n = 0;
  if (acc >= step) { n = 1; acc -= step; }
  else if (acc <= -step) { n = -1; acc += step; }
  if (acc >= step || acc <= -step) acc = 0;  // big wheel jumps: one notch per event
  return n;
}

}  // namespace mcpekbm
