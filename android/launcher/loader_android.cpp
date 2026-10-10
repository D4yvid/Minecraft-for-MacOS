#include "loader_android.h"

#include <dlfcn.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <mutex>

#include "audio_toolbox.h"
#include "darwin.h"

namespace mcfm {
namespace loader {
namespace {

// The runtime's libunwind (patched for Android, runtime/patch_llvm.py) asks these callbacks for
// addresses outside every ELF object.
struct UnwindSections {
  uintptr_t dso_base, dwarf_section;
  size_t dwarf_section_length;
  uintptr_t compact_unwind_section;
  size_t compact_unwind_section_length;
};
extern "C" int __unw_add_find_dynamic_unwind_sections(int (*)(uintptr_t, UnwindSections *));

struct Range { uintptr_t lo, hi; UnwindSections sections; };
constexpr int kMaxImages = 8;
Range g_ranges[kMaxImages];
int g_count = 0;
std::mutex g_lock;

int find_sections(uintptr_t addr, UnwindSections *info) {
  std::lock_guard<std::mutex> hold(g_lock);
  for (int i = 0; i < g_count; i++)
    if (addr >= g_ranges[i].lo && addr < g_ranges[i].hi) {
      *info = g_ranges[i].sections;
      return 1;
    }
  return 0;
}

// The slide: where __TEXT is minus where it wanted to be (read from the mapped load commands).
intptr_t slide_of(uintptr_t header) {
  uint32_t ncmds;
  std::memcpy(&ncmds, reinterpret_cast<const void *>(header + 16), 4);
  uintptr_t cmd = header + 32;
  for (uint32_t i = 0; i < ncmds; i++) {
    uint32_t type, size;
    std::memcpy(&type, reinterpret_cast<const void *>(cmd), 4);
    std::memcpy(&size, reinterpret_cast<const void *>(cmd + 4), 4);
    if (type == 0x19 && std::strncmp(reinterpret_cast<const char *>(cmd + 8), "__TEXT", 16) == 0) {  // LC_SEGMENT_64
      uint64_t vmaddr;
      std::memcpy(&vmaddr, reinterpret_cast<const void *>(cmd + 24), 8);
      return static_cast<intptr_t>(header - vmaddr);
    }
    cmd += size;
  }
  return 0;
}

// open_library's handle for libSystem: not a dlopen handle, symbol() looks into the table.
char g_libsystem;
constexpr const char kStubPrefix[] = "mcfm_stub_";

bool is_stub(const std::string &name) { return name.compare(0, sizeof kStubPrefix - 1, kStubPrefix) == 0; }

// The OpenGLES framework's gl* functions come from the system's GLES 3: the library's exports;
// for an extension function that GLES 3 has in core (glBindRenderbufferOES, glGenVertexArraysOES,
// glDiscardFramebufferEXT = glInvalidateFramebuffer), the core function; only then
// eglGetProcAddress (it can return GLES 1 entry points for OES names, which crash under a GLES 3
// context). An unresolved gl* name stays unresolved: a draw call must never go to a stub.
void *gles_symbol(const std::string &name) {
  static void *gles = dlopen("libGLESv3.so", RTLD_NOW);
  static void *egl = dlopen("libEGL.so", RTLD_NOW);
  typedef void *(*GetProcAddress)(const char *);
  static GetProcAddress get_proc = egl ? reinterpret_cast<GetProcAddress>(dlsym(egl, "eglGetProcAddress")) : nullptr;
  if (!gles) return nullptr;
  if (void *p = dlsym(gles, name.c_str())) return p;
  std::string core;
  if (name == "glDiscardFramebufferEXT") core = "glInvalidateFramebuffer";
  else if (name.size() > 3 && name.compare(name.size() - 3, 3, "OES") == 0) core = name.substr(0, name.size() - 3);
  if (!core.empty())
    if (void *p = dlsym(gles, core.c_str())) return p;
  return get_proc ? get_proc(name.c_str()) : nullptr;
}

bool is_gl_function(const std::string &name) { return name.compare(0, 2, "gl") == 0; }

// open_library's handle for libc++: symbol() looks into the runtime's generated table.
char g_runtime;

}  // namespace

size_t AndroidLoaderOS::page_size() { return static_cast<size_t>(getpagesize()); }

uint8_t *AndroidLoaderOS::reserve(size_t size) {
  void *p = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? nullptr : static_cast<uint8_t *>(p);
}

bool AndroidLoaderOS::map_file(uint8_t *at, size_t size, int prot, int fd, uint64_t offset) {
  size_t page = page_size();
  size_t length = (size + page - 1) & ~(page - 1);
  if (mmap(at, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0) != at) return false;
  for (size_t done = 0; done < size;) {
    ssize_t n = pread(fd, at + done, size - done, static_cast<off_t>(offset + done));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    done += static_cast<size_t>(n);
  }
  // Mach VM_PROT_* and PROT_* share values (read 1, write 2, execute 4).
  if (prot & PROT_EXEC) __builtin___clear_cache(reinterpret_cast<char *>(at), reinterpret_cast<char *>(at + length));
  return mprotect(at, length, prot) == 0;
}

bool AndroidLoaderOS::map_zero(uint8_t *at, size_t size, int prot) {
  return mmap(at, size, prot, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0) == at;
}

void *AndroidLoaderOS::open_library(const std::string &name) {
  if (name == "libSystem") return &g_libsystem;
  if (name == "libc++") return &g_runtime;
  if (name == "libz") return dlopen("libz.so", RTLD_NOW);
  if (!is_stub(name)) {
    log("no Android library for " + name);
    return nullptr;
  }
  // Stubs need libmcfm_stubrt.so: load it from our directory first, so its soname is known.
  static bool stubrt = dlopen((dir_ + "/libmcfm_stubrt.so").c_str(), RTLD_NOW | RTLD_GLOBAL) != nullptr;
  (void)stubrt;
  std::string path = dir_ + "/" + name + ".so";
  void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) log("cannot load " + path + ": " + dlerror());
  else stubs_.insert(h);
  if (h && name == "mcfm_stub_OpenGLES") gles_stub_ = h;
  if (h && name == "mcfm_stub_AudioToolbox") audio_stub_ = h;
  return h;
}

void *AndroidLoaderOS::symbol(void *library, const std::string &name) {
  if (library == &g_libsystem) return mcfm_darwin_symbol(name.c_str());
  if (library == &g_runtime) return mcfm_runtime_symbol(name.c_str());
  // OpenGLES: the system's GLES first (EAGL's classes and constants stay stubbed).
  if (library == gles_stub_ && is_gl_function(name)) return gles_symbol(name);
  // AudioToolbox: ours on AAudio (audio_toolbox.cpp), the stub for anything else.
  if (library == audio_stub_)
    if (void *p = mcfm::audio::audio_symbol(name.c_str())) return p;
  // Stubs export the Mach-O names (leading '_'), which keeps them apart from bionic's.
  if (stubs_.count(library)) return dlsym(library, ("_" + name).c_str());
  return dlsym(library, name.c_str());
}

void *AndroidLoaderOS::flat_symbol(const std::string &name) {
  if (void *p = mcfm_darwin_symbol(name.c_str())) return p;
  if (void *p = mcfm_runtime_symbol(name.c_str())) return p;
  return dlsym(RTLD_DEFAULT, name.c_str());
}

bool AndroidLoaderOS::register_unwind(uintptr_t header, uintptr_t text_lo, uintptr_t text_hi, uintptr_t compact_unwind,
                                      size_t compact_size, uintptr_t eh_frame, size_t eh_size) {
  {
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_count == kMaxImages) return false;
    if (g_count == 0 && __unw_add_find_dynamic_unwind_sections(find_sections) != 0) return false;
    UnwindSections s = {header, eh_frame, eh_size, compact_unwind, compact_size};
    g_ranges[g_count++] = Range{text_lo, text_hi, s};
  }
  // Without the lock: callbacks may unwind (find_sections takes it). Called once the image is mapped and bound, before its initializers: those may register
  // _dyld_register_func_for_add_image callbacks, which see this image.
  mcfm_darwin_add_image(reinterpret_cast<const void *>(header), slide_of(header));
  return true;
}

void AndroidLoaderOS::log(const std::string &line) { std::fprintf(stderr, "mcfm: %s\n", line.c_str()); }

}  // namespace loader
}  // namespace mcfm
