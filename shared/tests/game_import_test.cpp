// The game import (shared/launcher/game_import.cpp, run by the Android app on the user's IPA):
// the binary is checked, converted with the launcher's hooks and replaces the previous image
// atomically; anything refused leaves the previous image as it was.
// usage: game_import_test <fixture executable> <hooks.tsv> <python-converted.dylib> <tmp dir>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "game_import.h"

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::string read(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char **argv) {
  if (argc != 5) return 2;
  std::string fixture = argv[1], dir = argv[4];
  std::vector<mcfm::loader::ConvertHook> hooks;
  std::ifstream h(argv[2]);
  for (std::string line; std::getline(h, line);)
    if (!line.empty()) hooks.push_back({line.substr(0, line.find('\t')), std::strtoull(line.c_str() + line.find('\t') + 1, nullptr, 16)});
  using mcfm::launcher::import_game;
  using mcfm::launcher::ImportOptions;
  ImportOptions fixture_options;
  fixture_options.hooks = hooks;
  fixture_options.check_game_uuid = false;  // the fixture is not the game

  // 1. The fixture: the image is what mcfm_image.py makes.
  std::string error = import_game(fixture, dir, fixture_options);
  EXPECT(error.empty());
  EXPECT(read(dir + "/minecraftpe.dylib") == read(argv[3]));

  // 2. The game check: the fixture is not Minecraft PE 0.15.10, and the old image stays.
  ImportOptions game_options;
  game_options.hooks = hooks;
  error = import_game(fixture, dir, game_options);
  EXPECT(error.find("0.15.10") != std::string::npos);
  EXPECT(read(dir + "/minecraftpe.dylib") == read(argv[3]));

  // 3. Unreadable and malformed inputs: a message, the old image untouched, no temp file left.
  error = import_game(dir + "/does-not-exist", dir, fixture_options);
  EXPECT(!error.empty());
  { std::ofstream(dir + "/junk") << "not a Mach-O"; }
  error = import_game(dir + "/junk", dir, fixture_options);
  EXPECT(!error.empty());
  EXPECT(read(dir + "/minecraftpe.dylib") == read(argv[3]));
  EXPECT(!std::ifstream(dir + "/minecraftpe.dylib.tmp"));

  // 4. A re-import replaces the image (same bytes again here).
  { std::ofstream(dir + "/minecraftpe.dylib") << "old"; }
  EXPECT(import_game(fixture, dir, fixture_options).empty());
  EXPECT(read(dir + "/minecraftpe.dylib") == read(argv[3]));

  // 5. The image records the hooks it was converted with: an app whose seams changed sees it is
  //    stale and converts the kept binary again (no new import).
  using mcfm::launcher::image_is_current;
  EXPECT(image_is_current(dir, hooks));
  std::vector<mcfm::loader::ConvertHook> other = hooks;
  other.back().address += 4;
  EXPECT(!image_is_current(dir, other));
  other = hooks;
  other.pop_back();  // a hook removed from the seams
  EXPECT(!image_is_current(dir, other));
  std::remove((dir + "/minecraftpe.hooks").c_str());
  EXPECT(!image_is_current(dir, hooks));
  EXPECT(!image_is_current(dir + "/nowhere", hooks));
  EXPECT(import_game(fixture, dir, fixture_options).empty() && image_is_current(dir, hooks));

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("game_import_test: all passed\n");
  return 0;
}
