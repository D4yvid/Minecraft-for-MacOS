// loader_run <image> <symbols.txt>: loads the loader fixture with our loader on macOS and runs
// its code: initializer, a C++ exception caught inside the image, a hook, operator new.
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "loader.h"
#include "loader_macos.h"

using namespace mcfm::loader;

static int replacement(int x) { return 1000 + x; }

static uint64_t symbol_address(const char *path, const std::string &name) {
  std::ifstream f(path);
  std::string n, a;
  while (f >> n >> a)
    if (n == name) return std::stoull(a, nullptr, 16);
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 3) return 2;
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  int fd = open(argv[1], O_RDONLY);
  std::string dir = argv[1];
  dir = dir.substr(0, dir.rfind('/'));
  MacLoaderOS os(dir);
  const uintptr_t hooks[] = {static_cast<uintptr_t>(symbol_address(argv[2], "_fixture_answer"))};
  const void *const replacements[] = {reinterpret_cast<void *>(&replacement)};
  LoadOptions opts;
  opts.hook_addresses = hooks;
  opts.hook_replacements = replacements;
  opts.hook_count = 1;
  size_t initializers = 0;
  opts.initializers_run = &initializers;
  opts.argc = argc;
  opts.argv = const_cast<const char **>(argv);
  Image img;
  std::string err;
  if (!load_image(os, fd, file.data(), file.size(), opts, &img, &err)) {
    std::printf("load failed: %s\n", err.c_str());
    return 1;
  }
  auto thrower = reinterpret_cast<int (*)()>(symbol_address(argv[2], "_fixture_thrower") + img.slide);
  auto answer = reinterpret_cast<int (*)(int)>(symbol_address(argv[2], "_fixture_answer") + img.slide);
  auto init_answer = reinterpret_cast<int *>(symbol_address(argv[2], "_fixture_init_answer") + img.slide);
  std::printf("thrower=%d answer=%d init_answer=%d\n", thrower(), answer(5), *init_answer);
  std::printf("initializers=%zu\n", initializers);
  // operator new: the weak-bound slots must hold libc++'s.
  std::vector<Fixup> fx;
  decode_fixups(file.data(), file.size(), img.macho, &fx, &err);
  void *libcxx_new = dlsym(RTLD_DEFAULT, "_Znwm");
  for (const Fixup &x : fx)
    if (x.kind == FixupKind::WeakBind && x.symbol == "__Znwm") {
      uint64_t v;
      std::memcpy(&v, reinterpret_cast<void *>(img.macho.segments[x.segment].vmaddr + img.slide + x.offset), 8);
      std::printf("operator_new=%s\n", reinterpret_cast<void *>(v) == libcxx_new ? "libc++" : "other");
    }
  return 0;
}
