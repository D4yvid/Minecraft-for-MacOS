#include "mac_keymap.h"

namespace mcfm {
namespace launcher {

int mac_keycode_to_vk(unsigned short k) {
  // macOS virtual key codes (Carbon kVK_*, stable since the 1990s) -> Windows VK codes.
  static const unsigned char letters[] = {  // kVK_ANSI_* order is not alphabetical
      'A', 'S', 'D', 'F', 'H', 'G', 'Z', 'X', 'C', 'V', 0, 'B', 'Q', 'W', 'E', 'R', 'Y', 'T'};
  if (k < sizeof letters) return letters[k];
  switch (k) {
    case 0x12: return '1'; case 0x13: return '2'; case 0x14: return '3'; case 0x15: return '4';
    case 0x17: return '5'; case 0x16: return '6'; case 0x1A: return '7'; case 0x1C: return '8';
    case 0x19: return '9'; case 0x1D: return '0';
    case 0x1F: return 'O'; case 0x20: return 'U'; case 0x22: return 'I'; case 0x23: return 'P';
    case 0x25: return 'L'; case 0x26: return 'J'; case 0x28: return 'K'; case 0x2D: return 'N';
    case 0x2E: return 'M';
    case 0x18: return 187; case 0x1B: return 189; case 0x1E: return 221; case 0x21: return 219;
    case 0x27: return 222; case 0x29: return 186; case 0x2A: return 220; case 0x2B: return 188;
    case 0x2C: return 191; case 0x2F: return 190; case 0x32: return 192;
    case 0x24: return 13; case 0x30: return 9; case 0x31: return 32; case 0x33: return 8;
    case 0x35: return 27; case 0x75: return 46;  // forward delete
    case 0x38: case 0x3C: return 16;  // shift
    case 0x3B: case 0x3E: return 17;  // control
    case 0x3A: case 0x3D: return 18;  // option
    case 0x37: case 0x36: return 91;  // command
    case 0x7B: return 37; case 0x7E: return 38; case 0x7C: return 39; case 0x7D: return 40;
    case 0x7A: return 112; case 0x78: return 113; case 0x63: return 114; case 0x76: return 115;
    case 0x60: return 116; case 0x61: return 117; case 0x62: return 118; case 0x64: return 119;
    case 0x65: return 120; case 0x6D: return 121; case 0x67: return 122; case 0x6F: return 123;
    case 0x52: return 96; case 0x53: return 97; case 0x54: return 98; case 0x55: return 99;
    case 0x56: return 100; case 0x57: return 101; case 0x58: return 102; case 0x59: return 103;
    case 0x5B: return 104; case 0x5C: return 105;
  }
  return 0;
}

}  // namespace launcher
}  // namespace mcfm
