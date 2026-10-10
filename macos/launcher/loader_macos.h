#pragma once
// LoaderOS on macOS: signed, file-backed mappings (the kernel validates the image's code
// signature, registered as dyld does), dlopen/dlsym for imports, libunwind's dynamic unwind
// sections for C++ exceptions.
#include <string>

#include "loader.h"

namespace mcfm {
namespace loader {

class MacLoaderOS : public LoaderOS {
 public:
  explicit MacLoaderOS(const std::string &library_dir) : dir_(library_dir) {}
  size_t page_size() override;
  uint8_t *reserve(size_t size) override;
  bool register_code_signature(int fd, uint64_t offset, uint64_t size, uint64_t mapped_end) override;
  bool map_file(uint8_t *at, size_t size, int prot, int fd, uint64_t offset) override;
  bool requires_code_signature() override { return true; }  // arm64 macOS runs only signed code
  bool map_zero(uint8_t *at, size_t size, int prot) override;
  void *open_library(const std::string &short_name) override;
  void *symbol(void *library, const std::string &name) override;
  void *flat_symbol(const std::string &name) override;
  bool register_unwind(uintptr_t header, uintptr_t text_lo, uintptr_t text_hi, uintptr_t compact_unwind,
                       size_t compact_size, uintptr_t eh_frame, size_t eh_size) override;
  void log(const std::string &line) override;

 private:
  std::string dir_;  // where mcfm_stub_*.dylib and the providers live
};

}  // namespace loader
}  // namespace mcfm
