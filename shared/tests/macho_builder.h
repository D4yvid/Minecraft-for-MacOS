#pragma once
// Test helper: builds a Mach-O header + load commands in a 64 KB buffer (the "file").
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

struct Builder {
  std::vector<uint8_t> b = std::vector<uint8_t>(64 * 1024, 0);
  size_t off = 32;
  uint32_t ncmds = 0;
  template <class T> void put(size_t at, T v) { std::memcpy(b.data() + at, &v, sizeof v); }
  size_t begin(uint32_t cmd, uint32_t size) {
    size_t at = off;
    put<uint32_t>(at, cmd);
    put<uint32_t>(at + 4, size);
    off += size;
    ncmds++;
    return at;
  }
  void segment(const char *name, uint64_t vmaddr, uint64_t vmsize, uint64_t fileoff, uint64_t filesize, uint32_t prot,
               std::vector<std::pair<const char *, uint64_t>> sects, uint32_t sect_flags = 0) {
    size_t at = begin(0x19, 72 + 80 * static_cast<uint32_t>(sects.size()));
    std::strncpy(reinterpret_cast<char *>(b.data() + at + 8), name, 16);
    put<uint64_t>(at + 24, vmaddr);
    put<uint64_t>(at + 32, vmsize);
    put<uint64_t>(at + 40, fileoff);
    put<uint64_t>(at + 48, filesize);
    put<uint32_t>(at + 56, prot);
    put<uint32_t>(at + 60, prot);
    put<uint32_t>(at + 64, static_cast<uint32_t>(sects.size()));
    uint64_t addr = vmaddr;
    for (size_t k = 0; k < sects.size(); k++) {
      size_t s = at + 72 + 80 * k;
      std::strncpy(reinterpret_cast<char *>(b.data() + s), sects[k].first, 16);
      std::strncpy(reinterpret_cast<char *>(b.data() + s + 16), name, 16);
      put<uint64_t>(s + 32, addr);
      put<uint64_t>(s + 40, sects[k].second);
      put<uint32_t>(s + 48, static_cast<uint32_t>(fileoff + (addr - vmaddr)));
      put<uint32_t>(s + 64, sect_flags);
      addr += sects[k].second;
    }
  }
  void dylib(uint32_t cmd, const char *name) {
    size_t n = std::strlen(name) + 1, size = (24 + n + 7) & ~size_t(7);
    size_t at = begin(cmd, static_cast<uint32_t>(size));
    put<uint32_t>(at + 8, 24);
    std::memcpy(b.data() + at + 24, name, n);
  }
  void finish() {
    put<uint32_t>(0, 0xFEEDFACF);
    put<uint32_t>(4, 0x0100000C);
    put<uint32_t>(12, 6);  // MH_DYLIB
    put<uint32_t>(16, ncmds);
    put<uint32_t>(20, static_cast<uint32_t>(off - 32));
  }
};

}  // namespace
