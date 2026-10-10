#include "mac_keymap.h"
#include <cstdio>
static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main() {
  using mcfm::launcher::mac_keycode_to_vk;
  EXPECT(mac_keycode_to_vk(0x00) == 'A');   // kVK_ANSI_A
  EXPECT(mac_keycode_to_vk(0x0D) == 'W');   // kVK_ANSI_W
  EXPECT(mac_keycode_to_vk(0x01) == 'S');
  EXPECT(mac_keycode_to_vk(0x02) == 'D');
  EXPECT(mac_keycode_to_vk(0x12) == '1');   // kVK_ANSI_1
  EXPECT(mac_keycode_to_vk(0x1D) == '0');   // kVK_ANSI_0
  EXPECT(mac_keycode_to_vk(0x24) == 13);    // Return
  EXPECT(mac_keycode_to_vk(0x31) == 32);    // Space
  EXPECT(mac_keycode_to_vk(0x33) == 8);     // Delete (backspace)
  EXPECT(mac_keycode_to_vk(0x35) == 27);    // Escape
  EXPECT(mac_keycode_to_vk(0x38) == 16);    // Shift
  EXPECT(mac_keycode_to_vk(0x3B) == 17);    // Control
  EXPECT(mac_keycode_to_vk(0x3A) == 18);    // Option
  EXPECT(mac_keycode_to_vk(0x7B) == 37 && mac_keycode_to_vk(0x7E) == 38 && mac_keycode_to_vk(0x7C) == 39 && mac_keycode_to_vk(0x7D) == 40);
  EXPECT(mac_keycode_to_vk(0x7A) == 112);   // F1
  EXPECT(mac_keycode_to_vk(0x6F) == 123);   // F12
  EXPECT(mac_keycode_to_vk(0x52) == 96);    // keypad 0
  EXPECT(mac_keycode_to_vk(0x32) == 192);   // `
  EXPECT(mac_keycode_to_vk(0xFF) == 0);
  if (fails) return 1;
  std::printf("mac_keymap_test: all passed\n");
  return 0;
}
