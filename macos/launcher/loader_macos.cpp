#include "loader_macos.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <mutex>

namespace mcfm {
namespace loader {
namespace {

// libunwind's hook for code it did not see loaded (JITs; here: images we map ourselves).
struct UnwindSections {
  uintptr_t dso_base, dwarf_section;
  size_t dwarf_section_length;
  uintptr_t compact_unwind_section;
  size_t compact_unwind_section_length;
};
extern "C" int __unw_add_find_dynamic_unwind_sections(int (*)(uintptr_t, UnwindSections *));

struct Range { uintptr_t lo, hi; UnwindSections sections; };
constexpr int kMaxImages = 8;
Range g_ranges[kMaxImages];
int g_count = 0;
std::mutex g_lock;

int find_sections(uintptr_t addr, UnwindSections *info) {
  std::lock_guard<std::mutex> hold(g_lock);
  for (int i = 0; i < g_count; i++)
    if (addr >= g_ranges[i].lo && addr < g_ranges[i].hi) {
      *info = g_ranges[i].sections;
      return 1;
    }
  return 0;
}

}  // namespace

size_t MacLoaderOS::page_size() { return static_cast<size_t>(getpagesize()); }

uint8_t *MacLoaderOS::reserve(size_t size) {
  void *p = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  return p == MAP_FAILED ? nullptr : static_cast<uint8_t *>(p);
}

bool MacLoaderOS::register_code_signature(int fd, uint64_t offset, uint64_t size) {
  fsignatures_t sig = {0, reinterpret_cast<void *>(offset), static_cast<size_t>(size)};
  if (fcntl(fd, F_ADDFILESIGS_RETURN, &sig) == -1) {
    perror("mcfm: F_ADDFILESIGS_RETURN");
    return false;
  }
  return true;
}

bool MacLoaderOS::map_file(uint8_t *at, size_t size, int prot, int fd, uint64_t offset) {
  // Mach VM_PROT_* and PROT_* share values (read 1, write 2, execute 4).
  return mmap(at, size, prot, MAP_PRIVATE | MAP_FIXED, fd, static_cast<off_t>(offset)) == at;
}

bool MacLoaderOS::map_zero(uint8_t *at, size_t size, int prot) {
  return mmap(at, size, prot, MAP_PRIVATE | MAP_FIXED | MAP_ANON, -1, 0) == at;
}

void *MacLoaderOS::open_library(const std::string &name) {
  // Stubs reference @rpath/libmcfm_stubrt.dylib: load it from our directory first, so dyld
  // reuses it whatever the host executable's rpaths are.
  static bool runtime_loaded = false;
  if (!runtime_loaded && name.compare(0, 10, "mcfm_stub_") == 0) {
    runtime_loaded = true;
    dlopen((dir_ + "/libmcfm_stubrt.dylib").c_str(), RTLD_NOW | RTLD_GLOBAL);
  }
  std::string path = name == "libc++"      ? "/usr/lib/libc++.1.dylib"
                     : name == "libSystem" ? "/usr/lib/libSystem.B.dylib"
                     : name == "libz"      ? "/usr/lib/libz.1.dylib"
                                           : dir_ + "/" + name + ".dylib";
  void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) log(std::string("cannot load ") + path + ": " + dlerror());
  return h;
}

void *MacLoaderOS::symbol(void *library, const std::string &name) { return dlsym(library, name.c_str()); }

void *MacLoaderOS::flat_symbol(const std::string &name) { return dlsym(RTLD_DEFAULT, name.c_str()); }

void MacLoaderOS::register_unwind(uintptr_t header, uintptr_t text_lo, uintptr_t text_hi, uintptr_t compact_unwind,
                                  size_t compact_size, uintptr_t eh_frame, size_t eh_size) {
  std::lock_guard<std::mutex> hold(g_lock);
  if (g_count == kMaxImages) return;
  if (g_count == 0) __unw_add_find_dynamic_unwind_sections(find_sections);
  UnwindSections s = {header, eh_frame, eh_size, compact_unwind, compact_size};
  g_ranges[g_count++] = Range{text_lo, text_hi, s};
}

void MacLoaderOS::log(const std::string &line) { std::fprintf(stderr, "mcfm: %s\n", line.c_str()); }

}  // namespace loader
}  // namespace mcfm
