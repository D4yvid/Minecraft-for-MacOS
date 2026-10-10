// mcfm-launch: runs the converted game image (make launcher) in a macOS window.
// usage: mcfm-launch [--print-hooks] [--frames N [--screenshot out.ppm]] [image]
// The LC_UUID is checked on the file before dlopen, the hook table is filled from a dyld
// add-image callback (before the game's initializers), then the engine boots in an ANGLE
// (OpenGL ES 3 on Metal) context. docs/LAUNCHER.md, Stage 1b.
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <dlfcn.h>
#include <unistd.h>
#include <mach-o/dyld.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "egl_min.h"
#include "engine.h"
#include "hook_table.h"
#include "input.h"
#include "launcher_platform.h"
#include "macho_uuid.h"
#include "resize_math.h"
#include "screenshot.h"
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
  out->resize(static_cast<size_t>(f.gcount()));
  return out->size() >= 32;
}

uintptr_t g_slide = 0;
bool g_found = false;
bool g_hooks_ok = false;  // every hooked entry jumps through its own table slot

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
  // The image must have been converted with exactly these hooks (make launcher): otherwise a
  // patched entry would jump through an empty slot, or an unpatched seam would run.
  for (size_t i = 0; i < n; i++) {
    uintptr_t entry = h[i].address + g_slide;
    if (mcfm::hook_slot(reinterpret_cast<const void *>(entry), entry) != table + g_slide + 8 * i) {
      std::fprintf(stderr, "mcfm: hook %s is not installed in the game image\n", h[i].name);
      return;
    }
  }
  void **slots = reinterpret_cast<void **>(table + g_slide);
  for (size_t i = 0; i < n; i++) slots[i] = h[i].replacement;
  g_hooks_ok = true;
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
  SYM(gles, ReadPixels, "glReadPixels");
  SYM(gles, PixelStorei, "glPixelStorei");
  SYM(gles, BindBuffer, "glBindBuffer");
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
- (BOOL)acceptsFirstResponder { return YES; }
- (void)keyDown:(NSEvent *)e { mcfm::launcher::input::key_event(e); }
- (void)keyUp:(NSEvent *)e { mcfm::launcher::input::key_event(e); }
- (void)flagsChanged:(NSEvent *)e { mcfm::launcher::input::flags_changed(e); }
- (void)mouseMoved:(NSEvent *)e { mcfm::launcher::input::mouse_event(e); }
- (void)mouseDragged:(NSEvent *)e { mcfm::launcher::input::mouse_event(e); }
- (void)rightMouseDragged:(NSEvent *)e { mcfm::launcher::input::mouse_event(e); }
- (void)otherMouseDragged:(NSEvent *)e { mcfm::launcher::input::mouse_event(e); }
- (void)mouseDown:(NSEvent *)e { mcfm::launcher::input::mouse_event(e); }
- (void)mouseUp:(NSEvent *)e { mcfm::launcher::input::mouse_event(e); }
- (void)rightMouseDown:(NSEvent *)e { mcfm::launcher::input::mouse_event(e); }
- (void)rightMouseUp:(NSEvent *)e { mcfm::launcher::input::mouse_event(e); }
- (void)otherMouseDown:(NSEvent *)e { mcfm::launcher::input::mouse_event(e); }
- (void)otherMouseUp:(NSEvent *)e { mcfm::launcher::input::mouse_event(e); }
- (void)scrollWheel:(NSEvent *)e { mcfm::launcher::input::scroll_event(e); }
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
  std::string _screenshot;  // written from the last frame of --frames
}
- (instancetype)initWithDataDir:(const std::string &)dir frames:(long)frames screenshot:(const std::string &)shot {
  if ((self = [super init])) { _dataDir = dir; _framesLeft = frames; _screenshot = shot; }
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
  mcfm::launcher::input::install(self.window.contentView, _engine.vtable(), mcfm::launcher::InputAddresses::for_slide(g_slide));
  self.window.acceptsMouseMovedEvents = YES;
  [self.window makeFirstResponder:self.window.contentView];
  // Common modes: keep rendering during live resize and while a menu is open.
  NSTimer *timer = [NSTimer timerWithTimeInterval:1.0 / 60 target:self selector:@selector(frame) userInfo:nil repeats:YES];
  [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
}
- (void)frame {
  NSSize px = [self pixelSize];
  self.egl.MakeCurrent(self.display, self.surface, self.surface, self.context);
  self.egl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  self.egl.Viewport(0, 0, (int)px.width, (int)px.height);
  _engine.frame();
  if (self.framesLeft == 1 && !_screenshot.empty()) {
    // Read the default framebuffer with tightly packed rows into client memory, whatever
    // state the engine left behind.
    self.egl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    self.egl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    self.egl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    self.egl.PixelStorei(GL_PACK_ROW_LENGTH, 0);
    std::vector<unsigned char> pixels(static_cast<size_t>(px.width) * static_cast<size_t>(px.height) * 4);
    self.egl.ReadPixels(0, 0, (int)px.width, (int)px.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (write_ppm(_screenshot.c_str(), (int)px.width, (int)px.height, pixels.data()))
      std::printf("mcfm: screenshot %s\n", _screenshot.c_str());
  }
  self.egl.SwapBuffers(self.display, self.surface);
  self.framesDone++;
  if (self.framesLeft > 0 && --_framesLeft == 0) {
    std::printf("mcfm: %ld frames rendered\n", self.framesDone);
    std::fflush(stdout);
    std::fflush(stderr);
    // _exit: engine threads (REST, audio, ...) are still running; static destructors must not.
    _exit(0);
  }
}
- (void)updateSurfaceSize {
  NSSize px = [self pixelSize];
  CAMetalLayer *layer = (CAMetalLayer *)self.window.contentView.layer;
  layer.contentsScale = self.window.backingScaleFactor;
  layer.drawableSize = CGSizeMake(px.width, px.height);
  _engine.resize((int)px.width, (int)px.height);
}
- (void)windowDidResize:(NSNotification *)n {
  [self updateSurfaceSize];
}
// Moved between a Retina and a non-Retina display: new pixel size for the same window.
- (void)windowDidChangeBackingProperties:(NSNotification *)n {
  [self updateSurfaceSize];
}
- (void)windowDidResignKey:(NSNotification *)n {
  mcfm::launcher::input::focus_lost();
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app { return YES; }
@end

namespace {

int usage(const char *why) {
  std::fprintf(stderr, "mcfm: %s\nusage: mcfm-launch [--print-hooks] [--frames N [--screenshot out.ppm]] [image]\n", why);
  return 2;
}

// A positive whole number of frames, or -1.
long parse_frames(const char *text) {
  char *end = nullptr;
  long n = std::strtol(text, &end, 10);
  return (end && *end == '\0' && n > 0) ? n : -1;
}

}  // namespace

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);  // keep progress lines if the engine crashes
  long frames = -1;
  std::string path, screenshot;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--print-hooks") {
      size_t n = 0;
      const Hook *h = hooks(&n);
      for (size_t k = 0; k < n; k++) std::printf("%s\t0x%lx\n", h[k].name, static_cast<unsigned long>(h[k].address));
      return 0;
    } else if (a == "--frames") {
      if (i + 1 >= argc || (frames = parse_frames(argv[++i])) < 0) return usage("--frames needs a positive number");
    } else if (a == "--screenshot") {
      if (i + 1 >= argc) return usage("--screenshot needs a file");
      screenshot = argv[++i];
    } else if (a.compare(0, 2, "--") == 0 || !path.empty()) {
      return usage(("unexpected argument " + a).c_str());
    } else {
      path = a;
    }
  }
  if (!screenshot.empty() && frames < 0) return usage("--screenshot needs --frames (it captures the last frame)");
  if (path.empty()) path = executable_dir() + "/libminecraftpe.dylib";
  std::vector<char> header;
  if (!read_header(path, &header)) { std::fprintf(stderr, "mcfm: cannot load %s: unreadable\n", path.c_str()); return 2; }
  if (!mcfm::is_expected_game_image(header.data(), header.size())) {
    std::fprintf(stderr, "mcfm: %s is not Minecraft PE 0.15.10 (LC_UUID)\n", path.c_str());
    return 3;
  }
  // The game's data/ lives in the app the image came from: make_launcher.sh writes its path.
  std::string data_dir;
  std::ifstream df(executable_dir() + "/data_dir.txt");
  std::getline(df, data_dir);
  BOOL is_dir = NO;
  if (data_dir.empty() || ![[NSFileManager defaultManager] fileExistsAtPath:@(data_dir.c_str()) isDirectory:&is_dir] || !is_dir) {
    std::fprintf(stderr, "mcfm: game data not found (%s); rebuild with make launcher\n",
                 data_dir.empty() ? "no data_dir.txt next to mcfm-launch" : data_dir.c_str());
    return 2;
  }
  _dyld_register_func_for_add_image(on_add_image);
  if (!dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL)) { std::fprintf(stderr, "mcfm: cannot load %s: %s\n", path.c_str(), dlerror()); return 2; }
  if (!g_found) { std::fprintf(stderr, "mcfm: %s loaded but not found among dyld images\n", path.c_str()); return 2; }
  if (!g_hooks_ok) {
    std::fprintf(stderr, "mcfm: %s was built without the launcher's hooks (rebuild with make launcher)\n", path.c_str());
    return 2;
  }
  std::printf("mcfm: game image loaded (slide 0x%lx)\n", static_cast<unsigned long>(g_slide));
  @autoreleasepool {
    NSApplication *app = [NSApplication sharedApplication];
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    McfmApp *delegate = [[McfmApp alloc] initWithDataDir:data_dir frames:frames screenshot:screenshot];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}
