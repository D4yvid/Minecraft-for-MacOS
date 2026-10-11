// The iOS app's launcher (docs/LAUNCHER.md, Stage 4): the converted game image, signed into the
// app's Frameworks, is loaded by dyld (the macOS launcher's --loader dyld path: iOS runs no code
// that was not signed at build time); our AppPlatform (shared/launcher) runs the engine. Called
// on the main thread only.
#import <Foundation/Foundation.h>

#include <dlfcn.h>
#include <mach-o/dyld.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <mcfm/keyboard_mouse.h>

#include "addresses_0_15_10.h"
#include "app_platform.h"
#include "engine.h"
#include "hook_table.h"
#include "ios_keys.h"
#include "launcher_platform.h"
#include "layout.h"
#include "lifecycle.h"
#include "macho_uuid.h"
#include "mcfm_ios.h"
#include "seams.h"
#include "text_input.h"
#include "touch_input.h"

using namespace mcfm::launcher;

namespace {

McfmIosCallbacks g_callbacks;
uintptr_t g_slide = 0;
bool g_found = false, g_hooks_ok = false, g_started = false, g_text_mode = false;
TouchSlots g_touches;
IosKeys g_keys;

Engine &engine() {
  static Engine e;
  return e;
}

Lifecycle<Engine> &life() {
  static Lifecycle<Engine> l(engine());
  return l;
}

void fatal(const std::string &message) {
  std::fprintf(stderr, "mcfm: %s\n", message.c_str());
  if (g_callbacks.fatal) g_callbacks.fatal(message.c_str());
}

// dyld calls this after mapping an image and before running its initializers: the hook table
// gets our replacements before any game code runs (as macos/launcher/main.mm).
void on_add_image(const mach_header *header, intptr_t slide) {
  if (g_found || !mcfm::is_expected_game_image(header)) return;
  g_found = true;
  g_slide = static_cast<uintptr_t>(slide);
  size_t n = 0;
  const Hook *h = hooks(&n);
  uintptr_t table = mcfm::hook_table_address(header);
  if (!table || mcfm::hook_table_capacity(header) < n) return;
  for (size_t i = 0; i < n; i++) {
    uintptr_t entry = h[i].address + g_slide;
    if (mcfm::hook_slot(reinterpret_cast<const void *>(entry), entry) != table + g_slide + 8 * i) return;
  }
  void **slots = reinterpret_cast<void **>(table + g_slide);
  for (size_t i = 0; i < n; i++) slots[i] = h[i].replacement;
  g_hooks_ok = true;
}

bool file_is_game(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  std::vector<char> head(64 * 1024);
  f.read(head.data(), head.size());
  head.resize(static_cast<size_t>(f.gcount()));
  return head.size() >= 32 && mcfm::is_expected_game_image(head.data(), head.size());
}

void show_keyboard(const std::string &) {
  g_text_mode = true;
  if (g_callbacks.show_keyboard) g_callbacks.show_keyboard();
}
void hide_keyboard() {
  g_text_mode = false;
  if (g_callbacks.hide_keyboard) g_callbacks.hide_keyboard();
}
void pick_image() {
  if (g_callbacks.pick_image) g_callbacks.pick_image();
  else image_pick_cancelled();
}

uintptr_t text_queue() { return addr::kKeyboardText + g_slide; }

}  // namespace

extern "C" {

int mcfm_ios_start(const char *image_path, const char *data_dir, const char *home_dir, int width_px, int height_px,
                   McfmIosCallbacks callbacks) {
  g_callbacks = callbacks;
  if (g_started) return 1;
  std::string image = image_path, home = home_dir;
  if (!file_is_game(image)) {
    fatal("The bundled game is not Minecraft PE 0.15.10 for iOS (" + image + ").");
    return 0;
  }
  _dyld_register_func_for_add_image(on_add_image);
  if (!dlopen(image.c_str(), RTLD_NOW | RTLD_LOCAL)) {
    fatal(std::string("The game cannot be loaded: ") + dlerror());
    return 0;
  }
  if (!g_found || !g_hooks_ok) {
    fatal("The game image was converted without this launcher's hooks (rebuild the app).");
    return 0;
  }
  std::fprintf(stderr, "mcfm: game image loaded (slide 0x%lx)\n", static_cast<unsigned long>(g_slide));
  // Worlds and options in Documents (the Files app shows them); the game's temp files (and a
  // picked skin) in the app's own tmp, which Files does not show. Builds before 2026-10-11 used
  // Documents/tmp: it goes.
  NSFileManager *fm = NSFileManager.defaultManager;
  [fm removeItemAtPath:@((home + "/tmp").c_str()) error:nil];
  HostInfo info = make_host_info(home, data_dir, NSTemporaryDirectory().stringByStandardizingPath.UTF8String);
  info.input_mode = 2;  // touch; hardware keys still work
  for (const std::string &d : {info.internal_dir, info.userdata_dir, info.temp_dir})
    [fm createDirectoryAtPath:@(d.c_str()) withIntermediateDirectories:YES attributes:nil error:nil];
  set_keyboard_callbacks(KeyboardCallbacks{&show_keyboard, &hide_keyboard});
  set_image_picker(&pick_image);
  if (!engine().start(EngineAddresses::for_slide(g_slide), info, width_px, height_px)) {
    fatal("The game engine did not start.");
    return 0;
  }
  static LauncherPlatform platform(engine().vtable(), InputAddresses::for_slide(g_slide));
  mcfm::keyboard_mouse::install(platform, mcfm::keyboard_mouse::PointerCallbacks{nullptr, nullptr}, false);
  g_started = true;
  std::fprintf(stderr, "mcfm: engine started (%dx%d)\n", width_px, height_px);
  life().started();  // a pause that came before the engine started applies now
  return 1;
}

void mcfm_ios_frame(void) {
  if (life().running()) engine().frame();
}

void mcfm_ios_resize(int width_px, int height_px) { life().resize(width_px, height_px); }

void mcfm_ios_pause(int paused) { life().set_paused(paused != 0); }

void mcfm_ios_touch(int action, int pointer, float x_px, float y_px) {
  typedef void (*MultitouchFeed)(int, int, int, int, int);
  if (!life().running()) return;
  FeedCall f;
  if (touch_feed(&g_touches, static_cast<TouchAction>(action), pointer, x_px, y_px, &f))
    reinterpret_cast<MultitouchFeed>(addr::kMultitouchFeed + g_slide)(f.button, f.state, f.x, f.y, f.slot);
}

int mcfm_ios_key(int hid_usage, int down) {
  if (!g_started) return 0;
  KeyRoute r = g_keys.press(hid_usage, down != 0, g_text_mode);
  if (r.vk && life().running()) mcfm::keyboard_mouse::key(r.vk, r.down);
  return r.to_text ? 1 : 0;
}

void mcfm_ios_text(const char *utf8) {
  if (g_started && utf8) push_text(text_queue(), utf8);
}

void mcfm_ios_backspace(void) {
  if (g_started) push_backspace(text_queue());
}

// As iOS's -[ShowKeyboardView textViewShouldReturn:]: the newline, then Enter pressed and
// released (which ends editing).
void mcfm_ios_return(int press_enter) {
  if (!g_started) return;
  push_return(text_queue());
  if (press_enter) {
    mcfm::keyboard_mouse::key(0x0D, true);
    mcfm::keyboard_mouse::key(0x0D, false);
  }
}

void mcfm_ios_image_picked(const char *png_path) {
  if (png_path) image_picked(png_path);
  else image_pick_cancelled();
}

void mcfm_ios_pixel_size(double w_pt, double h_pt, double scale, int *w_px, int *h_px) {
  PixelSize p = pixel_size(w_pt, h_pt, scale);
  *w_px = p.w;
  *h_px = p.h;
}

}  // extern "C"
