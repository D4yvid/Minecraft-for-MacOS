#pragma once
// Imports the user's game binary (from their decrypted IPA) as the image the launcher loads:
// thinned to arm64, checked to be Minecraft PE 0.15.10 (LC_UUID), converted with the launcher's
// hooks (shared/loader/convert.cpp) and written as <dir>/minecraftpe.dylib, replacing the
// previous image atomically. Used by the Android app (docs/LAUNCHER.md, Stage 3c). C++11.
#include <string>
#include <vector>

#include "convert.h"

namespace mcfm {
namespace launcher {

struct ImportOptions {
  std::vector<loader::ConvertHook> hooks;  // the launcher's seams (seams.cpp)
  bool check_game_uuid = true;             // false only for test fixtures
};

// Empty on success, else a message for the user; on failure the previous image is untouched.
std::string import_game(const std::string &binary_path, const std::string &dir, const ImportOptions &options);

}  // namespace launcher
}  // namespace mcfm
