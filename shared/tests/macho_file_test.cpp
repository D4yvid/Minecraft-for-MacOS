// parse_macho: fields from a crafted header, and refusal of malformed ones.
#include "macho_file.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace mcfm::loader;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

namespace {

// Builds a Mach-O header + load commands in a 64 KB buffer (the "file").
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

Builder sample() {
  Builder s;
  s.segment("__TEXT", 0x100000000, 0x4000, 0, 0x4000, 5, {{"__text", 0x100}, {"__unwind_info", 0x40}});
  s.segment("__DATA", 0x100004000, 0x4000, 0x4000, 0x2000, 3, {{"__mod_init_func", 0x10}}, 9);
  size_t di = s.begin(0x80000022, 48);
  for (int i = 0; i < 10; i++) s.put<uint32_t>(di + 8 + 4 * i, 0x6000 + 0x10 * i);
  s.dylib(0xC, "/usr/lib/libc++.1.dylib");
  s.dylib(0x80000018, "@rpath/mcfm_stub_WebKit.dylib");
  size_t cs = s.begin(0x1D, 16);
  s.put<uint32_t>(cs + 8, 0x7000);
  s.put<uint32_t>(cs + 12, 0x200);
  size_t u = s.begin(0x1B, 24);
  for (int i = 0; i < 16; i++) s.b[u + 8 + i] = static_cast<uint8_t>(i + 1);
  s.finish();
  return s;
}

}  // namespace

int main() {
  Builder s = sample();
  MachOFile m;
  std::string err;
  EXPECT(parse_macho(s.b.data(), s.b.size(), &m, &err));
  EXPECT(err.empty());
  EXPECT(m.filetype == 6);
  EXPECT(m.segments.size() == 2 && m.segments[0].name == "__TEXT" && m.segments[1].name == "__DATA");
  EXPECT(m.segments[1].vmaddr == 0x100004000 && m.segments[1].filesize == 0x2000 && m.segments[1].initprot == 3);
  EXPECT(m.section("__TEXT", "__unwind_info") && m.section("__TEXT", "__unwind_info")->size == 0x40);
  EXPECT(m.section("__DATA", "__mod_init_func") && m.section("__DATA", "__mod_init_func")->flags == 9);
  EXPECT(m.segment("__DATA") == &m.segments[1] && m.segment("__LINKEDIT") == nullptr);
  EXPECT(m.dylibs.size() == 2 && m.dylibs[0] == "/usr/lib/libc++.1.dylib" && m.dylibs[1] == "@rpath/mcfm_stub_WebKit.dylib");
  EXPECT(m.weak_dylibs.size() == 2 && !m.weak_dylibs[0] && m.weak_dylibs[1]);
  EXPECT(m.has_dyld_info && !m.has_chained_fixups);
  EXPECT(m.dyld_info.rebase_off == 0x6000 && m.dyld_info.export_size == 0x6090);
  EXPECT(m.code_signature_off == 0x7000 && m.code_signature_size == 0x200);
  EXPECT(m.uuid[0] == 1 && m.uuid[15] == 16);

  // Malformed: cmdsize 0.
  Builder z = sample();
  z.put<uint32_t>(32 + 4, 0);
  err.clear();
  EXPECT(!parse_macho(z.b.data(), z.b.size(), &m, &err) && !err.empty());
  // Malformed: nsects past cmdsize.
  Builder n = sample();
  n.put<uint32_t>(32 + 64, 50);
  err.clear();
  EXPECT(!parse_macho(n.b.data(), n.b.size(), &m, &err) && !err.empty());
  // Malformed: dyld info past the end of the file.
  Builder d = sample();
  err.clear();
  EXPECT(!parse_macho(d.b.data(), 0x6000, &m, &err) && !err.empty());
  // Not a 64-bit Mach-O, and a buffer shorter than a header.
  std::vector<uint8_t> junk(4096, 0);
  EXPECT(!parse_macho(junk.data(), junk.size(), &m, &err));
  EXPECT(!parse_macho(s.b.data(), 16, &m, &err));
  // Chained fixups are recognised (and refused later by the loader).
  Builder c = sample();
  size_t at = c.begin(0x80000034, 16);
  (void)at;
  c.finish();
  EXPECT(parse_macho(c.b.data(), c.b.size(), &m, &err) && m.has_chained_fixups);

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("macho_file_test: all passed\n");
  return 0;
}
