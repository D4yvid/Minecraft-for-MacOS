// mcfm-launch: loads the converted game image (make launcher) in a plain macOS process.
// usage: mcfm-launch [image]   (default: libminecraftpe.dylib next to this executable)
// The LC_UUID is checked on the file before dlopen, so no other image's code runs.
#include "macho_uuid.h"

#include <dlfcn.h>
#include <mach-o/dyld.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string executable_dir() {
  char buf[4096];
  uint32_t size = sizeof buf;
  if (_NSGetExecutablePath(buf, &size) != 0) return ".";
  std::string path(buf);
  return path.substr(0, path.rfind('/'));
}

// Header + load commands of the file (the UUID lives there).
bool read_header(const std::string &path, std::vector<char> *out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out->assign(64 * 1024, 0);
  f.read(out->data(), out->size());
  return f.gcount() >= 32;
}

}  // namespace

int main(int argc, char **argv) {
  std::string path = argc > 1 ? argv[1] : executable_dir() + "/libminecraftpe.dylib";
  std::vector<char> header;
  if (!read_header(path, &header)) {
    std::fprintf(stderr, "mcfm: cannot load %s: unreadable\n", path.c_str());
    return 2;
  }
  if (!mcfm::is_expected_game_image(header.data())) {
    std::fprintf(stderr, "mcfm: %s is not Minecraft PE 0.15.10 (LC_UUID)\n", path.c_str());
    return 3;
  }
  if (!dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL)) {
    std::fprintf(stderr, "mcfm: cannot load %s: %s\n", path.c_str(), dlerror());
    return 2;
  }
  for (uint32_t i = 0; i < _dyld_image_count(); i++) {
    const mach_header *h = _dyld_get_image_header(i);
    if (mcfm::is_expected_game_image(h)) {
      std::printf("mcfm: game image loaded at %p (slide 0x%lx)\n", static_cast<const void *>(h),
                  static_cast<unsigned long>(_dyld_get_image_vmaddr_slide(i)));
      return 0;
    }
  }
  std::fprintf(stderr, "mcfm: %s loaded but not found among dyld images\n", path.c_str());
  return 2;
}
