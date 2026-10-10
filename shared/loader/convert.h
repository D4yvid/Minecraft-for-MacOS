#pragma once
// The game executable -> the image our loader maps (docs/LAUNCHER.md): the C++ form of
// tools/launcher/mcfm_image.py dylib --hooks, byte for byte, so the Android app can import a
// user's IPA on the device. C++11, no OS headers.
#include <cstdint>
#include <string>
#include <vector>

namespace mcfm {
namespace loader {

struct ConvertHook {
  std::string name;
  uint64_t address;  // unslid, start of the function
};

// The arm64 slice of a FAT (universal) Mach-O; a thin arm64 Mach-O is returned as it is.
bool thin_arm64(const std::vector<uint8_t> &in, std::vector<uint8_t> *out, std::string *error);

// MH_EXECUTE -> MH_DYLIB (LC_ID_DYLIB @rpath/libminecraftpe.dylib), __PAGEZERO -> __MCFM_PAD,
// __objc_* sections -> __xbjc_*, framework loads -> @rpath/mcfm_stub_<lib>.dylib, macOS platform
// tag, no LC_MAIN/dylinker/encryption info; hooked functions start with adrp/ldr/br x16 through
// the hook table at the end of __DATA. `in` must be thin arm64 (thin_arm64 first) and decrypted.
bool convert_executable(const std::vector<uint8_t> &in, const std::vector<ConvertHook> &hooks,
                        std::vector<uint8_t> *out, std::string *error);

}  // namespace loader
}  // namespace mcfm
