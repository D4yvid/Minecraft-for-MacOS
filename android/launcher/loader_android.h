#pragma once
// LoaderOS on Android (docs/LAUNCHER.md, Stage 3a). Segments are copied into anonymous memory
// and mprotect'ed (an app targeting SDK 29+ may not execute its own files). Imports resolve to:
//   libSystem          -> the Darwin layer's table (mcfm_darwin_symbol)
//   libc++             -> libmcfm_runtime.so (Apple-ABI libc++/libc++abi)
//   libz               -> the system's libz.so
//   mcfm_stub_<lib>    -> <dir>/mcfm_stub_<lib>.so (symbols keep their Mach-O names)
// Unwind info is served to the runtime's libunwind through its dynamic section finder.
#include <set>
#include <string>

#include "loader.h"

namespace mcfm {
namespace loader {

class AndroidLoaderOS : public LoaderOS {
 public:
  explicit AndroidLoaderOS(const std::string &library_dir) : dir_(library_dir) {}
  size_t page_size() override;
  uint8_t *reserve(size_t size) override;
  bool register_code_signature(int, uint64_t, uint64_t, uint64_t) override { return true; }
  bool requires_code_signature() override { return false; }
  bool map_file(uint8_t *at, size_t size, int prot, int fd, uint64_t offset) override;
  bool map_zero(uint8_t *at, size_t size, int prot) override;
  void *open_library(const std::string &short_name) override;
  void *symbol(void *library, const std::string &name) override;
  void *flat_symbol(const std::string &name) override;
  bool register_unwind(uintptr_t header, uintptr_t text_lo, uintptr_t text_hi, uintptr_t compact_unwind,
                       size_t compact_size, uintptr_t eh_frame, size_t eh_size) override;
  void log(const std::string &line) override;

 private:
  std::string dir_;          // where mcfm_stub_*.so and libmcfm_stubrt.so live
  std::set<void *> stubs_;   // dlopen handles of stub libraries
};

}  // namespace loader
}  // namespace mcfm
