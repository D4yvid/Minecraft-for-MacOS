#include "../src/input_policy.h"
#include <cstdio>

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
  using namespace mcpekbm;
  const PointerState menu{false, true, 400, 300};       // free cursor over the game
  const PointerState strip{false, true, 400, 10};       // over the hidden title bar strip
  const PointerState lights{false, true, 20, 10};       // over the traffic lights
  const PointerState outside{false, false, 400, 300};   // menu bar, other window, resize edge
  const PointerState game{true, true, 0, 0};            // captured in-world

  // I3: dragging in menus keeps moving the cursor (moves come from the touch stream)
  EXPECT(should_feed(PointerEvent::Move, menu));
  EXPECT(should_feed(PointerEvent::ButtonDown, menu));
  EXPECT(should_feed(PointerEvent::Scroll, menu));

  // I6: clicks/scrolls on the window chrome or outside the view never reach the game
  EXPECT(!should_feed(PointerEvent::ButtonDown, strip));
  EXPECT(!should_feed(PointerEvent::ButtonDown, lights));
  EXPECT(!should_feed(PointerEvent::Scroll, strip));
  EXPECT(!should_feed(PointerEvent::ButtonDown, outside));
  EXPECT(!should_feed(PointerEvent::Scroll, outside));
  EXPECT(!should_feed(PointerEvent::Move, outside));
  // releases always pass so nothing stays held
  EXPECT(should_feed(PointerEvent::ButtonUp, strip));
  EXPECT(should_feed(PointerEvent::ButtonUp, outside));

  // captured: buttons and scroll always feed; absolute moves don't (look uses deltas)
  EXPECT(should_feed(PointerEvent::ButtonDown, game));
  EXPECT(should_feed(PointerEvent::Scroll, game));
  EXPECT(should_feed(PointerEvent::ButtonUp, game));
  EXPECT(!should_feed(PointerEvent::Move, game));

  // a hover that ends mid-view (button pressed, pointer onto the traffic lights) is not an exit
  EXPECT(!hover_end_is_exit(400, 300, 1000, 700));
  EXPECT(!hover_end_is_exit(20, 10, 1000, 700));
  EXPECT(hover_end_is_exit(400, 1, 1000, 700));    // left through the top
  EXPECT(hover_end_is_exit(999, 300, 1000, 700));  // right edge
  EXPECT(hover_end_is_exit(400, 698, 1000, 700));  // bottom edge
  EXPECT(hover_end_is_exit(2, 300, 1000, 700));    // left edge
  EXPECT(hover_end_is_exit(-1, -1, 1000, 700));    // never seen inside

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("input_policy_test: all passed\n");
  return 0;
}
