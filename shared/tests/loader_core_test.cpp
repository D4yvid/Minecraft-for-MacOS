// load_image with a fake OS (bytes copied, nothing executed): every fixup slot, weak-bind
// policy, hooks, unwind registration, and errors. argv[1]: the converted loader fixture,
// argv[2]: its symbols.txt (unslid addresses).
#include "loader.h"

#include "macho_builder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace mcfm::loader;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

namespace {

uint64_t fake_address(const std::string &name) {  // a distinct, recognisable "address" per name
  uint64_t h = 1469598103934665603ull;
  for (char c : name) h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
  return 0x700000000000ull | (h & 0xFFFFFFF0ull);
}

struct FakeOS : LoaderOS {
  const std::vector<uint8_t> &file;
  std::set<std::string> missing;   // names symbol() does not find
  bool host_has_weak = true;       // flat_symbol finds weak-bound names in "the host"
  bool signature = false;
  uintptr_t unwind_header = 0, unwind_lo = 0, unwind_hi = 0, unwind_compact = 0;
  std::vector<std::string> libraries;
  std::vector<void *> blocks;
  explicit FakeOS(const std::vector<uint8_t> &f) : file(f) {}
  ~FakeOS() override { for (void *b : blocks) std::free(b); }
  size_t page_size() override { return 0x4000; }
  uint8_t *reserve(size_t size) override {
    void *p = nullptr;
    if (posix_memalign(&p, 0x4000, size)) return nullptr;
    std::memset(p, 0xAB, size);  // not zero: proves map_zero / map_file cover what they must
    blocks.push_back(p);
    return static_cast<uint8_t *>(p);
  }
  bool register_code_signature(int, uint64_t, uint64_t) override { return signature = true; }
  bool map_file(uint8_t *at, size_t size, int, int, uint64_t off) override {
    if (off + size > file.size()) return false;
    std::memcpy(at, file.data() + off, size);
    return true;
  }
  bool map_zero(uint8_t *at, size_t size, int) override { std::memset(at, 0, size); return true; }
  void *open_library(const std::string &name) override {
    libraries.push_back(name);
    return reinterpret_cast<void *>(0x1000 + libraries.size());
  }
  void *symbol(void *, const std::string &name) override {
    return missing.count(name) ? nullptr : reinterpret_cast<void *>(fake_address(name));
  }
  void *flat_symbol(const std::string &name) override {
    return host_has_weak ? reinterpret_cast<void *>(fake_address("host:" + name)) : nullptr;
  }
  void register_unwind(uintptr_t header, uintptr_t lo, uintptr_t hi, uintptr_t compact, size_t, uintptr_t, size_t) override {
    unwind_header = header; unwind_lo = lo; unwind_hi = hi; unwind_compact = compact;
  }
  void log(const std::string &) override {}
};

std::vector<uint8_t> slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

uint64_t symbol_address(const char *symbols_txt, const std::string &name) {
  std::ifstream f(symbols_txt);
  std::string n, a;
  while (f >> n >> a)
    if (n == name) return std::stoull(a, nullptr, 16);
  return 0;
}

uint64_t read64(const uint8_t *p) { uint64_t v; std::memcpy(&v, p, 8); return v; }

std::string dlsym_name(const std::string &symbol) {  // as the loader asks: without a leading '_'
  return !symbol.empty() && symbol[0] == '_' ? symbol.substr(1) : symbol;
}

// What each fixup slot must hold after loading, computed from the decoded list.
std::map<uint64_t, uint64_t> expected_slots(const std::vector<uint8_t> &file, const MachOFile &m, intptr_t slide,
                                            bool host_has_weak) {
  std::vector<Fixup> fx;
  std::string err;
  decode_fixups(file.data(), file.size(), m, &fx, &err);
  std::map<uint64_t, uint64_t> want;
  for (const Fixup &x : fx) {
    const Segment &s = m.segments[x.segment];
    uint64_t addr = s.vmaddr + x.offset;
    switch (x.kind) {
      case FixupKind::Rebase: want[addr] = read64(file.data() + s.fileoff + x.offset) + slide; break;
      case FixupKind::Bind:
      case FixupKind::LazyBind:
        want[addr] = x.symbol == "dyld_stub_binder" ? 0 : fake_address(dlsym_name(x.symbol)) + x.addend;
        break;
      case FixupKind::WeakBind:
        if (host_has_weak) want[addr] = fake_address("host:" + dlsym_name(x.symbol)) + x.addend;
        break;  // no host definition: the slot keeps what rebase/bind put there (the image's own)
    }
  }
  return want;
}

int replacement_marker;

void check_fixture(const char *image_path, const char *symbols_txt, bool host_has_weak) {
  std::vector<uint8_t> file = slurp(image_path);
  FakeOS os(file);
  os.host_has_weak = host_has_weak;
  os.missing.insert("dyld_stub_binder");  // gone from modern libSystem; never called (all binds eager)
  uint64_t answer = symbol_address(symbols_txt, "_fixture_answer");
  EXPECT(answer != 0);
  const uintptr_t hook_addresses[] = {static_cast<uintptr_t>(answer)};
  const void *const hook_replacements[] = {&replacement_marker};
  LoadOptions opts;
  opts.hook_addresses = hook_addresses;
  opts.hook_replacements = hook_replacements;
  opts.hook_count = 1;
  opts.run_initializers = false;
  Image img;
  std::string err;
  bool ok = load_image(os, -1, file.data(), file.size(), opts, &img, &err);
  if (!ok) std::printf("load_image: %s\n", err.c_str());
  EXPECT(ok);
  if (!ok) return;
  EXPECT(os.signature);
  const Segment *text = img.macho.segment("__TEXT");
  EXPECT(text && img.header == reinterpret_cast<uint8_t *>(text->vmaddr + img.slide));
  EXPECT(os.unwind_header == reinterpret_cast<uintptr_t>(img.header) && os.unwind_lo == img.text_lo && os.unwind_hi == img.text_hi);
  const Section *ui = img.macho.section("__TEXT", "__unwind_info");
  EXPECT(ui && os.unwind_compact == ui->addr + img.slide);
  std::map<uint64_t, uint64_t> want = expected_slots(file, img.macho, img.slide, host_has_weak);
  EXPECT(want.size() > 20);
  int wrong = 0;
  for (auto &w : want) {
    uint64_t got = read64(reinterpret_cast<const uint8_t *>(w.first + img.slide));
    if (got != w.second && wrong++ < 5) std::printf("FAIL slot 0x%llx = 0x%llx, want 0x%llx\n", (unsigned long long)w.first, (unsigned long long)got, (unsigned long long)w.second);
  }
  EXPECT(wrong == 0);
  EXPECT(os.libraries.size() >= 3);  // libc++, libSystem, mcfm_stub_FakeKit (each opened once)
  std::set<std::string> unique(os.libraries.begin(), os.libraries.end());
  EXPECT(unique.size() == os.libraries.size() && unique.count("libc++") && unique.count("libSystem") && unique.count("mcfm_stub_FakeKit"));

  // A hook address that the image was not converted for is refused.
  FakeOS os2(file);
  const uintptr_t wrong_hook[] = {static_cast<uintptr_t>(symbol_address(symbols_txt, "_fixture_thrower"))};
  opts.hook_addresses = wrong_hook;
  err.clear();
  EXPECT(!load_image(os2, -1, file.data(), file.size(), opts, &img, &err) && err.find("hook") != std::string::npos);
}

// A crafted image: one weak import nobody provides and one normal import.
Builder crafted(bool weak) {
  Builder s;
  s.segment("__TEXT", 0x100000000, 0x4000, 0, 0x4000, 5, {{"__text", 0x100}});
  s.segment("__DATA", 0x100004000, 0x4000, 0x4000, 0x4000, 3, {{"__data", 0x100}});
  size_t di = s.begin(0x80000022, 48);
  std::vector<uint8_t> bind = {0x11, static_cast<uint8_t>(0x40 | (weak ? 1 : 0)), '_', 'g', 'o', 'n', 'e', 0, 0x51, 0x71, 0x10, 0x90,
                               0x40, '_', 'h', 'e', 'r', 'e', 0, 0x90, 0x00};
  for (size_t i = 0; i < bind.size(); i++) s.b[0x9000 + i] = bind[i];
  s.put<uint32_t>(di + 16, 0x9000);
  s.put<uint32_t>(di + 20, static_cast<uint32_t>(bind.size()));
  s.dylib(0xC, "@rpath/mcfm_stub_FakeKit.dylib");
  s.finish();
  return s;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 3) { std::printf("usage: loader_core_test <fixture image> <symbols.txt>\n"); return 2; }
  check_fixture(argv[1], argv[2], true);
  check_fixture(argv[1], argv[2], false);

  Builder w = crafted(true);
  FakeOS os(w.b);
  os.missing.insert("gone");
  LoadOptions opts;
  opts.run_initializers = false;
  Image img;
  std::string err;
  EXPECT(load_image(os, -1, w.b.data(), w.b.size(), opts, &img, &err));
  EXPECT(read64(reinterpret_cast<const uint8_t *>(0x100004010 + img.slide)) == 0);  // weak import -> 0
  EXPECT(read64(reinterpret_cast<const uint8_t *>(0x100004018 + img.slide)) == fake_address("here"));
  EXPECT(read64(reinterpret_cast<const uint8_t *>(0x100004020 + img.slide)) == 0);  // zero-filled data stays zero

  Builder n = crafted(false);
  FakeOS os2(n.b);
  os2.missing.insert("gone");
  err.clear();
  EXPECT(!load_image(os2, -1, n.b.data(), n.b.size(), opts, &img, &err));
  EXPECT(err.find("mcfm_stub_FakeKit") != std::string::npos && err.find("_gone") != std::string::npos);

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("loader_core_test: all passed\n");
  return 0;
}
