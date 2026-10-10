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
//   --boot --data <dir> --home <dir> [--frames N] [--screenshot <ppm>]
//                             the game: the launcher's hooks and LC_UUID check at load, then the
//                             engine boots and renders N frames (boot.cpp); implies --gl
#include <EGL/egl.h>
#include <dlfcn.h>
#include <unwind.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <typeinfo>
#include <iterator>
#include <string>
#include <vector>

#include "boot.h"
#include "darwin.h"
#include "hook_table.h"
#include "macho_uuid.h"
#include "seams.h"
#include "fixups.h"
#include "loader.h"
#include "loader_android.h"

using namespace mcfm::loader;

namespace {

int fixture_hook(int x) { return 1000 + x; }

// For std::terminate's backtrace: the game image's range (pcs there print unslid, for IDA).
uintptr_t g_image_lo = 0, g_image_hi = 0;
intptr_t g_image_slide = 0;

_Unwind_Reason_Code print_frame(_Unwind_Context *context, void *depth) {
  uintptr_t pc = _Unwind_GetIP(context);
  int n = (*static_cast<int *>(depth))++;
  if (pc >= g_image_lo && pc < g_image_hi) {
    std::fprintf(stderr, "mcfm:   #%02d game 0x%lx\n", n, static_cast<unsigned long>(pc - g_image_slide));
  } else {
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(pc), &info) && info.dli_fname)
      std::fprintf(stderr, "mcfm:   #%02d %s+0x%lx (%s)\n", n, info.dli_fname,
                   static_cast<unsigned long>(pc - reinterpret_cast<uintptr_t>(info.dli_fbase)), info.dli_sname ? info.dli_sname : "?");
    else
      std::fprintf(stderr, "mcfm:   #%02d 0x%lx\n", n, static_cast<unsigned long>(pc));
  }
  return n < 40 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

// An uncaught exception: what it was and where it was thrown (still on the stack: __cxa_throw
// calls terminate before unwinding), with our unwinder, which knows the game's frames.
[[noreturn]] void on_terminate() {
  const char *what = "unknown";
  if (std::exception_ptr e = std::current_exception()) {
    try {
      std::rethrow_exception(e);
    } catch (const std::exception &x) {
      what = x.what();
    } catch (...) {
    }
  }
  std::fprintf(stderr, "mcfm: terminate: uncaught exception (%s); backtrace:\n", what);
  int depth = 0;
  _Unwind_Backtrace(print_frame, &depth);
  std::abort();
}

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
  std::fprintf(stderr, "mcfm-run: %s\nusage: mcfm-run <image> [--hook <addr>]... [--gl] [--initializers-only] "
               "[--call <sym>[=<int>]] [--int <sym>] [--weak <sym>]\n"
               "       mcfm-run <image> --boot --data <dir> --home <dir> [--frames N] [--screenshot <ppm>]\n", message);
  return 2;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int mcfm_run_main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  std::set_terminate(on_terminate);
  if (argc < 2) return usage("no image");
  std::string path = argv[1];
  std::vector<uintptr_t> hooks;
  std::vector<const void *> replacements;
  bool boot = false;
  mcfm::android::BootOptions boot_options;
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--boot") boot = true;
    if (i + 1 < argc && a == "--data") boot_options.data_dir = argv[i + 1];
    if (i + 1 < argc && a == "--home") boot_options.home_dir = argv[i + 1];
    if (i + 1 < argc && a == "--frames") boot_options.frames = std::atol(argv[i + 1]);
    if (i + 1 < argc && a == "--screenshot") boot_options.screenshot = argv[i + 1];
  }
  if (boot && (boot_options.data_dir.empty() || boot_options.home_dir.empty() || boot_options.frames < 1))
    return usage("--boot needs --data <dir>, --home <dir> and --frames N > 0");
  bool gl = boot;
  for (int i = 2; i < argc; i++) gl |= std::strcmp(argv[i], "--gl") == 0;
  if (gl && !make_gl_context(boot_options.width, boot_options.height)) {
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
  std::vector<const char *> hook_names;
  if (boot) {  // the launcher's seams (shared/launcher/seams.cpp), as mcfm-launch installs them
    size_t n = 0;
    const mcfm::launcher::Hook *h = mcfm::launcher::hooks(&n);
    for (size_t i = 0; i < n; i++) {
      hooks.push_back(h[i].address);
      replacements.push_back(h[i].replacement);
      hook_names.push_back(h[i].name);
    }
  }
  LoadOptions opts;
  opts.hook_addresses = hooks.data();
  opts.hook_replacements = replacements.data();
  opts.hook_names = boot ? hook_names.data() : nullptr;
  opts.hook_count = hooks.size();
  // The game's addresses are only valid for its 0.15.10 build: check what was mapped.
  if (boot) opts.accept_header = [](const uint8_t *header) { return mcfm::is_expected_game_image(header); };
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
  g_image_slide = image.slide;
  for (const Segment &seg : image.macho.segments)
    if (seg.name == "__TEXT") {
      g_image_lo = seg.vmaddr + image.slide;
      g_image_hi = g_image_lo + seg.vmsize;
    }
  std::printf("mcfm: %zu initializers ran\n", initializers);
  mcfm_darwin_drain_main_queue();  // work the initializers queued for the main thread

  auto exported = [&](const std::string &name) -> uint8_t * {
    uint64_t offset = 0;
    if (!find_export(file.data(), file.size(), image.macho, "_" + name, &offset, nullptr)) return nullptr;
    return image.header + offset;
  };
  if (boot) return mcfm::android::boot(static_cast<uintptr_t>(image.slide), boot_options);
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
