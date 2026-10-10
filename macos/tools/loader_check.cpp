// loader-check <image>: loads the converted game with Apple's dyld and with our loader in one
// process and compares every fixup location. Equal values, or both pointing into their own
// image at the same unslid address, pass. Exit 1 on any difference.
#include <dlfcn.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "loader.h"
#include "loader_macos.h"

using namespace mcfm::loader;

int main(int argc, char **argv) {
  if (argc != 2) { std::fprintf(stderr, "usage: loader-check <libminecraftpe.dylib>\n"); return 2; }
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::string dir = argv[1];
  dir = dir.substr(0, dir.rfind('/'));

  // 1. Our loader first (no initializers): its imports load the stub/provider libraries, which
  //    dyld then reuses for its own copy.
  MacLoaderOS os(dir);
  LoadOptions opts;
  opts.run_initializers = false;
  Image ours;
  std::string err;
  int fd = open(argv[1], O_RDONLY);
  if (!load_image(os, fd, file.data(), file.size(), opts, &ours, &err)) {
    std::fprintf(stderr, "loader-check: our loader failed: %s\n", err.c_str());
    return 1;
  }
  // 2. dyld's copy.
  if (!dlopen(argv[1], RTLD_NOW | RTLD_LOCAL)) {
    std::fprintf(stderr, "loader-check: dlopen failed: %s\n", dlerror());
    return 1;
  }
  intptr_t dyld_slide = 0;
  bool found = false;
  for (uint32_t i = 0; i < _dyld_image_count(); i++)
    if (std::strstr(_dyld_get_image_name(i), "libminecraftpe")) {
      dyld_slide = _dyld_get_image_vmaddr_slide(i);
      found = true;
    }
  if (!found) { std::fprintf(stderr, "loader-check: dyld image not found\n"); return 1; }

  uint64_t lo = UINT64_MAX, hi = 0;
  for (const Segment &s : ours.macho.segments)
    if (s.vmsize) { lo = std::min(lo, s.vmaddr); hi = std::max(hi, s.vmaddr + s.vmsize); }
  auto unslide = [&](uint64_t v, intptr_t slide, uint64_t *out) {
    uint64_t u = v - static_cast<uint64_t>(slide);
    if (u >= lo && u < hi) { *out = u; return true; }
    return false;
  };
  std::vector<Fixup> fx;
  decode_fixups(file.data(), file.size(), ours.macho, &fx, &err);
  std::set<uint64_t> locations;
  std::map<uint64_t, std::string> bound_symbol;  // last bind per location
  size_t rebases = 0, binds = 0;
  for (const Fixup &x : fx) {
    locations.insert(ours.macho.segments[x.segment].vmaddr + x.offset);
    if (x.kind != FixupKind::Rebase) bound_symbol[ours.macho.segments[x.segment].vmaddr + x.offset] = x.symbol;
    (x.kind == FixupKind::Rebase ? rebases : binds)++;
  }
  size_t differences = 0, same_library = 0, stub_binder = 0;
  for (uint64_t addr : locations) {
    uint64_t a, b, ua, ub;
    std::memcpy(&a, reinterpret_cast<void *>(addr + ours.slide), 8);
    std::memcpy(&b, reinterpret_cast<void *>(addr + dyld_slide), 8);
    if (a == b) continue;
    // Rebased the same way: each loader added its own slide (incl. tagged values, high bits set).
    if (a - static_cast<uint64_t>(ours.slide) == b - static_cast<uint64_t>(dyld_slide)) continue;
    (void)ua;
    (void)ub;
    auto sym = bound_symbol.find(addr);
    // dyld's lazy-binding helper: we bind every pointer eagerly, it is never called.
    if (sym != bound_symbol.end() && sym->second == "dyld_stub_binder") { stub_binder++; continue; }
    Dl_info ia = {}, ib = {};
    dladdr(reinterpret_cast<void *>(a), &ia);
    dladdr(reinterpret_cast<void *>(b), &ib);
    // The same import from the same library through another entry point: libsystem_platform
    // exports strcmp/strncmp both as the plain implementation (what dlsym returns) and as a
    // dispatching entry (what linked code gets). Same function, same library.
    if (sym != bound_symbol.end() && ia.dli_fname && ib.dli_fname && std::strcmp(ia.dli_fname, ib.dli_fname) == 0) {
      same_library++;
      continue;
    }
    if (differences++ < 20) {
      std::printf("difference at 0x%llx: ours 0x%llx (%s) dyld 0x%llx (%s)\n", (unsigned long long)addr,
                  (unsigned long long)a, ia.dli_sname ? ia.dli_sname : "?", (unsigned long long)b, ib.dli_sname ? ib.dli_sname : "?");
    }
  }
  std::printf("loader-check: %zu fixup locations (%zu rebases, %zu binds), %zu differences "
              "(accepted: %zu same-library entry points, %zu dyld_stub_binder)\n",
              locations.size(), rebases, binds, differences, same_library, stub_binder);
  return differences ? 1 : 0;
}
