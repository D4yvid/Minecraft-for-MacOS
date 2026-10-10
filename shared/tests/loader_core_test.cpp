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
  bool unwind_ok = true;           // register_unwind succeeds
  bool require_signature = false;
  std::set<std::string> unloadable;  // open_library fails for these
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
  bool register_code_signature(int, uint64_t, uint64_t, uint64_t) override { return signature = true; }
  bool map_file(uint8_t *at, size_t size, int, int, uint64_t off) override {
    // Like mmap: whole pages, so the bytes after `size` in the last page come from the file.
    size_t pages = (size + 0x3FFF) & ~size_t(0x3FFF);
    if (off + pages > file.size()) pages = file.size() - off;
    if (off + size > file.size()) return false;
    std::memcpy(at, file.data() + off, pages);
    return true;
  }
  bool requires_code_signature() override { return require_signature; }
  bool map_zero(uint8_t *at, size_t size, int) override { std::memset(at, 0, size); return true; }
  void *open_library(const std::string &name) override {
    if (unloadable.count(name)) return nullptr;
    libraries.push_back(name);
    return reinterpret_cast<void *>(0x1000 + libraries.size());
  }
  void *symbol(void *, const std::string &name) override {
    return missing.count(name) ? nullptr : reinterpret_cast<void *>(fake_address(name));
  }
  void *flat_symbol(const std::string &name) override {
    return host_has_weak ? reinterpret_cast<void *>(fake_address("host:" + name)) : nullptr;
  }
  bool register_unwind(uintptr_t header, uintptr_t lo, uintptr_t hi, uintptr_t compact, size_t, uintptr_t, size_t) override {
    unwind_header = header; unwind_lo = lo; unwind_hi = hi; unwind_compact = compact;
    return unwind_ok;
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
  const char *const names[] = {"thrower_hook"};
  opts.hook_addresses = wrong_hook;
  opts.hook_names = names;
  err.clear();
  EXPECT(!load_image(os2, -1, file.data(), file.size(), opts, &img, &err) && err.find("thrower_hook") != std::string::npos);
  // libunwind refuses the image: no load (exceptions would terminate the game).
  FakeOS os3(file);
  os3.unwind_ok = false;
  opts.hook_count = 0;
  err.clear();
  EXPECT(!load_image(os3, -1, file.data(), file.size(), opts, &img, &err) && err.find("unwind") != std::string::npos);
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

// A flexible crafted image: __DATA with `data_filesize` file bytes (the rest of its first file
// page is 0xCD), an optional extra __DATA section, bind/weak/export streams, one dylib.
struct Craft {
  uint64_t data_filesize = 0x4000;
  const char *extra_section = nullptr;
  uint32_t extra_flags = 0;
  std::vector<uint8_t> bind, weak, exports, extra_content;
  uint32_t dylib_cmd = 0xC;
  Builder build() const {
    Builder s;
    s.segment("__TEXT", 0x100000000, 0x4000, 0, 0x4000, 5, {{"__text", 0x100}});
    if (extra_section)
      s.segment("__DATA", 0x100004000, 0x4000, 0x4000, data_filesize, 3, {{extra_section, 8}}, extra_flags);
    else
      s.segment("__DATA", 0x100004000, 0x4000, 0x4000, data_filesize, 3, {{"__data", 0x10}});
    for (size_t i = 0x4000 + data_filesize; i < 0x8000; i++) s.b[i] = 0xCD;
    for (size_t i = 0; i < extra_content.size(); i++) s.b[0x4000 + i] = extra_content[i];
    size_t di = s.begin(0x80000022, 48);
    auto stream = [&](const std::vector<uint8_t> &v, size_t at, size_t field) {
      for (size_t i = 0; i < v.size(); i++) s.b[at + i] = v[i];
      s.put<uint32_t>(di + 8 + 4 * field, v.empty() ? 0 : static_cast<uint32_t>(at));
      s.put<uint32_t>(di + 12 + 4 * field, static_cast<uint32_t>(v.size()));
    };
    stream(bind, 0x9000, 2);
    stream(weak, 0x9400, 4);
    stream(exports, 0x9800, 8);
    s.dylib(dylib_cmd, "@rpath/mcfm_stub_FakeKit.dylib");
    s.finish();
    return s;
  }
};

bool load_crafted(const Craft &c, FakeOS *os_out, Image *img, std::string *err, bool run_init = false) {
  static Builder keep;
  keep = c.build();
  FakeOS os(keep.b);
  if (os_out) { os.unloadable = os_out->unloadable; os.host_has_weak = os_out->host_has_weak; os.require_signature = os_out->require_signature; }
  LoadOptions opts;
  opts.run_initializers = run_init;
  err->clear();
  bool ok = load_image(os, -1, keep.b.data(), keep.b.size(), opts, img, err);
  if (ok) {  // copy the mapped bytes we inspect out before FakeOS frees them
    static std::vector<uint8_t> mapped;
    mapped.assign(reinterpret_cast<uint8_t *>(0x100004000 + img->slide), reinterpret_cast<uint8_t *>(0x100008000 + img->slide));
    img->slide = reinterpret_cast<intptr_t>(mapped.data()) - 0x100004000;
  }
  return ok;
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

  {  // the last partial file page of a writable segment is zero after filesize, as dyld does
    Craft c;
    c.data_filesize = 0x100;
    EXPECT(load_crafted(c, nullptr, &img, &err));
    const uint8_t *data = reinterpret_cast<const uint8_t *>(0x100004000 + img.slide);
    bool zero = true;
    for (size_t i = 0x100; i < 0x4000; i++) zero &= data[i] == 0;
    EXPECT(zero);
  }
  {  // an initializer pointing outside __TEXT is refused before anything is called
    Craft c;
    c.extra_section = "__mod_init_func";
    c.extra_flags = 9;
    c.extra_content = {0, 0, 0, 0, 2, 0, 0, 0};  // 0x200000000
    EXPECT(!load_crafted(c, nullptr, &img, &err, true) && err.find("initializer") != std::string::npos);
  }
  {  // thread-local variables are not supported: refused with a message
    Craft c;
    c.extra_section = "__thread_vars";
    c.extra_flags = 0x13;
    EXPECT(!load_crafted(c, nullptr, &img, &err) && err.find("thread-local") != std::string::npos);
  }
  {  // a weak-linked library that is missing: its symbols bind to 0 (dyld does the same)
    Craft c;
    c.dylib_cmd = 0x80000018;
    c.bind = {0x11, 0x40, '_', 'h', 'e', 'r', 'e', 0, 0x51, 0x71, 0x08, 0x90, 0x00};
    FakeOS cfg(w.b);
    cfg.unloadable.insert("mcfm_stub_FakeKit");
    EXPECT(load_crafted(c, &cfg, &img, &err));
    EXPECT(read64(reinterpret_cast<const uint8_t *>(0x100004008 + img.slide)) == 0);
  }
  {  // a self-bind to a re-exported symbol is refused (not a definition in this image)
    Craft c;
    c.bind = {0x30, 0x40, '_', 'r', 0, 0x51, 0x71, 0x08, 0x90, 0x00};
    c.exports = {0x00, 0x01, '_', 'r', 0, 6, 0x03, 0x08, 0x01, 0x00};
    EXPECT(!load_crafted(c, nullptr, &img, &err) && err.find("_r") != std::string::npos);
  }
  {  // a weak bind with no host definition falls back to the image's own export
    Craft c;
    c.weak = {0x40, '_', 'w', 0, 0x51, 0x71, 0x20, 0x90, 0x00};
    c.exports = {0x00, 0x01, '_', 'w', 0, 6, 0x02, 0x00, 0x40, 0x00};
    FakeOS cfg(w.b);
    cfg.host_has_weak = false;
    EXPECT(load_crafted(c, &cfg, &img, &err));
    // own export: header (0x100000000 + slide) + 0x40; the copy's slide is the mapped-data slide
    uint64_t v = read64(reinterpret_cast<const uint8_t *>(0x100004020 + img.slide));
    EXPECT((v & 0xFFF) == 0x40 && v != 0);
  }
  {  // a host that requires signed code refuses an unsigned image
    Craft c;
    FakeOS cfg(w.b);
    cfg.require_signature = true;
    EXPECT(!load_crafted(c, &cfg, &img, &err) && err.find("signature") != std::string::npos);
  }

  {  // the caller rejects the mapped header (e.g. a different LC_UUID): no fixups, no load
    Builder keep = Craft().build();
    FakeOS os(keep.b);
    LoadOptions opts;
    opts.accept_header = [](const uint8_t *) { return false; };
    err.clear();
    EXPECT(!load_image(os, -1, keep.b.data(), keep.b.size(), opts, &img, &err) && err.find("rejected") != std::string::npos);
  }

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("loader_core_test: all passed\n");
  return 0;
}
