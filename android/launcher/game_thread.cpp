#include "game_thread.h"

#include <EGL/egl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

#include <mcfm/keyboard_mouse.h>

#include "addresses_0_15_10.h"
#include "darwin.h"
#include "engine.h"
#include "launcher_platform.h"
#include "loader.h"
#include "loader_android.h"
#include "macho_uuid.h"
#include "seams.h"
#include "text_input.h"

namespace mcfm {
namespace android {
namespace {

using namespace mcfm::launcher;

// What the UI thread asks for; guarded by g_lock, signalled with g_changed.
struct Shared {
  ANativeWindow *window = nullptr;  // wanted window (owned reference)
  int width = 0, height = 0;
  bool window_changed = false;
  bool paused = false, pause_changed = false;
  bool focused = true, focus_changed = false;
  long acked = 0, requested = 0;  // set_window / set_paused wait until acked catches up
  bool finished = false;          // the render thread ended (the game could not run)
} g;
std::mutex g_lock;
std::condition_variable g_changed, g_acked;
EventQueue g_events;
GamePaths g_paths;
AppCallbacks g_callbacks;

void make_dirs(const std::string &path) {
  for (size_t i = 1; i <= path.size(); i++)
    if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0700);
}

// The game image with the launcher's hooks; false (message given) when it cannot load.
bool load_game(uintptr_t *slide) {
  std::ifstream f(g_paths.image, std::ios::binary);
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  int fd = open(g_paths.image.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0 || file.empty()) {
    if (fd >= 0) close(fd);
    g_callbacks.fatal("The game is not imported (" + g_paths.image + ").");
    return false;
  }
  size_t n = 0;
  const Hook *h = hooks(&n);
  std::vector<uintptr_t> addresses;
  std::vector<const void *> replacements;
  std::vector<const char *> names;
  for (size_t i = 0; i < n; i++) {
    addresses.push_back(h[i].address);
    replacements.push_back(h[i].replacement);
    names.push_back(h[i].name);
  }
  static loader::AndroidLoaderOS os("");  // stubs: the APK's libraries, by name
  loader::LoadOptions opts;
  opts.hook_addresses = addresses.data();
  opts.hook_replacements = replacements.data();
  opts.hook_names = names.data();
  opts.hook_count = n;
  opts.accept_header = [](const uint8_t *header) { return is_expected_game_image(header); };
  static loader::Image image;
  std::string error;
  bool ok = loader::load_image(os, fd, file.data(), file.size(), opts, &image, &error);
  close(fd);
  if (!ok) {
    g_callbacks.fatal("The game cannot be loaded: " + error);
    return false;
  }
  std::fprintf(stderr, "mcfm: game image loaded (slide 0x%lx)\n", static_cast<unsigned long>(image.slide));
  *slide = static_cast<uintptr_t>(image.slide);
  return true;
}

struct Egl {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLConfig config = nullptr;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface surface = EGL_NO_SURFACE;
} egl;

bool init_egl() {
  egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(egl.display, nullptr, nullptr)) return false;
  const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                          EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
  EGLint count = 0;
  if (!eglChooseConfig(egl.display, attrs, &egl.config, 1, &count) || count < 1) return false;
  const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  egl.context = eglCreateContext(egl.display, egl.config, EGL_NO_CONTEXT, ctx);
  return egl.context != EGL_NO_CONTEXT;
}

// Drops the window surface, keeping the context (and with it every GL object the game made).
void release_surface() {
  if (egl.surface == EGL_NO_SURFACE) return;
  eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl.context);
  eglDestroySurface(egl.display, egl.surface);
  egl.surface = EGL_NO_SURFACE;
}

bool attach_surface(ANativeWindow *window) {
  egl.surface = eglCreateWindowSurface(egl.display, egl.config, window, nullptr);
  return egl.surface != EGL_NO_SURFACE && eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context);
}

typedef void (*MultitouchFeed)(int, int, int, int, int);

void show_keyboard(const std::string &text) { g_callbacks.show_keyboard(text); }
void hide_keyboard() { g_callbacks.hide_keyboard(); }

void apply(const Event &e, uintptr_t slide, TouchSlots *touches) {
  static MultitouchFeed feed = reinterpret_cast<MultitouchFeed>(addr::kMultitouchFeed + slide);
  switch (e.type) {
    case EventType::Touch: {
      FeedCall f;
      if (touch_feed(touches, static_cast<TouchAction>(e.a), e.b, e.x, e.y, &f)) feed(f.button, f.state, f.x, f.y, f.slot);
      break;
    }
    case EventType::Key: keyboard_mouse::key(e.a, e.b != 0); break;
    case EventType::MouseButton: keyboard_mouse::mouse_button(e.a, e.b != 0, int(e.x), int(e.y)); break;
    case EventType::MouseMove: keyboard_mouse::mouse_move_abs(int(e.x), int(e.y)); break;
    case EventType::MouseWheel: keyboard_mouse::mouse_wheel(e.a, int(e.x), int(e.y)); break;
    case EventType::Text: push_text(addr::kKeyboardText + slide, e.text); break;
    case EventType::Backspace: push_backspace(addr::kKeyboardText + slide); break;
    case EventType::Return: push_return(addr::kKeyboardText + slide); break;
  }
}

void run() {
  uintptr_t slide = 0;
  if (!load_game(&slide)) return;
  if (!init_egl()) {
    g_callbacks.fatal("OpenGL ES 3 is not available on this device.");
    return;
  }
  static Engine engine;
  bool started = false, paused = false;
  int width = 0, height = 0;
  ANativeWindow *window = nullptr;
  TouchSlots touches;
  std::vector<Event> events;
  for (;;) {
    // Take what the UI thread asked for.
    bool new_window = false, pause_changed = false, focus_changed = false, focused = true;
    ANativeWindow *wanted = nullptr;
    {
      std::unique_lock<std::mutex> hold(g_lock);
      g_changed.wait(hold, [&] { return g.window_changed || g.pause_changed || g.focus_changed || (window && !paused); });
      new_window = g.window_changed;
      wanted = g.window;
      if (new_window) { width = g.width; height = g.height; }
      pause_changed = g.pause_changed;
      paused = g.paused;
      focus_changed = g.focus_changed;
      focused = g.focused;
      g.window_changed = g.pause_changed = g.focus_changed = false;
    }
    if (new_window) {
      if (wanted != window) {
        release_surface();
        if (window) ANativeWindow_release(window);
        window = wanted;
        if (window) ANativeWindow_acquire(window);
        if (window && !attach_surface(window)) std::fprintf(stderr, "mcfm: cannot draw into the window\n");
      }
      if (window && !started) {
        GamePaths &p = g_paths;
        HostInfo info = make_host_info(p.home, p.data, p.home + "/tmp");
        info.input_mode = 2;  // touch; keys and a mouse still work
        for (const std::string &d : {info.internal_dir, info.userdata_dir, info.temp_dir}) make_dirs(d);
        set_keyboard_callbacks(KeyboardCallbacks{&show_keyboard, &hide_keyboard});
        if (!engine.start(EngineAddresses::for_slide(slide), info, width, height)) {
          g_callbacks.fatal("The game engine did not start.");
          return;
        }
        static LauncherPlatform platform(engine.vtable(), InputAddresses::for_slide(slide));
        keyboard_mouse::install(platform, keyboard_mouse::PointerCallbacks{nullptr, nullptr}, false);
        std::fprintf(stderr, "mcfm: engine started (%dx%d)\n", width, height);
        started = true;
      } else if (window && started) {
        engine.resize(width, height);
      }
    }
    if (started && focus_changed) focused ? engine.focus_gained() : engine.focus_lost();
    if (started && pause_changed) {
      if (paused) {
        engine.focus_lost();
        engine.suspend();  // the game saves
        mcfm_darwin_drain_main_queue();
        std::fprintf(stderr, "mcfm: paused (saved)\n");
      } else {
        engine.resume();
        engine.focus_gained();
      }
    }
    {
      std::lock_guard<std::mutex> hold(g_lock);
      g.acked = g.requested;  // the window / pause requests so far are done
    }
    g_acked.notify_all();
    if (!started || !window || paused) continue;
    g_events.drain(&events);
    for (const Event &e : events) apply(e, slide, &touches);
    engine.frame();
    mcfm_darwin_drain_main_queue();
    eglSwapBuffers(egl.display, egl.surface);
  }
}

// Wakes the render thread and waits until it has handled this request (or has ended).
void request_and_wait(std::unique_lock<std::mutex> &hold) {
  long ticket = ++g.requested;
  g_changed.notify_all();
  g_acked.wait(hold, [&] { return g.acked >= ticket || g.finished; });
}

void thread_main() {
  run();
  std::lock_guard<std::mutex> hold(g_lock);
  g.finished = true;
  g_acked.notify_all();
}

bool g_started = false;

}  // namespace

void start_game(const GamePaths &paths, const AppCallbacks &callbacks) {
  std::lock_guard<std::mutex> hold(g_lock);
  if (g_started) return;
  g_started = true;
  g_paths = paths;
  g_callbacks = callbacks;
  std::thread(thread_main).detach();
}

void set_window(ANativeWindow *window, int width, int height) {
  std::unique_lock<std::mutex> hold(g_lock);
  if (g.window) ANativeWindow_release(g.window);
  g.window = window;
  if (window) ANativeWindow_acquire(window);
  g.width = width;
  g.height = height;
  g.window_changed = true;
  if (g_started) request_and_wait(hold);
}

void set_paused(bool paused) {
  std::unique_lock<std::mutex> hold(g_lock);
  g.paused = paused;
  g.pause_changed = true;
  if (g_started) request_and_wait(hold);
}

void set_focus(bool focused) {
  std::lock_guard<std::mutex> hold(g_lock);
  g.focused = focused;
  g.focus_changed = true;
  g_changed.notify_all();
}

void push_event(const Event &event) {
  g_events.push(event);
}

}  // namespace android
}  // namespace mcfm
