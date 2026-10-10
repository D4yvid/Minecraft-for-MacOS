#pragma once
// The engine boot of mcfm-run --boot (boot.cpp). C++.
#include <cstdint>
#include <string>

namespace mcfm {
namespace android {

struct BootOptions {
  std::string data_dir;    // the game's data/ directory
  std::string home_dir;    // worlds and options: <home>/games/com.mojang
  std::string screenshot;  // PPM of the last frame, if not empty
  long frames = 120;
  int width = 1280, height = 720;  // the current GL surface's size
};

// Needs the game image loaded (with the launcher's hooks) and a current GLES 3 context.
int boot(uintptr_t slide, const BootOptions &options);

}  // namespace android
}  // namespace mcfm
