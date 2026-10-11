#include "game_thread.h"

#include <EGL/egl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#include <mcfm/keyboard_mouse.h>

#include "addresses_0_15_10.h"
#include "darwin.h"
#include "engine.h"
#include "game_import.h"
#include "launcher_platform.h"
#include "loader.h"
#include "loader_android.h"
#include "macho_uuid.h"
#include "render_requests.h"
#include "seams.h"
#include "text_input.h"

namespace mcfm {
namespace android {
namespace {

using namespace mcfm::launcher;

void acquire_window(void *w) { ANativeWindow_acquire(static_cast<ANativeWindow *>(w)); }
void release_window(void *w) { ANativeWindow_release(static_cast<ANativeWindow *>(w)); }

RenderRequests &requests() {
  static RenderRequests r(WindowRefs{&acquire_window, &release_window});
  return r;
}
EventQueue g_events;
GamePaths g_paths;
AppCallbacks g_callbacks;

void make_dirs(const std::string &path) {
  for (size_t i = 1; i <= path.size(); i++)
    if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0700);
}

std::vector<loader::ConvertHook> convert_hooks() {
  size_t n = 0;
  const Hook *h = hooks(&n);
  std::vector<loader::ConvertHook> out;
  for (size_t i = 0; i < n; i++) out.push_back({h[i].name, h[i].address});
  return out;
}

// An app update may change the launcher's hooks: the image is converted again from the binary
// kept next to it (no new import needed). False (message given) when that is not possible.
bool image_up_to_date() {
  std::string dir = g_paths.image.substr(0, g_paths.image.rfind('/'));
  std::vector<loader::ConvertHook> current = convert_hooks();
  if (image_is_current(dir, current)) return true;
  std::string binary = dir + "/minecraftpe2";
  if (access(binary.c_str(), R_OK) != 0) {
    g_callbacks.fatal(access(g_paths.image.c_str(), R_OK) == 0
                          ? "This version of the app needs the game imported again."
                          : "The game is not imported (" + g_paths.image + ").");
    return false;
  }
  std::fprintf(stderr, "mcfm: the launcher's hooks changed: converting the game again\n");
  ImportOptions options;
  options.hooks = current;
  std::string error = import_game(binary, dir, options);
  if (!error.empty()) {
    g_callbacks.fatal(error);
    return false;
  }
  return true;
}

// The game image with the launcher's hooks; false (message given) when it cannot load.
bool load_game(uintptr_t *slide) {
  if (!image_up_to_date()) return false;
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
  EGLSurface pbuffer = EGL_NO_SURFACE;  // current while there is no window
} egl;

bool init_egl() {
  egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(egl.display, nullptr, nullptr)) return false;
  const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                          EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
  EGLint count = 0;
  if (!eglChooseConfig(egl.display, attrs, &egl.config, 1, &count) || count < 1) return false;
  const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  egl.context = eglCreateContext(egl.display, egl.config, EGL_NO_CONTEXT, ctx);
  const EGLint size[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  egl.pbuffer = eglCreatePbufferSurface(egl.display, egl.config, size);
  return egl.context != EGL_NO_CONTEXT && egl.pbuffer != EGL_NO_SURFACE &&
         eglMakeCurrent(egl.display, egl.pbuffer, egl.pbuffer, egl.context);
}

// Drops the window surface. The context stays current on a 1x1 pbuffer (with it every GL object
// the game made): the game reloads its shaders on resume, before the new window exists, and
// without a current context its uniform lookups crashed. (Surfaceless needs an extension.)
void release_surface() {
  if (egl.surface == EGL_NO_SURFACE) return;
  eglMakeCurrent(egl.display, egl.pbuffer, egl.pbuffer, egl.context);
  eglDestroySurface(egl.display, egl.surface);
  egl.surface = EGL_NO_SURFACE;
}

bool attach_surface(ANativeWindow *window) {
  egl.surface = eglCreateWindowSurface(egl.display, egl.config, window, nullptr);
  if (egl.surface != EGL_NO_SURFACE && eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context)) return true;
  std::fprintf(stderr, "mcfm: cannot draw into the window (EGL error 0x%x)\n", eglGetError());
  if (egl.surface != EGL_NO_SURFACE) eglDestroySurface(egl.display, egl.surface);
  egl.surface = EGL_NO_SURFACE;
  eglMakeCurrent(egl.display, egl.pbuffer, egl.pbuffer, egl.context);
  return false;
}

typedef void (*MultitouchFeed)(int, int, int, int, int);

void show_keyboard(const std::string &text) { g_callbacks.show_keyboard(text); }
void pick_image() { g_callbacks.pick_image(); }
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
    case EventType::ImagePicked: e.text.empty() ? image_pick_cancelled() : image_picked(e.text); break;
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
  bool started = false, paused = false, suspended = false, drawable = false;
  int width = 0, height = 0;
  ANativeWindow *window = nullptr;
  TouchSlots touches;
  std::vector<Event> events;
  for (;;) {
    // What the UI thread asked for (blocks while there is nothing, and nothing to draw).
    Requests q = requests().wait(started && drawable && !paused);
    // Requests often come in one batch (a resume with its new window and the window focus). The
    // engine is resumed before it hears of a window size or focus, and suspended while it still
    // has its surface, as when each came alone (resize or focus_gained while suspended crashed).
    if (q.pause_changed) paused = q.paused;
    bool resumed = false;
    auto sync_pause = [&] {
      if (started && paused && !suspended) {
        engine.focus_lost();
        engine.suspend();  // the game saves
        mcfm_darwin_drain_main_queue();
        suspended = true;
        std::fprintf(stderr, "mcfm: paused (saved)\n");
      } else if (started && !paused && suspended) {
        engine.resume();
        engine.focus_gained();
        suspended = false;
        resumed = true;
      }
    };
    sync_pause();
    if (q.window_changed) {
      ANativeWindow *wanted = static_cast<ANativeWindow *>(q.window);  // a reference of our own
      if (wanted != window) {
        release_surface();
        if (window) ANativeWindow_release(window);
        window = wanted;
        drawable = window && attach_surface(window);
      } else if (wanted) {
        ANativeWindow_release(wanted);  // the same window resized: one reference is enough
      }
      if (window) {
        width = q.width;
        height = q.height;
      }
      if (drawable && !started) {
        HostInfo info = make_host_info(g_paths.home, g_paths.data, g_paths.home + "/tmp");
        info.input_mode = 2;  // touch; keys and a mouse still work
        for (const std::string &d : {info.internal_dir, info.userdata_dir, info.temp_dir}) make_dirs(d);
        set_keyboard_callbacks(KeyboardCallbacks{&show_keyboard, &hide_keyboard});
        set_image_picker(&pick_image);
        if (!engine.start(EngineAddresses::for_slide(slide), info, width, height)) {
          release_surface();
          ANativeWindow_release(window);
          requests().ack(q);
          g_callbacks.fatal("The game engine did not start.");
          return;
        }
        static LauncherPlatform platform(engine.vtable(), InputAddresses::for_slide(slide));
        keyboard_mouse::install(platform, keyboard_mouse::PointerCallbacks{nullptr, nullptr}, false);
        std::fprintf(stderr, "mcfm: engine started (%dx%d)\n", width, height);
        started = true;
        requests().set_engine_started();
      } else if (drawable && started) {
        engine.resize(width, height);
      }
    }
    sync_pause();  // a pause that came before the engine started
    if (started && !paused && q.focus_changed && !(resumed && q.focused))
      q.focused ? engine.focus_gained() : engine.focus_lost();
    requests().ack(q);  // only what was taken above: a request that came in since waits its turn
    if (!started || !drawable || paused) continue;
    g_events.drain(&events);
    for (const Event &e : events) apply(e, slide, &touches);
    engine.frame();
    mcfm_darwin_drain_main_queue();
    if (eglSwapBuffers(egl.display, egl.surface)) {
      static bool shown = false;
      if (!shown && g_callbacks.first_frame) g_callbacks.first_frame();  // the splash goes
      shown = true;
    } else {
      std::fprintf(stderr, "mcfm: the window cannot be drawn into any more (EGL error 0x%x)\n", eglGetError());
      drawable = false;  // until the next window
    }
  }
}

void thread_main() {
  run();
  requests().finish();  // the game cannot run: nobody waits for this thread any more
  if (g_callbacks.thread_exit) g_callbacks.thread_exit();
}

}  // namespace

void start_game(const GamePaths &paths, const AppCallbacks &callbacks) {
  static bool started = false;  // the UI thread only
  if (started) return;
  started = true;
  g_paths = paths;
  g_callbacks = callbacks;
  std::thread(thread_main).detach();
}

void set_window(ANativeWindow *window, int width, int height) { requests().set_window(window, width, height); }
void set_paused(bool paused) { requests().set_paused(paused); }
void set_focus(bool focused) { requests().set_focus(focused); }
void push_event(const Event &event) { g_events.push(event); }

}  // namespace android
}  // namespace mcfm
