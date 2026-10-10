// decode_fixups / find_export on crafted opcode streams (dyld's LC_DYLD_INFO format).
#include "fixups.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace mcfm::loader;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

namespace {

typedef std::vector<uint8_t> Bytes;

Bytes str(const char *s) {  // a C string with its terminator
  Bytes b;
  while (*s) b.push_back(static_cast<uint8_t>(*s++));
  b.push_back(0);
  return b;
}
Bytes operator+(Bytes a, const Bytes &b) { a.insert(a.end(), b.begin(), b.end()); return a; }

struct Image {
  Bytes file = Bytes(0x10000, 0);
  MachOFile m;
  Image() {
    Segment text = {"__TEXT", 0x100000000, 0x4000, 0, 0x4000, 5, 5, {}};
    Segment data = {"__DATA", 0x100004000, 0x4000, 0x4000, 0x4000, 3, 3, {}};
    m.segments.push_back(text);
    m.segments.push_back(data);
    m.dylibs.push_back("/usr/lib/libc++.1.dylib");
    m.dylibs.push_back("@rpath/mcfm_stub_UIKit.dylib");
    m.weak_dylibs.assign(2, false);
    m.has_dyld_info = true;
  }
  void set(uint32_t *off, uint32_t *size, uint32_t at, const Bytes &stream) {
    *off = at;
    *size = static_cast<uint32_t>(stream.size());
    for (size_t i = 0; i < stream.size(); i++) file[at + i] = stream[i];
  }
  void rebase(const Bytes &s) { set(&m.dyld_info.rebase_off, &m.dyld_info.rebase_size, 0x8000, s); }
  void bind(const Bytes &s) { set(&m.dyld_info.bind_off, &m.dyld_info.bind_size, 0x9000, s); }
  void lazy(const Bytes &s) { set(&m.dyld_info.lazy_bind_off, &m.dyld_info.lazy_bind_size, 0xA000, s); }
  void weak(const Bytes &s) { set(&m.dyld_info.weak_bind_off, &m.dyld_info.weak_bind_size, 0xB000, s); }
  void exports(const Bytes &s) { set(&m.dyld_info.export_off, &m.dyld_info.export_size, 0xC000, s); }
  bool decode(std::vector<Fixup> *out, std::string *err) { return decode_fixups(file.data(), file.size(), m, out, err); }
};

}  // namespace

int main() {
  std::vector<Fixup> f;
  std::string err;
  {  // rebases: set type, segment+offset, imm times, imm-scaled add, add-addr, times-skipping
    Image im;
    im.rebase(Bytes{0x11, 0x21, 0x10, 0x52, 0x42, 0x51, 0x70, 0x08, 0x80, 0x02, 0x08, 0x00});
    EXPECT(im.decode(&f, &err));
    // 0x10, 0x18 (imm times 2) -> cur 0x20; +16 -> 0x30: rebase -> 0x38; add-addr rebase at
    // 0x38, then +8+8 -> 0x48; times-skipping 2 x {rebase; +8+8}: 0x48, 0x58
    uint64_t want[] = {0x10, 0x18, 0x30, 0x38, 0x48, 0x58};
    EXPECT(f.size() == 6);
    for (size_t i = 0; i < 6 && i < f.size(); i++) EXPECT(f[i].kind == FixupKind::Rebase && f[i].segment == 1 && f[i].offset == want[i]);
  }
  {  // binds: ordinal, symbol, type, negative addend, do-bind, special ordinal, weak import,
     // imm-scaled add, uleb-times-skipping
    Image im;
    im.bind(Bytes{0x11} + Bytes{0x40} + str("_foo") + Bytes{0x51, 0x60, 0x78, 0x71, 0x20, 0x90} +
            Bytes{0x3E} + Bytes{0x41} + str("_w") + Bytes{0x60, 0x00, 0xB1} +
            Bytes{0x12} + Bytes{0x40} + str("_r") + Bytes{0xC0, 0x02, 0x08, 0x00});
    EXPECT(im.decode(&f, &err));
    EXPECT(f.size() == 4);
    if (f.size() == 4) {
      EXPECT(f[0].kind == FixupKind::Bind && f[0].segment == 1 && f[0].offset == 0x20 && f[0].ordinal == 1 &&
             f[0].symbol == "_foo" && f[0].addend == -8 && !f[0].weak_import);
      EXPECT(f[1].offset == 0x28 && f[1].ordinal == -2 && f[1].symbol == "_w" && f[1].weak_import && f[1].addend == 0);
      // imm-scaled: bind at 0x28, then +8*1+8 -> 0x38; times-skipping: 0x38, 0x38+8+8
      EXPECT(f[2].offset == 0x38 && f[2].ordinal == 2 && f[2].symbol == "_r");
      EXPECT(f[3].offset == 0x48 && f[3].symbol == "_r");
    }
  }
  {  // lazy stream: DONE ends one entry, decoding continues; weak stream: strong-definition
     // records (flag 8) produce nothing, a weak bind entry produces a WeakBind
    Image im;
    im.lazy(Bytes{0x71, 0x40, 0x12, 0x40} + str("_a") + Bytes{0x90, 0x00, 0x71, 0x48, 0x11, 0x40} + str("_b") + Bytes{0x90, 0x00});
    im.weak(Bytes{0x48} + str("_strong") + Bytes{0x40} + str("_Znwm") + Bytes{0x51, 0x71, 0x50, 0x90, 0x00});
    EXPECT(im.decode(&f, &err));
    EXPECT(f.size() == 3);
    if (f.size() == 3) {
      EXPECT(f[0].kind == FixupKind::LazyBind && f[0].offset == 0x40 && f[0].ordinal == 2 && f[0].symbol == "_a");
      EXPECT(f[1].kind == FixupKind::LazyBind && f[1].offset == 0x48 && f[1].ordinal == 1 && f[1].symbol == "_b");
      EXPECT(f[2].kind == FixupKind::WeakBind && f[2].offset == 0x50 && f[2].symbol == "_Znwm");
    }
  }
  {  // order: rebases, binds, lazy binds, weak binds (dyld's)
    Image im;
    im.weak(Bytes{0x40} + str("_w") + Bytes{0x51, 0x71, 0x08, 0x90, 0x00});
    im.lazy(Bytes{0x71, 0x10, 0x11, 0x40} + str("_l") + Bytes{0x90, 0x00});
    im.bind(Bytes{0x11, 0x40} + str("_b") + Bytes{0x51, 0x71, 0x18, 0x90, 0x00});
    im.rebase(Bytes{0x11, 0x21, 0x20, 0x51, 0x00});
    EXPECT(im.decode(&f, &err) && f.size() == 4);
    if (f.size() == 4)
      EXPECT(f[0].kind == FixupKind::Rebase && f[1].kind == FixupKind::Bind && f[2].kind == FixupKind::LazyBind &&
             f[3].kind == FixupKind::WeakBind);
  }
  struct Bad { const char *what; Bytes bind; Bytes rebase; };
  Bad bad[] = {
      {"threaded", Bytes{0xD0, 0x00}, Bytes{}},
      {"bind past the segment", Bytes{0x11, 0x40} + str("_x") + Bytes{0x71, 0xFC, 0x7F, 0x90, 0x00}, Bytes{}},
      {"segment index", Bytes{}, Bytes{0x11, 0x25, 0x00, 0x51, 0x00}},
      {"rebase into read-only __TEXT", Bytes{}, Bytes{0x11, 0x20, 0x00, 0x51, 0x00}},
      {"truncated uleb", Bytes{0x71, 0x80}, Bytes{}},
      {"unterminated symbol", Bytes{0x40, 'a', 'b'}, Bytes{}},
      {"uleb128 wider than 64 bits", Bytes{0x71, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x90, 0x00}, Bytes{}},
      {"dylib ordinal beyond the dylib list", Bytes{0x20, 0x81, 0x80, 0x80, 0x80, 0x10, 0x40} + str("_x") + Bytes{0x71, 0x10, 0x90, 0x00}, Bytes{}},
      {"bind type other than pointer", Bytes{0x11, 0x40} + str("_x") + Bytes{0x52, 0x71, 0x10, 0x90, 0x00}, Bytes{}},
      {"rebase type other than pointer", Bytes{}, Bytes{0x12, 0x21, 0x10, 0x51, 0x00}},
  };
  for (const Bad &b : bad) {
    Image im;
    if (!b.bind.empty()) im.bind(b.bind);
    if (!b.rebase.empty()) im.rebase(b.rebase);
    err.clear();
    bool ok = im.decode(&f, &err);
    if (ok || err.empty()) std::printf("FAIL: %s accepted\n", b.what), fails++;
  }
  {  // SLEB128 at the sign bit (10 bytes, the smallest int64) decodes without overflow
    Image im;
    im.bind(Bytes{0x11, 0x40} + str("_m") + Bytes{0x51, 0x60, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F, 0x71, 0x10, 0x90, 0x00});
    EXPECT(im.decode(&f, &err) && f.size() == 1 && f[0].addend == INT64_MIN);
  }
  {  // export flags are reported (re-export 0x8); a terminal size past the trie is not followed
    Image im;
    im.exports(Bytes{0x00, 0x01} + str("_r") + Bytes{6} + Bytes{0x03, 0x08, 0x01, 0x00});
    uint64_t off = 0;
    uint32_t flags = 0;
    EXPECT(find_export(im.file.data(), im.file.size(), im.m, "_r", &off, &flags) && flags == 0x8);
    Image bad;
    bad.exports(Bytes{0x7F, 0x01} + str("_r") + Bytes{6});
    EXPECT(!find_export(bad.file.data(), bad.file.size(), bad.m, "_r", &off, &flags));
  }
  {  // export trie: root -> "_a" (0x100), "_bc" (0x200)
    Image im;
    im.exports(Bytes{0x00, 0x02} + str("_a") + Bytes{11} + str("_bc") + Bytes{16} +
               Bytes{0x03, 0x00, 0x80, 0x02, 0x00} + Bytes{0x03, 0x00, 0x80, 0x04, 0x00});
    uint64_t off = 0;
    uint32_t flags = 99;
    EXPECT(find_export(im.file.data(), im.file.size(), im.m, "_a", &off, &flags) && off == 0x100 && flags == 0);
    EXPECT(find_export(im.file.data(), im.file.size(), im.m, "_bc", &off, &flags) && off == 0x200);
    EXPECT(!find_export(im.file.data(), im.file.size(), im.m, "_b", &off, &flags));
    EXPECT(!find_export(im.file.data(), im.file.size(), im.m, "_x", &off, &flags));
  }
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("macho_fixups_test: all passed\n");
  return 0;
}
