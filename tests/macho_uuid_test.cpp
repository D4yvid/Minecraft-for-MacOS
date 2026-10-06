#include "../src/macho_uuid.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::vector<char> slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(f), {});
}

int main(int argc, char **argv) {
  // argv[1]: the converted game binary (thin arm64), argv[2]: any other thin binary
  if (argc != 3) { std::printf("usage: macho_uuid_test <game> <other>\n"); return 2; }
  std::vector<char> game = slurp(argv[1]), other = slurp(argv[2]);
  EXPECT(game.size() > 4096 && other.size() > 1024);
  EXPECT(mcpekbm::is_expected_game_image(game.data()));     // the real game
  EXPECT(!mcpekbm::is_expected_game_image(other.data()));   // a different binary
  std::vector<char> junk(4096, 0);
  EXPECT(!mcpekbm::is_expected_game_image(junk.data()));    // not a Mach-O at all
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("macho_uuid_test: all passed\n");
  return 0;
}
