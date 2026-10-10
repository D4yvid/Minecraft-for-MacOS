// write_ppm: GL rows (bottom-up RGBA) become a top-down binary PPM (P6, RGB).
#include "screenshot.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main(int, char **argv) {
  // 2x2: bottom row red, green; top row blue, white (GL order: bottom row first).
  const unsigned char rgba[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
  std::string path = std::string(argv[1]);
  EXPECT(mcfm::launcher::write_ppm(path.c_str(), 2, 2, rgba));
  std::ifstream f(path, std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(f)), {});
  const std::string header = "P6\n2 2\n255\n";
  EXPECT(data.size() == header.size() + 12);
  EXPECT(data.compare(0, header.size(), header) == 0);
  const unsigned char want[] = {0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255, 0};  // top row first
  for (int i = 0; i < 12 && header.size() + i < data.size(); i++) EXPECT((unsigned char)data[header.size() + i] == want[i]);
  EXPECT(!mcfm::launcher::write_ppm("/nonexistent-dir/x.ppm", 2, 2, rgba));
  if (fails) return 1;
  std::printf("screenshot_test: all passed\n");
  return 0;
}
