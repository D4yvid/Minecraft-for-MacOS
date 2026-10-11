// Hardware keyboards on iOS (ios/launcher/ios_keymap.cpp): UIKeyboardHIDUsage -> the Windows
// virtual key the engine's Keyboard uses (as the Mac and Android keymaps). Host test.
#include <cstdio>

#include "ios_keymap.h"

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

using mcfm::launcher::ios_hid_to_vk;

int main() {
  for (int i = 0; i < 26; i++) EXPECT(ios_hid_to_vk(0x04 + i) == 'A' + i);  // a..z
  for (int i = 0; i < 9; i++) EXPECT(ios_hid_to_vk(0x1E + i) == '1' + i);   // 1..9
  EXPECT(ios_hid_to_vk(0x27) == '0');
  EXPECT(ios_hid_to_vk(0x28) == 0x0D);  // Return
  EXPECT(ios_hid_to_vk(0x29) == 0x1B);  // Escape
  EXPECT(ios_hid_to_vk(0x2A) == 0x08);  // Backspace
  EXPECT(ios_hid_to_vk(0x2B) == 0x09);  // Tab
  EXPECT(ios_hid_to_vk(0x2C) == 0x20);  // Space
  EXPECT(ios_hid_to_vk(0x4F) == 0x27 && ios_hid_to_vk(0x50) == 0x25 &&
         ios_hid_to_vk(0x51) == 0x28 && ios_hid_to_vk(0x52) == 0x26);  // right, left, down, up
  EXPECT(ios_hid_to_vk(0xE1) == 0x10 && ios_hid_to_vk(0xE5) == 0x10);  // shifts
  EXPECT(ios_hid_to_vk(0xE0) == 0x11 && ios_hid_to_vk(0xE4) == 0x11);  // controls
  EXPECT(ios_hid_to_vk(0xE2) == 0x12 && ios_hid_to_vk(0xE6) == 0x12);  // options (alt)
  for (int i = 0; i < 12; i++) EXPECT(ios_hid_to_vk(0x3A + i) == 0x70 + i);  // F1..F12
  EXPECT(ios_hid_to_vk(0x00) == 0 && ios_hid_to_vk(0x200) == 0 && ios_hid_to_vk(-1) == 0);
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("ios_keymap_test: all passed\n");
  return 0;
}
