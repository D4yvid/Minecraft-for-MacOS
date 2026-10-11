// The iOS app's drawable size (ios/launcher/layout.h): points x the screen's native scale, in
// landscape (width >= height) whatever the view reports. Host test.
#include <cstdio>

#include "layout.h"

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

using mcfm::launcher::pixel_size;

int main() {
  auto p = pixel_size(956, 440, 3);  // iPhone 16 Pro Max, landscape
  EXPECT(p.w == 2868 && p.h == 1320);
  p = pixel_size(440, 956, 3);  // the view still in portrait during rotation
  EXPECT(p.w == 2868 && p.h == 1320);
  p = pixel_size(1024.5, 768, 2);  // rounded, not truncated
  EXPECT(p.w == 2049 && p.h == 1536);
  p = pixel_size(0, 0, 3);
  EXPECT(p.w == 0 && p.h == 0);
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("layout_test: all passed\n");
  return 0;
}
