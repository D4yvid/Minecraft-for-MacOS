#include "../src/titlebar_zone.h"
#include <cstdio>

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
  using mcpekbm::in_titlebar_hot_zone;
  EXPECT(in_titlebar_hot_zone(10, 10));     // over the traffic lights
  EXPECT(in_titlebar_hot_zone(0, 0));
  EXPECT(in_titlebar_hot_zone(79.9, 29.9));
  EXPECT(!in_titlebar_hot_zone(80, 10));    // right of the buttons
  EXPECT(!in_titlebar_hot_zone(10, 30));    // below the title bar
  EXPECT(!in_titlebar_hot_zone(500, 5));    // top edge, middle of window
  EXPECT(!in_titlebar_hot_zone(-1, -1));    // pointer gone / captured
  // window centre: AppKit frame (bottom-left origin) -> CoreGraphics global (top-left origin)
  double cx = 0, cy = 0;
  mcpekbm::window_center_cg(100, 200, 800, 500, 1080, &cx, &cy);
  EXPECT(cx == 500);   // 100 + 800/2
  EXPECT(cy == 630);   // 1080 - (200 + 500/2)
  mcpekbm::window_center_cg(-1920, 0, 1920, 1080, 1080, &cx, &cy);  // display left of the primary
  EXPECT(cx == -960);
  EXPECT(cy == 540);

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("titlebar_test: all passed\n");
  return 0;
}
