#pragma once
// A 64-bit Mach-O read from bytes, for our own loader (docs/LAUNCHER.md, Stage 2). Only what
// loading needs; every offset is bounds-checked against the file. C++11, no OS headers.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mcfm {
namespace loader {

struct Section {
  std::string segment, name;
  uint64_t addr, size;
  uint32_t offset, flags;
};

struct Segment {
  std::string name;
  uint64_t vmaddr, vmsize, fileoff, filesize;
  uint32_t maxprot, initprot;
  std::vector<Section> sections;
};

struct DyldInfo {
  uint32_t rebase_off, rebase_size, bind_off, bind_size, weak_bind_off, weak_bind_size;
  uint32_t lazy_bind_off, lazy_bind_size, export_off, export_size;
};

struct MachOFile {
  uint32_t filetype = 0, flags = 0;
  std::vector<Segment> segments;     // load-command order: fixups address segments by index
  std::vector<std::string> dylibs;   // every dylib load command in order: ordinal i + 1
  std::vector<bool> weak_dylibs;     // LC_LOAD_WEAK_DYLIB
  bool has_dyld_info = false, has_chained_fixups = false;
  DyldInfo dyld_info = {};
  uint32_t code_signature_off = 0, code_signature_size = 0;
  uint8_t uuid[16] = {};

  const Section *section(const char *segment, const char *name) const;
  const Segment *segment(const char *name) const;
};

// false + a message for anything malformed: short buffer, bad magic, cmdsize, nsects, offsets
// or sizes past the end of the file.
bool parse_macho(const uint8_t *data, size_t size, MachOFile *out, std::string *error);

}  // namespace loader
}  // namespace mcfm
