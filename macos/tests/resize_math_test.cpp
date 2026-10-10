#include "resize_math.h"
#include <cstdio>
static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main() {
  using mcfm::launcher::pixel_size;
  EXPECT(pixel_size(640, 2.0) == 1280);
  EXPECT(pixel_size(100.4, 1.0) == 100);
  EXPECT(pixel_size(0, 2.0) == 1);
  EXPECT(pixel_size(-5, 2.0) == 1);
  EXPECT(pixel_size(0.2, 1.0) == 1);
  int x = 0, y = 0;
  mcfm::launcher::view_to_pixels(10, 700, 720, 2.0, &x, &y);  // near the top of a 720-pt view
  EXPECT(x == 20 && y == 40);
  mcfm::launcher::view_to_pixels(0, 0, 720, 1.0, &x, &y);     // bottom-left
  EXPECT(x == 0 && y == 720);
  if (fails) return 1;
  std::printf("resize_math_test: all passed\n");
  return 0;
}
