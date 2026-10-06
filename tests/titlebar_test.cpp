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
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("titlebar_test: all passed\n");
  return 0;
}
