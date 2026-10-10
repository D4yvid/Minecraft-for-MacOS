// mcfm-launch: runs the converted game image (make launcher) in a macOS window.
// usage: mcfm-launch [--print-hooks] [--frames N] [image]
// The LC_UUID is checked on the file before dlopen, the hook table is filled from a dyld
// add-image callback (before the game's initializers), then the engine boots in an ANGLE
// (OpenGL ES 3 on Metal) context. docs/LAUNCHER.md, Stage 1b.
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <dlfcn.h>
#include <mach-o/dyld.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "egl_min.h"
#include "engine.h"
#include "hook_table.h"
#include "macho_uuid.h"
#include "resize_math.h"
#include "seams.h"

using namespace mcfm::launcher;

namespace {

std::string executable_dir() {
  char buf[4096];
  uint32_t size = sizeof buf;
  if (_NSGetExecutablePath(buf, &size) != 0) return ".";
  std::string path(buf);
  return path.substr(0, path.rfind('/'));
}

bool read_header(const std::string &path, std::vector<char> *out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out->assign(64 * 1024, 0);
  f.read(out->data(), out->size());
  return f.gcount() >= 32;
}

uintptr_t g_slide = 0;
bool g_found = false;

// dyld calls this after mapping an image and before running its initializers.
void on_add_image(const mach_header *header, intptr_t slide) {
  if (g_found || !mcfm::is_expected_game_image(header)) return;
  g_found = true;
  g_slide = static_cast<uintptr_t>(slide);
  size_t n = 0;
  const Hook *h = hooks(&n);
  uintptr_t table = mcfm::hook_table_address(header);
  if (!table || mcfm::hook_table_capacity(header) < n) {
    std::fprintf(stderr, "mcfm: no hook table in the game image (rebuild with make launcher)\n");
    return;
  }
  void **slots = reinterpret_cast<void **>(table + g_slide);
  for (size_t i = 0; i < n; i++) slots[i] = h[i].replacement;
}

bool load_egl(const std::string &dir, Egl *e) {
  void *egl = dlopen((dir + "/libEGL.dylib").c_str(), RTLD_NOW);
  void *gles = dlopen((dir + "/libGLESv2.dylib").c_str(), RTLD_NOW);
  if (!egl || !gles) { std::fprintf(stderr, "mcfm: cannot load ANGLE: %s\n", dlerror()); return false; }
#define SYM(lib, field, name) e->field = reinterpret_cast<decltype(e->field)>(dlsym(lib, name)); if (!e->field) return false
  SYM(egl, GetPlatformDisplay, "eglGetPlatformDisplay");
  SYM(egl, Initialize, "eglInitialize");
  SYM(egl, ChooseConfig, "eglChooseConfig");
  SYM(egl, CreateContext, "eglCreateContext");
  SYM(egl, CreateWindowSurface, "eglCreateWindowSurface");
  SYM(egl, MakeCurrent, "eglMakeCurrent");
  SYM(egl, SwapBuffers, "eglSwapBuffers");
  SYM(egl, GetError, "eglGetError");
  SYM(gles, BindFramebuffer, "glBindFramebuffer");
  SYM(gles, Viewport, "glViewport");
#undef SYM
  return true;
}

HostInfo host_info(const std::string &game_data_dir) {
  NSString *base = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/MinecraftPE-mcfm"];
  HostInfo info;
  info.data_dir = game_data_dir;
  info.external_dir = std::string(base.UTF8String) + "/external";
  info.internal_dir = std::string(base.UTF8String) + "/internal";
  info.userdata_dir = std::string(base.UTF8String) + "/userdata";
  info.temp_dir = std::string(base.UTF8String) + "/tmp";
  for (const std::string &d : {info.external_dir, info.internal_dir, info.userdata_dir, info.temp_dir})
    [[NSFileManager defaultManager] createDirectoryAtPath:@(d.c_str()) withIntermediateDirectories:YES attributes:nil error:nil];
  info.region = "en_US";
  info.device_id = "mcfm-launcher";
  return info;
}

}  // namespace

@interface McfmView : NSView
@end
@implementation McfmView
- (CALayer *)makeBackingLayer { return [CAMetalLayer layer]; }
- (BOOL)wantsUpdateLayer { return YES; }
@end

@interface McfmApp : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic) Egl egl;
@property(nonatomic) EGLDisplay display;
@property(nonatomic) EGLSurface surface;
@property(nonatomic) EGLContext context;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic) long framesLeft;  // < 0: run forever
@property(nonatomic) long framesDone;
@end

@implementation McfmApp {
  Engine _engine;
  std::string _dataDir;
}
- (instancetype)initWithDataDir:(const std::string &)dir frames:(long)frames {
  if ((self = [super init])) { _dataDir = dir; _framesLeft = frames; }
  return self;
}
- (NSSize)pixelSize {
  NSView *v = self.window.contentView;
  double scale = self.window.backingScaleFactor;
  return NSMakeSize(pixel_size(v.bounds.size.width, scale), pixel_size(v.bounds.size.height, scale));
}
- (void)applicationDidFinishLaunching:(NSNotification *)n {
  self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1280, 720)
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                              backing:NSBackingStoreBuffered defer:NO];
  self.window.title = @"Minecraft PE";
  self.window.delegate = self;
  McfmView *view = [[McfmView alloc] initWithFrame:self.window.contentView.bounds];
  view.wantsLayer = YES;
  self.window.contentView = view;
  [self.window center];
  [self.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  CAMetalLayer *layer = (CAMetalLayer *)view.layer;
  layer.contentsScale = self.window.backingScaleFactor;

  Egl e;
  if (!load_egl(executable_dir(), &e)) { std::fprintf(stderr, "mcfm: ANGLE missing\n"); exit(4); }
  self.egl = e;
  const EGLAttrib display_attribs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE};
  self.display = e.GetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr, display_attribs);
  EGLint major = 0, minor = 0;
  if (!self.display || !e.Initialize(self.display, &major, &minor)) { std::fprintf(stderr, "mcfm: eglInitialize failed 0x%x\n", e.GetError()); exit(4); }
  const EGLint config_attribs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                   EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (!e.ChooseConfig(self.display, config_attribs, &config, 1, &count) || count < 1) { std::fprintf(stderr, "mcfm: no EGL config\n"); exit(4); }
  const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  self.context = e.CreateContext(self.display, config, nullptr, context_attribs);
  self.surface = e.CreateWindowSurface(self.display, config, (__bridge void *)layer, nullptr);
  if (!self.context || !self.surface || !e.MakeCurrent(self.display, self.surface, self.surface, self.context)) {
    std::fprintf(stderr, "mcfm: EGL context/surface failed 0x%x\n", e.GetError());
    exit(4);
  }
  std::printf("mcfm: EGL %d.%d (ANGLE, Metal)\n", major, minor);
  NSSize px = [self pixelSize];
  if (!_engine.start(EngineAddresses::for_slide(g_slide), host_info(_dataDir), (int)px.width, (int)px.height)) exit(5);
  std::printf("mcfm: engine started (%dx%d)\n", (int)px.width, (int)px.height);
  [NSTimer scheduledTimerWithTimeInterval:1.0 / 60 target:self selector:@selector(frame) userInfo:nil repeats:YES];
}
- (void)frame {
  NSSize px = [self pixelSize];
  self.egl.MakeCurrent(self.display, self.surface, self.surface, self.context);
  self.egl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  self.egl.Viewport(0, 0, (int)px.width, (int)px.height);
  _engine.frame();
  self.egl.SwapBuffers(self.display, self.surface);
  self.framesDone++;
  if (self.framesLeft > 0 && --_framesLeft == 0) {
    std::printf("mcfm: %ld frames rendered\n", self.framesDone);
    std::fflush(stdout);
    exit(0);
  }
}
- (void)windowDidResize:(NSNotification *)n {
  NSSize px = [self pixelSize];
  ((CAMetalLayer *)self.window.contentView.layer).drawableSize = CGSizeMake(px.width, px.height);
  _engine.resize((int)px.width, (int)px.height);
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app { return YES; }
@end

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);  // keep progress lines if the engine crashes
  long frames = -1;
  std::string path;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--print-hooks") {
      size_t n = 0;
      const Hook *h = hooks(&n);
      for (size_t k = 0; k < n; k++) std::printf("%s\t0x%lx\n", h[k].name, static_cast<unsigned long>(h[k].address));
      return 0;
    } else if (a == "--frames" && i + 1 < argc) {
      frames = std::atol(argv[++i]);
    } else {
      path = a;
    }
  }
  if (path.empty()) path = executable_dir() + "/libminecraftpe.dylib";
  std::vector<char> header;
  if (!read_header(path, &header)) { std::fprintf(stderr, "mcfm: cannot load %s: unreadable\n", path.c_str()); return 2; }
  if (!mcfm::is_expected_game_image(header.data())) {
    std::fprintf(stderr, "mcfm: %s is not Minecraft PE 0.15.10 (LC_UUID)\n", path.c_str());
    return 3;
  }
  _dyld_register_func_for_add_image(on_add_image);
  if (!dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL)) { std::fprintf(stderr, "mcfm: cannot load %s: %s\n", path.c_str(), dlerror()); return 2; }
  if (!g_found) { std::fprintf(stderr, "mcfm: %s loaded but not found among dyld images\n", path.c_str()); return 2; }
  std::printf("mcfm: game image loaded (slide 0x%lx)\n", static_cast<unsigned long>(g_slide));
  std::fflush(stdout);
  // The game's data/ lives next to the image's source app: make_launcher.sh writes its path.
  std::string data_dir;
  std::ifstream df(executable_dir() + "/data_dir.txt");
  std::getline(df, data_dir);
  @autoreleasepool {
    NSApplication *app = [NSApplication sharedApplication];
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    McfmApp *delegate = [[McfmApp alloc] initWithDataDir:data_dir frames:frames];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}
