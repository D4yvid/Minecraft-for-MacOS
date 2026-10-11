#include "ios_keymap.h"

namespace mcfm {
namespace launcher {

int ios_hid_to_vk(int u) {
  if (u >= 0x04 && u <= 0x1D) return 'A' + (u - 0x04);   // a..z
  if (u >= 0x1E && u <= 0x26) return '1' + (u - 0x1E);  // 1..9
  if (u >= 0x3A && u <= 0x45) return 0x70 + (u - 0x3A);  // F1..F12
  if (u >= 0x59 && u <= 0x61) return 0x61 + (u - 0x59);  // keypad 1..9
  switch (u) {
    case 0x27: return '0';
    case 0x62: return 0x60;  // keypad 0
    case 0x28: return 0x0D;  // Return
    case 0x58: return 0x0D;  // keypad Enter
    case 0x29: return 0x1B;  // Escape
    case 0x2A: return 0x08;  // Backspace
    case 0x2B: return 0x09;  // Tab
    case 0x2C: return 0x20;  // Space
    case 0x2D: return 0xBD;  // -
    case 0x2E: return 0xBB;  // =
    case 0x2F: return 0xDB;  // [
    case 0x30: return 0xDD;  // ]
    case 0x31: return 0xDC;  // backslash
    case 0x33: return 0xBA;  // ;
    case 0x34: return 0xDE;  // '
    case 0x35: return 0xC0;  // `
    case 0x36: return 0xBC;  // ,
    case 0x37: return 0xBE;  // .
    case 0x38: return 0xBF;  // /
    case 0x39: return 0x14;  // Caps Lock
    case 0x49: return 0x2D;  // Insert
    case 0x4A: return 0x24;  // Home
    case 0x4B: return 0x21;  // Page Up
    case 0x4C: return 0x2E;  // Delete forward
    case 0x4D: return 0x23;  // End
    case 0x4E: return 0x22;  // Page Down
    case 0x4F: return 0x27;  // Right
    case 0x50: return 0x25;  // Left
    case 0x51: return 0x28;  // Down
    case 0x52: return 0x26;  // Up
    case 0xE0: case 0xE4: return 0x11;  // Control
    case 0xE1: case 0xE5: return 0x10;  // Shift
    case 0xE2: case 0xE6: return 0x12;  // Option (Alt)
    default: return 0;
  }
}

}  // namespace launcher
}  // namespace mcfm
