#pragma once
// Engine functions the launcher replaces (docs/LAUNCHER.md "Patch policy"). The converter
// patches each address to jump through the hook table; the host fills table[i] with
// hooks()[i].replacement before the game's initializers run. C++11.
#include <cstddef>
#include <cstdint>

namespace mcfm {
namespace launcher {

struct Hook {
  const char *name;
  uintptr_t address;   // unslid, start of the function (>= 12 bytes)
  void *replacement;   // same signature and ABI as the original
};

const Hook *hooks(size_t *count);

}  // namespace launcher
}  // namespace mcfm
