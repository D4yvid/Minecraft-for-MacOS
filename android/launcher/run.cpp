// mcfm_run_main(argc, argv), called by the mcfm-run executable (mcfm_run.c) after it loads
// libmcfm_launcher.so. mcfm-run <image> [options]: loads a converted Mach-O image on Android with our loader
// (docs/LAUNCHER.md, Stage 3a) and runs its initializers. Stubs and libmcfm_stubrt.so are taken
// from the image's directory. Options (repeatable, in order, after loading):
//   --call <symbol>[=<int>]   call the exported `int symbol(int)`, print "<symbol>=<result>"
//   --int <symbol>            print the exported int variable: "<symbol>=<value>"
//   --hook <hex address>      (before loading) replace the converted image's hook at that unslid
//                             address with `1000 + x` (the loader fixture's hook)
//   --weak <symbol>           print "weak <symbol>=runtime" when every weak-bound slot of that
//                             symbol holds the runtime's definition, else "=other"
//   --initializers-only       load, run the initializers, print their count, exit
//   --gl                      (before loading) make a GLES 3 context current: EGL pbuffer 1280x720
#include <EGL/egl.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "darwin.h"
#include "fixups.h"
#include "loader.h"
#include "loader_android.h"

using namespace mcfm::loader;

namespace {

int fixture_hook(int x) { return 1000 + x; }

// A GLES 3 context on a pbuffer (no window before Stage 3c), current on this thread.
bool make_gl_context(int width, int height) {
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(display, nullptr, nullptr)) return false;
  const EGLint config_attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                 EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                 EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
  EGLConfig config;
  EGLint count = 0;
  if (!eglChooseConfig(display, config_attrs, &config, 1, &count) || count < 1) return false;
  const EGLint surface_attrs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
  EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attrs);
  const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
  return surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT && eglMakeCurrent(display, surface, surface, context);
}

int usage(const char *message) {
  std::fprintf(stderr, "mcfm-run: %s\nusage: mcfm-run <image> [--hook <addr>]... [--initializers-only] "
               "[--call <sym>[=<int>]] [--int <sym>] [--weak <sym>]\n", message);
  return 2;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int mcfm_run_main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  if (argc < 2) return usage("no image");
  std::string path = argv[1];
  std::vector<uintptr_t> hooks;
  std::vector<const void *> replacements;
  for (int i = 2; i < argc; i++)
    if (std::strcmp(argv[i], "--gl") == 0 && !make_gl_context(1280, 720)) {
      std::fprintf(stderr, "mcfm-run: cannot create a GLES 3 context\n");
      return 1;
    }
  for (int i = 2; i + 1 < argc; i++)
    if (std::strcmp(argv[i], "--hook") == 0) {
      hooks.push_back(static_cast<uintptr_t>(std::strtoull(argv[++i], nullptr, 16)));
      replacements.push_back(reinterpret_cast<const void *>(&fixture_hook));
    }

  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0 || file.empty()) {
    std::fprintf(stderr, "mcfm-run: cannot read %s\n", path.c_str());
    return 2;
  }
  std::string dir = path.find('/') == std::string::npos ? "." : path.substr(0, path.rfind('/'));
  static AndroidLoaderOS os(dir);
  LoadOptions opts;
  opts.hook_addresses = hooks.data();
  opts.hook_replacements = replacements.data();
  opts.hook_count = hooks.size();
  size_t initializers = 0;
  opts.initializers_run = &initializers;
  opts.argc = argc;
  opts.argv = const_cast<const char **>(argv);
  opts.envp = const_cast<const char **>(environ);
  static const char *apple[] = {nullptr};  // Darwin's "apple" strings: none
  opts.apple = apple;
  Image image;
  std::string error;
  bool loaded = load_image(os, fd, file.data(), file.size(), opts, &image, &error);
  close(fd);
  if (!loaded) {
    std::fprintf(stderr, "mcfm-run: cannot load %s: %s\n", path.c_str(), error.c_str());
    return 1;
  }
  std::printf("mcfm: loaded (slide 0x%lx)\n", static_cast<unsigned long>(image.slide));
  std::printf("mcfm: %zu initializers ran\n", initializers);
  mcfm_darwin_drain_main_queue();  // work the initializers queued for the main thread

  auto exported = [&](const std::string &name) -> uint8_t * {
    uint64_t offset = 0;
    if (!find_export(file.data(), file.size(), image.macho, "_" + name, &offset, nullptr)) return nullptr;
    return image.header + offset;
  };
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--initializers-only") return 0;
    if (a == "--hook") { i++; continue; }
    if (a == "--gl") continue;
    if (i + 1 >= argc) return usage(("missing value for " + a).c_str());
    std::string v = argv[++i];
    if (a == "--call") {
      std::string name = v.substr(0, v.find('='));
      int arg = v.find('=') == std::string::npos ? 0 : std::atoi(v.c_str() + v.find('=') + 1);
      uint8_t *fn = exported(name);
      if (!fn) return usage(("no exported " + name).c_str());
      std::printf("%s=%d\n", name.c_str(), reinterpret_cast<int (*)(int)>(fn)(arg));
    } else if (a == "--int") {
      int *p = reinterpret_cast<int *>(exported(v));
      if (!p) return usage(("no exported " + v).c_str());
      std::printf("%s=%d\n", v.c_str(), *p);
    } else if (a == "--weak") {
      std::vector<Fixup> fixups;
      decode_fixups(file.data(), file.size(), image.macho, &fixups, &error);
      void *want = mcfm_runtime_symbol(v.c_str());
      int slots = 0, ours = 0;
      for (const Fixup &x : fixups)
        if (x.kind == FixupKind::WeakBind && x.symbol == "_" + v) {
          uint64_t value;
          std::memcpy(&value, reinterpret_cast<void *>(image.macho.segments[x.segment].vmaddr + image.slide + x.offset), 8);
          slots++;
          ours += reinterpret_cast<void *>(value) == want;
        }
      std::printf("weak %s=%s\n", v.c_str(), slots && slots == ours ? "runtime" : "other");
    } else {
      return usage(("unknown option " + a).c_str());
    }
  }
  return 0;
}
