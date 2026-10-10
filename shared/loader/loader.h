#pragma once
// Our Mach-O loader (docs/LAUNCHER.md, Stage 2): maps a converted image, applies its fixups as
// dyld would, fills the launcher's hook table, registers unwind info and runs initializers.
// Platform-free; the host provides memory, code signing, symbols and unwind through LoaderOS.
// C++11.
#include <cstddef>
#include <cstdint>
#include <string>

#include "fixups.h"
#include "macho_file.h"

namespace mcfm {
namespace loader {

struct LoaderOS {
  virtual ~LoaderOS() {}
  virtual size_t page_size() = 0;
  virtual uint8_t *reserve(size_t size) = 0;  // inaccessible address range, nullptr on failure
  // `mapped_end`: file offset where the mapped segments end; the signature must cover it.
  virtual bool register_code_signature(int fd, uint64_t offset, uint64_t size, uint64_t mapped_end) = 0;
  // Maps whole pages, like mmap: the bytes after `size` in the last page come from the file.
  virtual bool map_file(uint8_t *at, size_t size, int prot, int fd, uint64_t offset) = 0;
  virtual bool requires_code_signature() = 0;  // true: an unsigned image cannot run here
  virtual bool map_zero(uint8_t *at, size_t size, int prot) = 0;
  virtual void *open_library(const std::string &short_name) = 0;  // libc++, mcfm_stub_UIKit, ...
  virtual void *symbol(void *library, const std::string &name) = 0;  // name without the leading '_'
  virtual void *flat_symbol(const std::string &name) = 0;            // any loaded library; no '_'
  // false: the unwinder will not find the image's frames (exceptions would terminate).
  virtual bool register_unwind(uintptr_t header, uintptr_t text_lo, uintptr_t text_hi, uintptr_t compact_unwind,
                               size_t compact_size, uintptr_t eh_frame, size_t eh_size) = 0;
  virtual void log(const std::string &line) = 0;
};

struct Image {
  uint8_t *header = nullptr;  // mapped mach_header (start of __TEXT)
  intptr_t slide = 0;
  uintptr_t text_lo = 0, text_hi = 0;
  MachOFile macho;
};

struct LoadOptions {
  const uintptr_t *hook_addresses = nullptr;     // unslid; the image must be converted with them
  const void *const *hook_replacements = nullptr;
  const char *const *hook_names = nullptr;       // optional, for error messages
  size_t hook_count = 0;
  bool run_initializers = true;
  // Optional: called on the mapped header (what will run) before any fixup; false refuses the
  // image. The file read for parsing could differ from the one mapped.
  bool (*accept_header)(const uint8_t *header) = nullptr;
  // Passed to initializers as dyld does (argc, argv, envp, apple).
  int argc = 0;
  const char **argv = nullptr;
  const char **envp = nullptr;
  const char **apple = nullptr;
};

// "libc++" for /usr/lib/libc++.1.dylib, "mcfm_stub_UIKit" for @rpath/mcfm_stub_UIKit.dylib.
std::string library_short_name(const std::string &install_name);

// Binds with ordinal -1 (flat lookup) search every loaded library, as dyld's flat namespace.
// `file`/`size`: the image's bytes (parsed); `fd`: the same file (mapped). false + a message
// naming what failed (malformed image, missing library or symbol, hooks not installed, ...).
bool load_image(LoaderOS &os, int fd, const uint8_t *file, size_t size, const LoadOptions &options, Image *out,
                std::string *error);

}  // namespace loader
}  // namespace mcfm
