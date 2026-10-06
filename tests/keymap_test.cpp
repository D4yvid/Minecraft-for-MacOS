#include "../src/keymap.h"
#include <cstdio>
#include <cstdlib>

static int fails = 0;
#define EXPECT_EQ(a, b) do { long _a = (a), _b = (b); if (_a != _b) { \
  std::printf("FAIL %s:%d %s == %ld, want %ld\n", __FILE__, __LINE__, #a, _a, _b); fails++; } } while (0)

int main() {
  using namespace mcpekbm;
  EXPECT_EQ(hid_to_vk(0x04), 'A');
  EXPECT_EQ(hid_to_vk(0x1A), 'W');
  EXPECT_EQ(hid_to_vk(0x1D), 'Z');
  EXPECT_EQ(hid_to_vk(0x1E), '1');
  EXPECT_EQ(hid_to_vk(0x26), '9');
  EXPECT_EQ(hid_to_vk(0x27), '0');
  EXPECT_EQ(hid_to_vk(0x28), 13);   // Return
  EXPECT_EQ(hid_to_vk(0x29), 27);   // Escape
  EXPECT_EQ(hid_to_vk(0x2A), 8);    // Backspace
  EXPECT_EQ(hid_to_vk(0x2B), 9);    // Tab
  EXPECT_EQ(hid_to_vk(0x2C), 32);   // Space
  EXPECT_EQ(hid_to_vk(0x2F), 219);  // [
  EXPECT_EQ(hid_to_vk(0x35), 192);  // `
  EXPECT_EQ(hid_to_vk(0x3A), 112);  // F1
  EXPECT_EQ(hid_to_vk(0x45), 123);  // F12
  EXPECT_EQ(hid_to_vk(0x4F), 39);   // Right
  EXPECT_EQ(hid_to_vk(0x50), 37);   // Left
  EXPECT_EQ(hid_to_vk(0x51), 40);   // Down
  EXPECT_EQ(hid_to_vk(0x52), 38);   // Up
  EXPECT_EQ(hid_to_vk(0xE0), 17);   // LCtrl
  EXPECT_EQ(hid_to_vk(0xE1), 16);   // LShift
  EXPECT_EQ(hid_to_vk(0xE5), 16);   // RShift
  EXPECT_EQ(hid_to_vk(0xE2), 18);   // LAlt
  EXPECT_EQ(hid_to_vk(0x00), 0);
  EXPECT_EQ(hid_to_vk(0x1000), 0);

  // Times are seconds. Steady trackpad drag: 0.25 per event every 30 ms -> notch per full step
  ScrollAccumulator s{1.0f};
  int total = 0;
  for (int i = 0; i < 10; i++) total += s.feed(0.25f, 0.03 * i);
  EXPECT_EQ(total, 2);
  // direction change resets; same direction -1.75 -> one notch
  EXPECT_EQ(s.feed(-0.25f, 0.31), 0);
  EXPECT_EQ(s.feed(-1.5f, 0.42), -1);
  // mouse wheel: one big event -> one notch (not 3), nothing left over
  ScrollAccumulator w{1.0f};
  EXPECT_EQ(w.feed(3.0f, 0.0), 1);
  EXPECT_EQ(w.feed(0.5f, 0.1), 0);
  // stale remainder decays: 0.9 then a tiny nudge a second later is not a notch
  ScrollAccumulator d{1.0f};
  EXPECT_EQ(d.feed(0.9f, 0.0), 0);
  EXPECT_EQ(d.feed(0.2f, 1.0), 0);
  // momentum flood: 1.5 every 10 ms for 1 s -> at most one notch per 80 ms
  ScrollAccumulator m{1.0f};
  int notches = 0;
  for (int i = 0; i < 100; i++) notches += m.feed(1.5f, 0.01 * i);
  EXPECT_EQ(notches <= 13, 1);
  EXPECT_EQ(notches >= 10, 1);

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("keymap_test: all passed\n");
  return 0;
}
