// Mouse look and hotbar scrolling from AppKit events.
#include "mouse_math.h"
#include <cstdio>
static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main() {
  using namespace mcfm::launcher;
  // Look: small fractional deltas add up instead of being truncated to 0.
  LookConverter look(2.0);  // Retina: points -> pixels
  int total_x = 0, total_y = 0, dx = 0, dy = 0;
  for (int i = 0; i < 10; i++) { look.feed(0.3, -0.3, &dx, &dy); total_x += dx; total_y += dy; }
  EXPECT(total_x == 6 && total_y == -6);
  look.feed(10.0, 0.0, &dx, &dy);
  EXPECT(dx == 20 && dy == 0);
  // Raw (GameController) deltas: device counts, y up -> engine y down, no point scaling.
  LookConverter raw(1.0);
  raw.feed_raw(3.0, 2.0, &dx, &dy);
  EXPECT(dx == 3 && dy == -2);
  // Once a raw mouse has reported, AppKit's accelerated deltas are not used for look.
  LookSource src;
  EXPECT(src.use_appkit_delta());
  src.raw_seen();
  EXPECT(!src.use_appkit_delta());
  // Mouse wheel: one hotbar step per click, however fast, sign gives the direction.
  HotbarScroll s;
  int steps = 0;
  for (int i = 0; i < 10; i++) steps += s.feed(1.0, false, 0.001 * i);
  EXPECT(steps == 10);
  EXPECT(s.feed(-3.0, false, 1.0) == -1);  // an accelerated click is still one step
  EXPECT(s.feed(0.0, false, 1.1) == 0);
  // Trackpad: one step per kTrackpadStep points; direction change drops the remainder.
  HotbarScroll t;
  steps = 0;
  for (int i = 0; i < 6; i++) steps += t.feed(HotbarScroll::kTrackpadStep / 2, true, 2.0 + 0.05 * i);
  EXPECT(steps == 3);
  EXPECT(t.feed(-HotbarScroll::kTrackpadStep / 2, true, 2.5) == 0);  // remainder dropped, not a step back
  // Trackpad momentum: at most one step per kTrackpadMinGap seconds.
  HotbarScroll m;
  steps = 0;
  for (int i = 0; i < 10; i++) steps += m.feed(HotbarScroll::kTrackpadStep * 3, true, 3.0 + 0.001 * i);
  EXPECT(steps == 1);
  if (fails) return 1;
  std::printf("mouse_math_test: all passed\n");
  return 0;
}
