#pragma once
// dyld's classic fixup streams (LC_DYLD_INFO_ONLY: rebase, bind, lazy bind, weak bind) and the
// export trie, decoded exactly as dyld does. C++11, no OS headers.
#include <cstdint>
#include <string>
#include <vector>

#include "macho_file.h"

namespace mcfm {
namespace loader {

enum class FixupKind { Rebase, Bind, LazyBind, WeakBind };

struct Fixup {
  FixupKind kind;
  int segment;          // index into MachOFile::segments
  uint64_t offset;      // within that segment
  int ordinal;          // binds: 1..N dylib, 0 self, -1 main executable, -2 flat lookup
  std::string symbol;   // with the leading '_'
  int64_t addend;
  bool weak_import;     // a missing symbol binds to 0
};

// All four streams in dyld's order: rebases, binds, lazy binds (bound eagerly), weak binds.
// false + a message for malformed or unsupported streams (threaded binds), and for fixups
// outside their segment or in a segment that is not writable.
bool decode_fixups(const uint8_t *file, size_t size, const MachOFile &m, std::vector<Fixup> *out, std::string *error);

// The export trie: `symbol`'s offset from the image base (the __TEXT vmaddr) and its flags.
bool find_export(const uint8_t *file, size_t size, const MachOFile &m, const std::string &symbol, uint64_t *offset,
                 uint32_t *flags);

}  // namespace loader
}  // namespace mcfm
