# Launcher Stage 1c — Make It Playable Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The launcher becomes a playable desktop game: keyboard, mouse, pointer capture and text entry drive the engine, FMOD plays sound through CoreAudio, the Xbox Live first-launch prompt no longer appears, and — once the owner has played a session — `make app`/`run`/`check` switch from the deprecated Catalyst build to the launcher.

**Architecture:** A `LauncherPlatform` (shared, C++11) implements the existing `mcfm::Platform` interface over our AppPlatform vtable and the engine's input globals, so the existing `keyboard_mouse` module installs unchanged. Text goes into the engine's `Keyboard` text queue exactly as iOS's `ShowKeyboardView` does (`{"c", false}` per character, `{"\b", false}` backspace, `{"\n", true}` return), switched on by our wrappers of AppPlatform slots 9/10 (`showKeyboard`/`hideKeyboard`). The macOS host turns `NSEvent`s into those calls and captures the pointer with CoreGraphics. Sound: `libmcfm_audiotoolbox.dylib` exports the AudioToolbox functions FMOD imports, maps the iOS RemoteIO unit to the macOS default output, answers the iOS-only `AudioSessionGetProperty`, and forwards the rest to the real AudioToolbox; it is the `AudioToolbox` stub's provider (like ANGLE for OpenGLES).

**Tech Stack:** C++11 (shared), Objective-C++17 (AppKit host), C++17 (audio provider, no Apple headers), Python/bash/make; IDA via `tools/ida/q.py` for Task 5.

**Spec:** `docs/LAUNCHER.md` (Stage 1 items 6–9, acceptance, Patch policy) and `docs/research/macho-launcher.md` (incl. "Stage 1b findings"). Survey for this plan: below, under "Facts this plan relies on".

## Facts this plan relies on (✅ verified 2026-10-10, IDA/strings/headers)

- AppPlatform slots: 9 `showKeyboard(const std::string &, int, bool, bool, const Vec2 &)`, 10 `hideKeyboard()`, 12 `getKeyboardHeight() const`, 13/14 hide/showMousePointer, 49 `updateTextBoxText(const std::string &)`, 50 `isKeyboardVisible()` (reads `+9`); base implementations exist for 9, 10, 12, 49, 50.
- `Keyboard` text queue `0x100F5A010`: `std::vector<{std::string text; bool newline;}>` (32-byte elements). `-[ShowKeyboardView textDidChange:]` pushes one element per character, `"\b"` per deleted character, and `{"\n", true}` for return; the engine drains it every tick.
- Sounds are FSB5 banks: 478 FADPCM + 33 PCM16 — FMOD decodes both itself (AudioQueue/AudioFile imports are not on the playback path).
- FMOD's output init (`0x100AD34EC`): `AudioSessionGetProperty('choc')` (output channels), `AudioComponentFindNext({'auou','rioc','appl'})`, `AudioComponentInstanceNew`, `AudioUnitSetProperty(StreamFormat=8, input scope, 0)`, `AudioUnitSetProperty(SetRenderCallback=23, global, 0)`, `AudioUnitInitialize`, then `AudioOutputUnitStart`. The record path (`0x100AD3B44`) runs only if `AudioSessionGetProperty('acat') == 'prec'`.
- Xbox Live first-launch prompt: loc keys `xbox.firstsignin.line1..3`, used by `0x100224594`, a method of a screen class in the Xbox UI unit `0x100222830–0x100224700` (vtables `0x100E57B48…0x100E57D90`; constructor candidates `sub_100223FB8`, `sub_100223DE0`, `sub_100223D28`).

## Global Constraints

- All Stage 1a/1b constraints hold (no Mojang files committed; `make test` passes without game files or ANGLE; host libraries `libSystem`, `libc++`, `libz`; engine text patched only at conversion via `--hooks`; logs `mcfm:`; quoted paths; don't touch the Catalyst build or a running game until Task 7).
- Shared code stays C++11 and platform-free; AppKit/CoreGraphics only in `macos/launcher/`.
- The audio provider includes no Apple headers (the iOS-only AudioSession API is unavailable in the macOS SDK); it declares the few types it needs and reaches the real AudioToolbox only through `dlopen`/`dlsym`.
- Input events are handled on the main thread (the engine drains its queues there).
- Every hook still needs ≥ 12 bytes and passes the converter's checks.

## Review Focus

1. Typing in a text box must not also move the player or trigger hotkeys → while the keyboard is shown, key presses go to text only, except Esc (Task 3 policy test).
2. Focus loss (Cmd-Tab) while keys/buttons are held or the pointer is captured → everything released exactly once and the cursor freed (Task 3 test for the held-set release; pointer release on resign-active).
3. Multi-byte text (é, ü, emoji) and backspace → one queue element per Unicode character, never split UTF-8 (Task 2 test).
4. No default audio device / the real AudioToolbox missing → audio calls fail with an error code, the game keeps running silently (Task 4 test with the real framework absent via an override).
5. A text box opened by the engine before the window has focus, or `hideKeyboard` without `showKeyboard` → no stale text mode (Task 2/3 tests: callbacks idempotent).

---

## File Structure

| File | Responsibility |
|---|---|
| `shared/apple/engine_mouse.h` (create) | `MouseAction`, `feed_mouse(...)` — shared by AddressPlatform (Catalyst) and LauncherPlatform |
| `shared/apple/address_platform.mm` (modify) | uses `engine_mouse.h` (no behaviour change) |
| `shared/apple/addresses_0_15_10.h` (modify) | `kKeyboardText` |
| `shared/launcher/launcher_platform.h/.cpp` (create) | `InputAddresses`, `LauncherPlatform : mcfm::Platform` |
| `shared/launcher/text_input.h/.cpp` (create) | `TextEvent`, `push_text`, `push_backspace`, `push_return`, UTF-8 splitting |
| `shared/launcher/app_platform.h/.cpp` (modify) | `KeyboardCallbacks`, slot 9/10 wrappers calling the base |
| `shared/launcher/engine.h` (modify) | `void **vtable()` accessor |
| `macos/launcher/mac_keymap.h/.cpp` (create) | macOS virtual key code → Windows VK |
| `macos/launcher/input.h/.mm` (create) | NSEvent routing, pointer capture, text mode |
| `macos/launcher/resize_math.h` (modify) | `view_to_pixels` |
| `macos/launcher/audio_toolbox.cpp` (create) | AudioToolbox provider |
| `macos/launcher/main.mm` (modify) | wire input + platform + keyboard callbacks |
| `macos/tools/make_launcher.sh`, `Makefile` (modify) | build/copy the audio provider; tests; Task 7 aliases |
| tests: `shared/tests/launcher_platform_test.cpp`, `launcher_text_test.cpp`, `macos/tests/mac_keymap_test.cpp`, `macos/tests/audio_toolbox_test.cpp`, `tools/tests/catalyst_deprecated_test.sh` (modify in Task 7) | |

Branch: `claude/launcher-1c` (created from `main`).

---

### Task 1: LauncherPlatform (keyboard_mouse through our vtable)

**Files:** create `shared/apple/engine_mouse.h`, `shared/launcher/launcher_platform.h/.cpp`, `shared/tests/launcher_platform_test.cpp`; modify `shared/apple/address_platform.mm`, `shared/launcher/engine.h`, `Makefile`.

**Interfaces:**
- Produces: `namespace mcfm::apple { struct MouseAction { int16_t x, y, dx, dy; int8_t button, data; uint8_t pad[6]; }; void feed_mouse(uintptr_t inputs, uintptr_t grow, uintptr_t device, uintptr_t device_feed, int btn, int state, int x, int y, int dx, int dy); }` — relative motion (`dx||dy`) is pushed into the `Mouse::_instance` queue at `inputs` (grown with the engine's `grow(vector*, MouseAction*)`), everything else calls `device_feed(device, btn, state, x, y)`; coordinates clamped to int16.
- Produces: `namespace mcfm::launcher { struct InputAddresses { uintptr_t keyboard_inputs, keyboard_states, keyboard_text, mouse_device, mouse_inputs, mouse_inputs_grow, mouse_device_feed; static InputAddresses for_slide(uintptr_t); }; class LauncherPlatform : public mcfm::Platform { public: LauncherPlatform(void **vtable, const InputAddresses &); /* log, patch_slot, global, mouse_feed */ }; }`. `patch_slot` maps `engine::Slot` → vtable index: GetEdition 93, DefaultInputMode 96, UIScalingRules 99, UseCenteredGUI 68, PlatformType 69, UseMetadataDrivenScreens 66, HideMousePointer 13, ShowMousePointer 14. `log` prints `mcfm: <msg>` to stderr.
- Produces: `void **Engine::vtable()`.

- [ ] **Step 1: Write the failing test**

`shared/tests/launcher_platform_test.cpp`:
```cpp
// LauncherPlatform: keyboard_mouse installs through our vtable and feeds fake engine globals.
#include "launcher_platform.h"

#include <mcfm/keyboard_mouse.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "engine_mouse.h"

using namespace mcfm::launcher;
namespace kbm = mcfm::keyboard_mouse;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct FeedCall { void *dev; int btn, state, x, y; };
static std::vector<FeedCall> feeds;
static void fake_feed(void *dev, int btn, int state, int x, int y) { feeds.push_back({dev, btn, state, x, y}); }
static int grows = 0;
template <class T> struct RawVector { T *begin, *end, *cap; };
static void fake_grow(void *vec, mcfm::apple::MouseAction *a) {
  grows++;
  auto *v = static_cast<RawVector<mcfm::apple::MouseAction> *>(vec);
  static mcfm::apple::MouseAction big[64];
  size_t n = v->end - v->begin;
  std::memcpy(big, v->begin, n * sizeof *a);
  big[n] = *a;
  v->begin = big; v->end = big + n + 1; v->cap = big + 64;
}
static int hidden = 0, shown = 0;

int main() {
  void *vt[101] = {};
  RawVector<mcfm::engine::KeyEvent> keys = {nullptr, nullptr, nullptr};
  int32_t states[256] = {};
  mcfm::apple::MouseAction one[1];
  RawVector<mcfm::apple::MouseAction> mouse = {one, one, one + 1};
  char device;
  InputAddresses a;
  a.keyboard_inputs = reinterpret_cast<uintptr_t>(&keys);
  a.keyboard_states = reinterpret_cast<uintptr_t>(states);
  a.keyboard_text = 0;
  a.mouse_device = reinterpret_cast<uintptr_t>(&device);
  a.mouse_inputs = reinterpret_cast<uintptr_t>(&mouse);
  a.mouse_inputs_grow = reinterpret_cast<uintptr_t>(&fake_grow);
  a.mouse_device_feed = reinterpret_cast<uintptr_t>(&fake_feed);
  LauncherPlatform p(vt, a);
  kbm::PointerCallbacks cb = {[] { hidden++; }, [] { shown++; }};
  EXPECT(kbm::install(p, cb));
  EXPECT(vt[96] != nullptr && reinterpret_cast<int (*)(void *)>(vt[96])(nullptr) == 1);  // mouse input mode
  reinterpret_cast<void (*)(void *)>(vt[13])(nullptr);
  reinterpret_cast<void (*)(void *)>(vt[14])(nullptr);
  EXPECT(hidden == 1 && shown == 1);
  kbm::key(87, true);  // W
  EXPECT(keys.end - keys.begin == 1 && keys.begin[0].key == 87 && keys.begin[0].state == 1 && states[87] == 1);
  kbm::mouse_button(1, true, 10, 20);
  EXPECT(feeds.size() == 1 && feeds[0].dev == &device && feeds[0].btn == 1 && feeds[0].state == 1 && feeds[0].x == 10 && feeds[0].y == 20);
  kbm::mouse_move_rel(5, -3);
  kbm::mouse_move_rel(70000, 0);  // clamped, and the queue grows through the engine's function
  EXPECT(mouse.end - mouse.begin == 2 && mouse.begin[0].dx == 5 && mouse.begin[0].dy == -3 && mouse.begin[1].dx == 32767);
  EXPECT(grows == 1);
  void *orig = nullptr;
  EXPECT(p.patch_slot(mcfm::engine::Slot::GetEdition, reinterpret_cast<void *>(&fake_feed), &orig) && vt[93] == reinterpret_cast<void *>(&fake_feed));
  EXPECT(p.global(mcfm::engine::Global::KeyboardStates) == states);
  ::operator delete(keys.begin);  // grown by keyboard_mouse with ::operator new
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("launcher_platform_test: all passed\n");
  return 0;
}
```
`Makefile` (host tests):
```make
$(BUILD)/test/launcher_platform_test: shared/tests/launcher_platform_test.cpp shared/launcher/launcher_platform.cpp shared/launcher/launcher_platform.h shared/apple/engine_mouse.h shared/src/keyboard_mouse.cpp shared/src/platform.cpp
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 $(LAUNCHER_SHARED_INC) $(SHARED_INC) shared/tests/launcher_platform_test.cpp shared/launcher/launcher_platform.cpp shared/src/keyboard_mouse.cpp shared/src/platform.cpp -o $@
```
Add `launcher_platform_test` to `SHARED_TESTS`. Run `make build/test/launcher_platform_test` → FAIL (`launcher_platform.h` not found).

- [ ] **Step 2: Implement**

`shared/apple/engine_mouse.h`:
```cpp
#pragma once
// Feeding Minecraft PE 0.15.10's mouse (Catalyst AddressPlatform and the launcher). C++11.
#include <cstdint>

namespace mcfm {
namespace apple {

struct MouseAction {  // MouseDevice::_inputs element
  int16_t x, y, dx, dy;
  int8_t button, data;
  uint8_t pad[6];
};
static_assert(sizeof(MouseAction) == 16, "MouseAction layout");

inline int16_t clamp16(int v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : static_cast<int16_t>(v)); }

// Relative motion goes straight into Mouse::_instance's queue (MouseDevice::feed in this
// build has no dx/dy; the engine's slow path grows the vector); everything else through feed.
inline void feed_mouse(uintptr_t inputs, uintptr_t grow, uintptr_t device, uintptr_t device_feed,
                       int btn, int state, int x, int y, int dx, int dy) {
  if (dx || dy) {
    MouseAction a = {};
    a.dx = clamp16(dx);
    a.dy = clamp16(dy);
    struct Vec { MouseAction *begin, *end, *cap; };
    Vec *v = reinterpret_cast<Vec *>(inputs);
    if (v->end < v->cap) {
      *v->end = a;
      v->end++;
    } else {
      reinterpret_cast<void (*)(void *, MouseAction *)>(grow)(v, &a);
    }
    return;
  }
  reinterpret_cast<void (*)(void *, int, int, int, int)>(device_feed)(reinterpret_cast<void *>(device), btn, state,
                                                                      clamp16(x), clamp16(y));
}

}  // namespace apple
}  // namespace mcfm
```
In `shared/apple/address_platform.mm`, delete the local `MouseAction`, `clamp16` (if unused elsewhere — keep it if other code uses it) and replace the body of `AddressPlatform::mouse_feed` after `if (!attached_) return;` with:
```cpp
  apple::feed_mouse(at(addr::kMouseInputs), at(addr::kMouseInputsGrow), at(addr::kMouseDevice),
                    at(addr::kMouseDeviceFeed), btn, state, x, y, dx, dy);
```
(`#include "engine_mouse.h"`; `at()` returns the slid address as `uintptr_t` — adapt the casts to its actual return type.) Run `make macos` → builds.

Add to `addresses_0_15_10.h` (Keyboard section): `constexpr uintptr_t kKeyboardText = 0x100F5A010;  // std::vector<{std::string; bool}>`.

`shared/launcher/launcher_platform.h`:
```cpp
#pragma once
// mcfm::Platform for the Mach-O launcher: shared modules (keyboard_mouse, ...) patch our
// AppPlatform vtable and feed the engine's input globals. C++11.
#include <mcfm/platform.h>

#include <cstdint>

namespace mcfm {
namespace launcher {

struct InputAddresses {  // slid
  uintptr_t keyboard_inputs, keyboard_states, keyboard_text;
  uintptr_t mouse_device, mouse_inputs, mouse_inputs_grow, mouse_device_feed;
  static InputAddresses for_slide(uintptr_t slide);
};

class LauncherPlatform : public mcfm::Platform {
 public:
  LauncherPlatform(void **vtable, const InputAddresses &addresses) : vtable_(vtable), a_(addresses) {}
  void log(const char *msg) override;
  bool patch_slot(engine::Slot slot, void *replacement, void **original) override;
  void *global(engine::Global g) override;
  void mouse_feed(int btn, int state, int x, int y, int dx, int dy) override;

 private:
  void **vtable_;
  InputAddresses a_;
};

}  // namespace launcher
}  // namespace mcfm
```
`shared/launcher/launcher_platform.cpp`:
```cpp
#include "launcher_platform.h"

#include <cstdio>

#include "addresses_0_15_10.h"
#include "engine_mouse.h"

namespace mcfm {
namespace launcher {
namespace {

int slot_index(engine::Slot s) {  // docs/research/appplatform-vtable.md
  switch (s) {
    case engine::Slot::GetEdition: return 93;
    case engine::Slot::DefaultInputMode: return 96;
    case engine::Slot::UIScalingRules: return 99;
    case engine::Slot::UseCenteredGUI: return 68;
    case engine::Slot::PlatformType: return 69;
    case engine::Slot::UseMetadataDrivenScreens: return 66;
    case engine::Slot::HideMousePointer: return 13;
    case engine::Slot::ShowMousePointer: return 14;
  }
  return -1;
}

}  // namespace

InputAddresses InputAddresses::for_slide(uintptr_t slide) {
  InputAddresses a;
  a.keyboard_inputs = addr::kKeyboardInputs + slide;
  a.keyboard_states = addr::kKeyboardStates + slide;
  a.keyboard_text = addr::kKeyboardText + slide;
  a.mouse_device = addr::kMouseDevice + slide;
  a.mouse_inputs = addr::kMouseInputs + slide;
  a.mouse_inputs_grow = addr::kMouseInputsGrow + slide;
  a.mouse_device_feed = addr::kMouseDeviceFeed + slide;
  return a;
}

void LauncherPlatform::log(const char *msg) { std::fprintf(stderr, "mcfm: %s\n", msg); }

bool LauncherPlatform::patch_slot(engine::Slot slot, void *replacement, void **original) {
  int i = slot_index(slot);
  if (i < 0) return false;
  if (original) *original = vtable_[i];
  vtable_[i] = replacement;
  return true;
}

void *LauncherPlatform::global(engine::Global g) {
  switch (g) {
    case engine::Global::KeyboardInputs: return reinterpret_cast<void *>(a_.keyboard_inputs);
    case engine::Global::KeyboardStates: return reinterpret_cast<void *>(a_.keyboard_states);
  }
  return nullptr;
}

void LauncherPlatform::mouse_feed(int btn, int state, int x, int y, int dx, int dy) {
  apple::feed_mouse(a_.mouse_inputs, a_.mouse_inputs_grow, a_.mouse_device, a_.mouse_device_feed, btn, state, x, y, dx, dy);
}

}  // namespace launcher
}  // namespace mcfm
```
In `engine.h` add to `Engine`'s public section: `void **vtable() { return vtable_; }`.

- [ ] **Step 3: Verify and commit**

Run: `make build/test/launcher_platform_test && build/test/launcher_platform_test` → `all passed`; `make test` → all pass (incl. `make macos` via makefile_deps_test).
```bash
git add shared/apple/engine_mouse.h shared/apple/address_platform.mm shared/apple/addresses_0_15_10.h shared/launcher/launcher_platform.h shared/launcher/launcher_platform.cpp shared/launcher/engine.h shared/tests/launcher_platform_test.cpp Makefile
git commit -m "Launcher: LauncherPlatform — keyboard_mouse through our vtable"
```

---

### Task 2: Text input (keyboard slots + text queue)

**Files:** create `shared/launcher/text_input.h/.cpp`, `shared/tests/launcher_text_test.cpp`; modify `shared/launcher/app_platform.h/.cpp`, `Makefile`.

**Interfaces:**
- Produces (`mcfm::launcher`): `struct TextEvent { std::string text; bool newline; };` (32 bytes, static_assert); `void push_text(uintptr_t queue, const std::string &utf8)` — one element per UTF-8 character (invalid bytes become one element each), `void push_backspace(uintptr_t queue)`, `void push_return(uintptr_t queue)`; `queue` is the slid `kKeyboardText` (a `std::vector<TextEvent>` with the engine's libc++ ABI — the same as ours on Apple).
- Produces: `struct KeyboardCallbacks { void (*show)(const std::string &initial); void (*hide)(); }; void set_keyboard_callbacks(const KeyboardCallbacks &);` — `build_vtable` wraps slots 9 and 10: each calls the base implementation first, then the callback (if set). Calling hide without show, or show twice, is harmless.

- [ ] **Step 1: Write the failing test**

`shared/tests/launcher_text_test.cpp`:
```cpp
// Text queue protocol (as iOS ShowKeyboardView) and the keyboard slot wrappers.
#include "app_platform.h"
#include "text_input.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace mcfm::launcher;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int base_show = 0, base_hide = 0, cb_show = 0, cb_hide = 0;
static std::string shown_text;
static void base_show_fn(void *, const std::string &, int, bool, bool, const void *) { base_show++; }
static void base_hide_fn(void *) { base_hide++; }

int main() {
  std::vector<TextEvent> q;
  uintptr_t qa = reinterpret_cast<uintptr_t>(&q);
  push_text(qa, "a");
  push_text(qa, "\xC3\xA9\xF0\x9F\x98\x80z");  // é, 😀, z
  push_backspace(qa);
  push_return(qa);
  EXPECT(q.size() == 6);
  if (q.size() == 6) {
    EXPECT(q[0].text == "a" && !q[0].newline);
    EXPECT(q[1].text == "\xC3\xA9" && q[2].text == "\xF0\x9F\x98\x80" && q[3].text == "z");
    EXPECT(q[4].text == "\b" && !q[4].newline);
    EXPECT(q[5].text == "\n" && q[5].newline);
  }
  for (int i = 0; i < 1000; i++) push_text(qa, "x");  // grows like the engine's vector
  EXPECT(q.size() == 1006 && q.back().text == "x");
  push_text(qa, std::string("\xFF", 1));  // invalid byte: its own element
  EXPECT(q.back().text == std::string("\xFF", 1));

  void *base[kBaseSlots] = {}, *vt[kBaseSlots];
  base[9] = reinterpret_cast<void *>(&base_show_fn);
  base[10] = reinterpret_cast<void *>(&base_hide_fn);
  EngineFns fns = {nullptr, nullptr, nullptr, nullptr};
  build_vtable(vt, base, fns);
  auto show = reinterpret_cast<void (*)(void *, const std::string &, int, bool, bool, const void *)>(vt[9]);
  auto hide = reinterpret_cast<void (*)(void *)>(vt[10]);
  hide(nullptr);  // before any callbacks: base only, no crash
  EXPECT(base_hide == 1);
  KeyboardCallbacks cb = {[](const std::string &t) { cb_show++; shown_text = t; }, [] { cb_hide++; }};
  set_keyboard_callbacks(cb);
  float pos[2] = {0, 0};
  show(nullptr, "hello", 64, false, false, pos);
  EXPECT(base_show == 1 && cb_show == 1 && shown_text == "hello");
  hide(nullptr);
  EXPECT(base_hide == 2 && cb_hide == 1);
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("launcher_text_test: all passed\n");
  return 0;
}
```
`Makefile`:
```make
$(BUILD)/test/launcher_text_test: shared/tests/launcher_text_test.cpp shared/launcher/text_input.cpp shared/launcher/text_input.h shared/launcher/app_platform.cpp shared/launcher/app_platform.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 $(LAUNCHER_SHARED_INC) shared/tests/launcher_text_test.cpp shared/launcher/text_input.cpp shared/launcher/app_platform.cpp -o $@
```
Add `launcher_text_test` to `SHARED_TESTS`; add `shared/launcher/text_input.cpp` to `LAUNCHER_SRCS`. Run → FAIL (`text_input.h` not found).

- [ ] **Step 2: Implement**

`shared/launcher/text_input.h`:
```cpp
#pragma once
// Typed text for the engine's Keyboard text queue (kKeyboardText), as iOS's ShowKeyboardView
// feeds it: one element per character, "\b" per deleted character, {"\n", true} for return.
// The queue is the engine's std::vector<TextEvent>; the launcher always runs the iOS binary
// (libc++, same ABI as ours on Apple). C++11.
#include <cstdint>
#include <string>

namespace mcfm {
namespace launcher {

struct TextEvent {
  std::string text;
  bool newline;
};
static_assert(sizeof(TextEvent) == 32, "TextEvent must match the engine (libc++ arm64)");

void push_text(uintptr_t queue, const std::string &utf8);  // one element per UTF-8 character
void push_backspace(uintptr_t queue);
void push_return(uintptr_t queue);

}  // namespace launcher
}  // namespace mcfm
```
`shared/launcher/text_input.cpp`:
```cpp
#include "text_input.h"

#include <vector>

namespace mcfm {
namespace launcher {
namespace {

std::vector<TextEvent> &queue_at(uintptr_t queue) { return *reinterpret_cast<std::vector<TextEvent> *>(queue); }

// Bytes in the UTF-8 sequence starting with `lead` (1 for ASCII and for invalid bytes).
size_t sequence_length(unsigned char lead) {
  if (lead >= 0xF0 && lead <= 0xF4) return 4;
  if (lead >= 0xE0) return lead <= 0xEF ? 3 : 1;
  if (lead >= 0xC2) return 2;
  return 1;
}

}  // namespace

void push_text(uintptr_t queue, const std::string &utf8) {
  for (size_t i = 0; i < utf8.size();) {
    size_t n = sequence_length(static_cast<unsigned char>(utf8[i]));
    if (i + n > utf8.size()) n = 1;
    for (size_t k = 1; k < n; k++)  // continuation bytes must be 10xxxxxx
      if ((static_cast<unsigned char>(utf8[i + k]) & 0xC0) != 0x80) { n = 1; break; }
    TextEvent e;
    e.text = utf8.substr(i, n);
    e.newline = false;
    queue_at(queue).push_back(e);
    i += n;
  }
}

void push_backspace(uintptr_t queue) {
  TextEvent e;
  e.text = "\b";
  e.newline = false;
  queue_at(queue).push_back(e);
}

void push_return(uintptr_t queue) {
  TextEvent e;
  e.text = "\n";
  e.newline = true;
  queue_at(queue).push_back(e);
}

}  // namespace launcher
}  // namespace mcfm
```
In `app_platform.h` add:
```cpp
// The engine opens / closes a text box (AppPlatform slots 9 showKeyboard, 10 hideKeyboard).
struct KeyboardCallbacks {
  void (*show)(const std::string &initial_text);
  void (*hide)();
};
void set_keyboard_callbacks(const KeyboardCallbacks &callbacks);
```
In `app_platform.cpp` (anonymous namespace):
```cpp
using ShowKeyboardFn = void (*)(void *, const std::string &, int, bool, bool, const void *);
using HideKeyboardFn = void (*)(void *);
ShowKeyboardFn g_base_show = nullptr;
HideKeyboardFn g_base_hide = nullptr;
KeyboardCallbacks &keyboard_callbacks() { static KeyboardCallbacks cb = {nullptr, nullptr}; return cb; }

// Base first (it keeps isKeyboardVisible's flag), then the host.
void show_keyboard(void *self, const std::string &text, int max_length, bool limit, bool numbers, const void *pos) {
  if (g_base_show) g_base_show(self, text, max_length, limit, numbers, pos);
  if (keyboard_callbacks().show) keyboard_callbacks().show(text);
}
void hide_keyboard(void *self) {
  if (g_base_hide) g_base_hide(self);
  if (keyboard_callbacks().hide) keyboard_callbacks().hide();
}
```
outside it: `void set_keyboard_callbacks(const KeyboardCallbacks &cb) { keyboard_callbacks() = cb; }`, and in `build_vtable` after the copy loop:
```cpp
  g_base_show = reinterpret_cast<ShowKeyboardFn>(base[9]);
  g_base_hide = reinterpret_cast<HideKeyboardFn>(base[10]);
  out[9] = fn(&show_keyboard);    // showKeyboard
  out[10] = fn(&hide_keyboard);   // hideKeyboard
```
Add 9 and 10 to the `ours` list in `launcher_app_platform_test.cpp` (they are no longer base pointers).

- [ ] **Step 3: Verify and commit**

Run: the new test → `all passed`; `make test` → all pass.
```bash
git add shared/launcher/text_input.h shared/launcher/text_input.cpp shared/launcher/app_platform.h shared/launcher/app_platform.cpp shared/tests/launcher_text_test.cpp shared/tests/launcher_app_platform_test.cpp Makefile
git commit -m "Launcher: text input — keyboard slot wrappers and the engine's text queue"
```

---

### Task 3: macOS input host (keys, mouse, capture, text mode)

**Files:** create `macos/launcher/mac_keymap.h/.cpp`, `macos/tests/mac_keymap_test.cpp`, `macos/launcher/input.h/.mm`; modify `macos/launcher/resize_math.h`, `macos/tests/resize_math_test.cpp`, `macos/launcher/main.mm`, `Makefile`.

**Interfaces:**
- Produces: `int mcfm::launcher::mac_keycode_to_vk(unsigned short keycode)` (macOS `kVK_*` → Windows VK; 0 = unused). Must cover: A–Z, 0–9, F1–F12, Return 13, Tab 9, Space 32, Delete(backspace) 8, Escape 27, arrows 37–40, Shift 16, Control 17, Option 18, Command 91 (left Win), `-`=189 `=`=187 `[`=219 `]`=221 `\\`=220 `;`=186 `'`=222 `,`=188 `.`=190 `/`=191 `` ` ``=192, keypad 0–9 = 96–105.
- Produces: `void mcfm::launcher::view_to_pixels(double x_pt, double y_pt_from_bottom, double height_pt, double scale, int *x, int *y)` in `resize_math.h` — flips y, scales, floors.
- Produces (`macos/launcher/input.h`): `namespace mcfm::launcher::input { void install(NSView *view, const InputAddresses &a); void key_event(NSEvent *e); void flags_changed(NSEvent *e); void mouse_event(NSEvent *e); void scroll_event(NSEvent *e); void focus_lost(); void set_text_mode(bool on); }` — installs `keyboard_mouse` through a `LauncherPlatform` (pointer callbacks → capture/release), registers `KeyboardCallbacks` (show → text mode on, hide → off).

- [ ] **Step 1: Failing tests for the pure parts**

`macos/tests/mac_keymap_test.cpp`:
```cpp
#include "mac_keymap.h"
#include <cstdio>
static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main() {
  using mcfm::launcher::mac_keycode_to_vk;
  EXPECT(mac_keycode_to_vk(0x00) == 'A');   // kVK_ANSI_A
  EXPECT(mac_keycode_to_vk(0x0D) == 'W');   // kVK_ANSI_W
  EXPECT(mac_keycode_to_vk(0x01) == 'S');
  EXPECT(mac_keycode_to_vk(0x02) == 'D');
  EXPECT(mac_keycode_to_vk(0x12) == '1');   // kVK_ANSI_1
  EXPECT(mac_keycode_to_vk(0x1D) == '0');   // kVK_ANSI_0
  EXPECT(mac_keycode_to_vk(0x24) == 13);    // Return
  EXPECT(mac_keycode_to_vk(0x31) == 32);    // Space
  EXPECT(mac_keycode_to_vk(0x33) == 8);     // Delete (backspace)
  EXPECT(mac_keycode_to_vk(0x35) == 27);    // Escape
  EXPECT(mac_keycode_to_vk(0x38) == 16);    // Shift
  EXPECT(mac_keycode_to_vk(0x3B) == 17);    // Control
  EXPECT(mac_keycode_to_vk(0x3A) == 18);    // Option
  EXPECT(mac_keycode_to_vk(0x7B) == 37 && mac_keycode_to_vk(0x7E) == 38 && mac_keycode_to_vk(0x7C) == 39 && mac_keycode_to_vk(0x7D) == 40);
  EXPECT(mac_keycode_to_vk(0x7A) == 112);   // F1
  EXPECT(mac_keycode_to_vk(0x6F) == 123);   // F12
  EXPECT(mac_keycode_to_vk(0x52) == 96);    // keypad 0
  EXPECT(mac_keycode_to_vk(0x32) == 192);   // `
  EXPECT(mac_keycode_to_vk(0xFF) == 0);
  if (fails) return 1;
  std::printf("mac_keymap_test: all passed\n");
  return 0;
}
```
Append to `macos/tests/resize_math_test.cpp` before `if (fails)`:
```cpp
  int x = 0, y = 0;
  mcfm::launcher::view_to_pixels(10, 700, 720, 2.0, &x, &y);  // near the top of a 720-pt view
  EXPECT(x == 20 && y == 40);
  mcfm::launcher::view_to_pixels(0, 0, 720, 1.0, &x, &y);     // bottom-left
  EXPECT(x == 0 && y == 720);
```
`Makefile`: rule for `mac_keymap_test` (`clang++ -std=c++17 -Wall -O1 -Imacos/launcher macos/tests/mac_keymap_test.cpp macos/launcher/mac_keymap.cpp`), add to `MACOS_TESTS`; add `macos/launcher/mac_keymap.cpp macos/launcher/input.mm shared/launcher/launcher_platform.cpp shared/src/keyboard_mouse.cpp shared/src/platform.cpp shared/src/keymap.cpp` to `LAUNCHER_SRCS` and `$(SHARED_INC)` to `LAUNCHER_CXXFLAGS`. Run both tests → FAIL (headers/functions missing).

- [ ] **Step 2: Implement the pure parts**

`macos/launcher/mac_keymap.h`: declaration above. `macos/launcher/mac_keymap.cpp`:
```cpp
#include "mac_keymap.h"

namespace mcfm {
namespace launcher {

int mac_keycode_to_vk(unsigned short k) {
  // macOS virtual key codes (Carbon kVK_*, stable since the 1990s) -> Windows VK codes.
  static const unsigned char letters[] = {  // kVK_ANSI_* order is not alphabetical
      'A', 'S', 'D', 'F', 'H', 'G', 'Z', 'X', 'C', 'V', 0, 'B', 'Q', 'W', 'E', 'R', 'Y', 'T'};
  if (k < sizeof letters) return letters[k];
  switch (k) {
    case 0x12: return '1'; case 0x13: return '2'; case 0x14: return '3'; case 0x15: return '4';
    case 0x17: return '5'; case 0x16: return '6'; case 0x1A: return '7'; case 0x1C: return '8';
    case 0x19: return '9'; case 0x1D: return '0';
    case 0x1F: return 'O'; case 0x20: return 'U'; case 0x22: return 'I'; case 0x23: return 'P';
    case 0x25: return 'L'; case 0x26: return 'J'; case 0x28: return 'K'; case 0x2D: return 'N';
    case 0x2E: return 'M';
    case 0x18: return 187; case 0x1B: return 189; case 0x1E: return 221; case 0x21: return 219;
    case 0x27: return 222; case 0x29: return 186; case 0x2A: return 220; case 0x2B: return 188;
    case 0x2C: return 191; case 0x2F: return 190; case 0x32: return 192;
    case 0x24: return 13; case 0x30: return 9; case 0x31: return 32; case 0x33: return 8;
    case 0x35: return 27; case 0x75: return 46;  // forward delete
    case 0x38: case 0x3C: return 16;  // shift
    case 0x3B: case 0x3E: return 17;  // control
    case 0x3A: case 0x3D: return 18;  // option
    case 0x37: case 0x36: return 91;  // command
    case 0x7B: return 37; case 0x7E: return 38; case 0x7C: return 39; case 0x7D: return 40;
    case 0x7A: return 112; case 0x78: return 113; case 0x63: return 114; case 0x76: return 115;
    case 0x60: return 116; case 0x61: return 117; case 0x62: return 118; case 0x64: return 119;
    case 0x65: return 120; case 0x6D: return 121; case 0x67: return 122; case 0x6F: return 123;
    case 0x52: return 96; case 0x53: return 97; case 0x54: return 98; case 0x55: return 99;
    case 0x56: return 100; case 0x57: return 101; case 0x58: return 102; case 0x59: return 103;
    case 0x5B: return 104; case 0x5C: return 105;
  }
  return 0;
}

}  // namespace launcher
}  // namespace mcfm
```
Add to `resize_math.h`:
```cpp
// A point in an AppKit view (origin bottom-left, points) -> engine pixels (origin top-left).
inline void view_to_pixels(double x_pt, double y_pt, double height_pt, double scale, int *x, int *y) {
  *x = static_cast<int>(std::floor(x_pt * scale));
  *y = static_cast<int>(std::floor((height_pt - y_pt) * scale));
}
```
Run both tests → pass.

- [ ] **Step 3: Implement the host input (`input.h/.mm`)**

`macos/launcher/input.h`:
```objc
#pragma once
#import <AppKit/AppKit.h>
#include "launcher_platform.h"

namespace mcfm {
namespace launcher {
namespace input {
void install(NSView *view, void **vtable, const InputAddresses &addresses);
void key_event(NSEvent *e);      // keyDown / keyUp
void flags_changed(NSEvent *e);  // modifier keys
void mouse_event(NSEvent *e);    // move, drag, buttons
void scroll_event(NSEvent *e);
void focus_lost();               // app/window resigned: release keys, buttons, pointer
}  // namespace input
}  // namespace launcher
}  // namespace mcfm
```
`macos/launcher/input.mm`:
```objc
#include "input.h"

#include <mcfm/input/input_state.h>
#include <mcfm/input/keymap.h>
#include <mcfm/keyboard_mouse.h>

#import <QuartzCore/QuartzCore.h>
#include <ApplicationServices/ApplicationServices.h>

#include "app_platform.h"
#include "mac_keymap.h"
#include "resize_math.h"
#include "text_input.h"

namespace mcfm {
namespace launcher {
namespace input {
namespace {

namespace kbm = mcfm::keyboard_mouse;
__weak NSView *g_view = nil;
LauncherPlatform *g_platform = nullptr;
uintptr_t g_text_queue = 0;
bool g_text_mode = false, g_capture_wanted = false, g_captured = false;
mcfm::HeldSet g_keys, g_buttons;
mcfm::ScrollAccumulator g_scroll{1.0f};
int g_x = 0, g_y = 0;

void apply_capture() {
  bool want = g_capture_wanted && NSApp.isActive && g_view.window.isKeyWindow;
  if (want == g_captured) return;
  g_captured = want;
  CGAssociateMouseAndMouseCursorPosition(want ? false : true);
  if (want) {
    [NSCursor hide];
    NSRect f = [g_view.window convertRectToScreen:[g_view convertRect:g_view.bounds toView:nil]];
    CGFloat top = NSScreen.screens.firstObject.frame.size.height;  // CG uses top-left origin
    CGWarpMouseCursorPosition(CGPointMake(NSMidX(f), top - NSMidY(f)));
  } else {
    [NSCursor unhide];
  }
}

void key(int vk, bool down) {
  if (g_text_mode && down && !mcfm::passes_while_typing(vk)) return;  // presses belong to the text box
  if (g_keys.set(vk, down)) kbm::key(vk, down);
}

void button(int btn, bool down) {
  if (g_buttons.set(btn, down)) kbm::mouse_button(btn, down, g_x, g_y);
}

void update_position(NSEvent *e) {
  NSPoint p = [g_view convertPoint:e.locationInWindow fromView:nil];
  view_to_pixels(p.x, p.y, g_view.bounds.size.height, g_view.window.backingScaleFactor, &g_x, &g_y);
}

void type_text(NSEvent *e) {  // text mode: characters go to the engine's text queue
  NSString *chars = e.characters;
  for (NSUInteger i = 0; i < chars.length; i++) {
    unichar c = [chars characterAtIndex:i];
    if (c == 0x7F || c == 0x08) push_backspace(g_text_queue);
    else if (c == '\r' || c == '\n' || c == 0x03) push_return(g_text_queue);
    else if (c >= 0xF700 && c <= 0xF8FF) continue;  // function/arrow keys: no text
    else if (c < 0x20) continue;
    else {
      NSRange r = [chars rangeOfComposedCharacterSequenceAtIndex:i];
      push_text(g_text_queue, [[chars substringWithRange:r] UTF8String]);
      i = NSMaxRange(r) - 1;
    }
  }
}

}  // namespace

void install(NSView *view, void **vtable, const InputAddresses &a) {
  g_view = view;
  g_text_queue = a.keyboard_text;
  g_platform = new LauncherPlatform(vtable, a);
  kbm::PointerCallbacks pc = {[] { g_capture_wanted = true; apply_capture(); },
                              [] { g_capture_wanted = false; apply_capture(); }};
  kbm::install(*g_platform, pc);
  KeyboardCallbacks kc = {[](const std::string &) { g_text_mode = true; }, [] { g_text_mode = false; }};
  set_keyboard_callbacks(kc);
  NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
  [nc addObserverForName:NSApplicationDidResignActiveNotification object:nil queue:NSOperationQueue.mainQueue
              usingBlock:^(NSNotification *) { focus_lost(); }];
  [nc addObserverForName:NSApplicationDidBecomeActiveNotification object:nil queue:NSOperationQueue.mainQueue
              usingBlock:^(NSNotification *) { apply_capture(); }];
}

void key_event(NSEvent *e) {
  bool down = e.type == NSEventTypeKeyDown;
  int vk = mac_keycode_to_vk(e.keyCode);
  if (g_text_mode && down && vk != 27) {
    type_text(e);
    return;
  }
  if (down && e.isARepeat) return;  // the engine has its own key repeat
  if (vk) key(vk, down);
}

void flags_changed(NSEvent *e) {
  int vk = mac_keycode_to_vk(e.keyCode);
  if (!vk) return;
  NSEventModifierFlags f = e.modifierFlags;
  bool down = (vk == 16 && (f & NSEventModifierFlagShift)) || (vk == 17 && (f & NSEventModifierFlagControl)) ||
              (vk == 18 && (f & NSEventModifierFlagOption)) || (vk == 91 && (f & NSEventModifierFlagCommand));
  key(vk, down);
}

void mouse_event(NSEvent *e) {
  if (g_captured && (e.type == NSEventTypeMouseMoved || e.type == NSEventTypeLeftMouseDragged ||
                     e.type == NSEventTypeRightMouseDragged || e.type == NSEventTypeOtherMouseDragged)) {
    kbm::mouse_move_rel(static_cast<int>(e.deltaX), static_cast<int>(e.deltaY));
  } else {
    update_position(e);
    kbm::mouse_move_abs(g_x, g_y);
  }
  switch (e.type) {
    case NSEventTypeLeftMouseDown: button(1, true); break;
    case NSEventTypeLeftMouseUp: button(1, false); break;
    case NSEventTypeRightMouseDown: button(2, true); break;
    case NSEventTypeRightMouseUp: button(2, false); break;
    case NSEventTypeOtherMouseDown: button(3, true); break;
    case NSEventTypeOtherMouseUp: button(3, false); break;
    default: break;
  }
}

void scroll_event(NSEvent *e) {
  int notches = g_scroll.feed(static_cast<float>(e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / 10.0 : e.scrollingDeltaY),
                              CACurrentMediaTime());
  if (notches) kbm::mouse_wheel(notches, g_x, g_y);
}

void focus_lost() {
  g_keys.release_all([](int vk) { kbm::key(vk, false); });
  g_buttons.release_all([](int b) { kbm::mouse_button(b, false, g_x, g_y); });
  apply_capture();
}

}  // namespace input
}  // namespace launcher
}  // namespace mcfm
```
(`ScrollAccumulator` comes from `<mcfm/input/keymap.h>`; `CGAssociateMouseAndMouseCursorPosition`/`CGWarpMouseCursorPosition` from ApplicationServices — the launcher host may use Apple frameworks; only the game image may not.)

- [ ] **Step 4: Wire it into `main.mm`**

In `McfmView` add:
```objc
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
```
After `_engine.start(...)` succeeds: `mcfm::launcher::input::install(self.window.contentView, _engine.vtable(), InputAddresses::for_slide(g_slide));`, `self.window.acceptsMouseMovedEvents = YES;`, `[self.window makeFirstResponder:self.window.contentView];`. Add `- (void)windowDidResignKey:(NSNotification *)n { mcfm::launcher::input::focus_lost(); }`.

- [ ] **Step 5: Verify and commit**

Run: `make test` → all pass. With the game: `make launcher GAME=… && make launcher-check` → passes; `make launcher-run` and check by hand (ask the owner): menu hover/click, Esc, WASD/Space/Shift, mouse look while captured, Cmd-Tab releases the cursor, chat (T) typing incl. é and backspace, Enter sends. Record results in the ledger.
```bash
git add macos/launcher/mac_keymap.h macos/launcher/mac_keymap.cpp macos/tests/mac_keymap_test.cpp macos/launcher/input.h macos/launcher/input.mm macos/launcher/resize_math.h macos/tests/resize_math_test.cpp macos/launcher/main.mm Makefile
git commit -m "Launcher: keyboard, mouse, pointer capture and text entry on macOS"
```

---

### Task 4: Sound (AudioToolbox provider)

**Files:** create `macos/launcher/audio_toolbox.cpp`, `macos/tests/audio_toolbox_test.cpp`; modify `macos/tools/make_launcher.sh`, `Makefile`.

**Interfaces:**
- Produces: `libmcfm_audiotoolbox.dylib` (install name `@rpath/libmcfm_audiotoolbox.dylib`) exporting, with AudioToolbox's C signatures: `AudioSessionGetProperty`, `AudioComponentFindNext`, `AudioComponentInstanceNew`, `AudioComponentInstanceDispose`, `AudioUnitInitialize`, `AudioUnitUninitialize`, `AudioUnitSetProperty`, `AudioUnitGetProperty`, `AudioUnitRender`, `AudioOutputUnitStart`, `AudioOutputUnitStop`.
  - `AudioSessionGetProperty('choc')` → 2 (UInt32), `'chsr'` → 48000.0 (Float64), `'acat'` → `'ambi'`; others → `'pty?'` (kAudioSessionUnsupportedPropertyError, 0x7074793F). A too-small `ioDataSize` → `'!siz'`.
  - `AudioComponentFindNext`: a description with subtype `'rioc'` (RemoteIO) is searched as `'def '` (DefaultOutput); everything forwarded to the real AudioToolbox, found with `dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox")`. If the framework or a symbol is missing (env `MCFM_AUDIO_DISABLE=1` simulates it for the test), every call returns `-1` (`kAudio_UnimplementedError` is `-4`; use `-4`) and `FindNext` returns null — FMOD then runs without output.
- make_launcher builds it and passes `--provider "AudioToolbox=$OUT/libmcfm_audiotoolbox.dylib"`.

- [ ] **Step 1: Write the failing test**

`macos/tests/audio_toolbox_test.cpp`:
```cpp
// The AudioToolbox provider: iOS session answers, RemoteIO mapped to the default output,
// and a clean failure when the real framework is unavailable.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

typedef int32_t OSStatus;
struct ComponentDesc { uint32_t type, subtype, manufacturer, flags, mask; };
static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

int main(int, char **argv) {
  void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) { std::printf("FAIL: %s\n", dlerror()); return 1; }
  auto session = (OSStatus (*)(uint32_t, uint32_t *, void *))dlsym(lib, "AudioSessionGetProperty");
  auto find = (void *(*)(void *, const ComponentDesc *))dlsym(lib, "AudioComponentFindNext");
  auto create = (OSStatus (*)(void *, void **))dlsym(lib, "AudioComponentInstanceNew");
  auto dispose = (OSStatus (*)(void *))dlsym(lib, "AudioComponentInstanceDispose");
  EXPECT(session && find && create && dispose);
  uint32_t channels = 0, size = 4;
  EXPECT(session(FOURCC('c', 'h', 'o', 'c'), &size, &channels) == 0 && channels == 2 && size == 4);
  double rate = 0; size = 8;
  EXPECT(session(FOURCC('c', 'h', 's', 'r'), &size, &rate) == 0 && rate == 48000.0);
  uint32_t category = 0; size = 4;
  EXPECT(session(FOURCC('a', 'c', 'a', 't'), &size, &category) == 0 && category == FOURCC('a', 'm', 'b', 'i'));
  uint32_t x = 0; size = 4;
  EXPECT(session(FOURCC('x', 'x', 'x', 'x'), &size, &x) == (OSStatus)FOURCC('p', 't', 'y', '?'));
  size = 1;
  EXPECT(session(FOURCC('c', 'h', 'o', 'c'), &size, &channels) == (OSStatus)FOURCC('!', 's', 'i', 'z'));
  ComponentDesc remote_io = {FOURCC('a', 'u', 'o', 'u'), FOURCC('r', 'i', 'o', 'c'), FOURCC('a', 'p', 'p', 'l'), 0, 0};
  bool disabled = std::getenv("MCFM_AUDIO_DISABLE") != nullptr;
  void *component = find(nullptr, &remote_io);
  if (disabled) {
    EXPECT(component == nullptr);
    void *unit = nullptr;
    EXPECT(create(nullptr, &unit) != 0);
  } else {
    EXPECT(component != nullptr);  // RemoteIO does not exist on macOS: mapped to DefaultOutput
    void *unit = nullptr;
    EXPECT(component && create(component, &unit) == 0 && unit != nullptr);
    if (unit) EXPECT(dispose(unit) == 0);
  }
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("audio_toolbox_test: all passed%s\n", disabled ? " (framework disabled)" : "");
  return 0;
}
```
`Makefile`:
```make
AUDIO_PROVIDER := $(BUILD)/launcher/libmcfm_audiotoolbox.dylib
$(AUDIO_PROVIDER): macos/launcher/audio_toolbox.cpp
	@mkdir -p $(dir $@)
	clang++ -arch arm64 -mmacosx-version-min=11.0 -std=c++17 -O2 -Wall -Wextra -dynamiclib macos/launcher/audio_toolbox.cpp \
	  -install_name @rpath/libmcfm_audiotoolbox.dylib -o $@
$(BUILD)/test/audio_toolbox_test: macos/tests/audio_toolbox_test.cpp
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -Wall -O1 macos/tests/audio_toolbox_test.cpp -o $@
```
In `test:` add: `	$(MAKE) --no-print-directory $(AUDIO_PROVIDER) $(BUILD)/test/audio_toolbox_test && $(BUILD)/test/audio_toolbox_test $(AUDIO_PROVIDER) && MCFM_AUDIO_DISABLE=1 $(BUILD)/test/audio_toolbox_test $(AUDIO_PROVIDER)`. Run → FAIL (no rule for `audio_toolbox.cpp`).

- [ ] **Step 2: Implement**

`macos/launcher/audio_toolbox.cpp`:
```cpp
// AudioToolbox for the game image's FMOD (docs/LAUNCHER.md, Stage 1c): the functions FMOD's
// iOS output calls, on macOS. RemoteIO (iOS only) becomes the default output unit, the iOS-only
// AudioSession is answered here, the rest goes to the real AudioToolbox. No Apple headers:
// AudioSessionGetProperty is unavailable in the macOS SDK.
#include <dlfcn.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

typedef int32_t OSStatus;
struct ComponentDesc { uint32_t type, subtype, manufacturer, flags, mask; };

constexpr uint32_t fourcc(const char (&s)[5]) {
  return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 | uint8_t(s[3]);
}
constexpr OSStatus kUnimplemented = -4;  // kAudio_UnimplementedError

void *real(const char *name) {
  static void *lib = std::getenv("MCFM_AUDIO_DISABLE")
                         ? nullptr
                         : dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox", RTLD_NOW | RTLD_LOCAL);
  return lib ? dlsym(lib, name) : nullptr;
}

#define FORWARD(ret, name, params, args, fallback)        \
  extern "C" ret name params {                            \
    using Fn = ret(*) params;                             \
    static Fn fn = reinterpret_cast<Fn>(real(#name));     \
    return fn ? fn args : fallback;                       \
  }

}  // namespace

extern "C" OSStatus AudioSessionGetProperty(uint32_t id, uint32_t *size, void *data) {
  auto put = [&](const void *value, uint32_t n) -> OSStatus {
    if (!size || *size < n) return static_cast<OSStatus>(fourcc("!siz"));
    std::memcpy(data, value, n);
    *size = n;
    return 0;
  };
  if (id == fourcc("choc")) { uint32_t v = 2; return put(&v, 4); }               // output channels
  if (id == fourcc("chsr")) { double v = 48000.0; return put(&v, 8); }           // hardware sample rate
  if (id == fourcc("acat")) { uint32_t v = fourcc("ambi"); return put(&v, 4); }  // ambient: no recording
  return static_cast<OSStatus>(fourcc("pty?"));
}

extern "C" void *AudioComponentFindNext(void *after, const ComponentDesc *desc) {
  using Fn = void *(*)(void *, const ComponentDesc *);
  static Fn fn = reinterpret_cast<Fn>(real("AudioComponentFindNext"));
  if (!fn || !desc) return nullptr;
  ComponentDesc d = *desc;
  if (d.type == fourcc("auou") && d.subtype == fourcc("rioc")) d.subtype = fourcc("def ");  // RemoteIO -> default output
  return fn(after, &d);
}

FORWARD(OSStatus, AudioComponentInstanceNew, (void *c, void **out), (c, out), kUnimplemented)
FORWARD(OSStatus, AudioComponentInstanceDispose, (void *u), (u), kUnimplemented)
FORWARD(OSStatus, AudioUnitInitialize, (void *u), (u), kUnimplemented)
FORWARD(OSStatus, AudioUnitUninitialize, (void *u), (u), kUnimplemented)
FORWARD(OSStatus, AudioUnitSetProperty, (void *u, uint32_t id, uint32_t scope, uint32_t el, const void *d, uint32_t n),
        (u, id, scope, el, d, n), kUnimplemented)
FORWARD(OSStatus, AudioUnitGetProperty, (void *u, uint32_t id, uint32_t scope, uint32_t el, void *d, uint32_t *n),
        (u, id, scope, el, d, n), kUnimplemented)
FORWARD(OSStatus, AudioUnitRender, (void *u, uint32_t *flags, const void *ts, uint32_t bus, uint32_t frames, void *list),
        (u, flags, ts, bus, frames, list), kUnimplemented)
FORWARD(OSStatus, AudioOutputUnitStart, (void *u), (u), kUnimplemented)
FORWARD(OSStatus, AudioOutputUnitStop, (void *u), (u), kUnimplemented)
```
In `macos/tools/make_launcher.sh`: take a 5th argument `AUDIO="$5"`, `cp "$AUDIO" "$OUT/"` before `build_stubs.sh`, and add `--provider "AudioToolbox=$OUT/libmcfm_audiotoolbox.dylib"`. In the Makefile `launcher:` target depend on `$(AUDIO_PROVIDER)` and pass `"$(AUDIO_PROVIDER)"` as the 5th argument.

- [ ] **Step 3: Verify and commit**

Run: `make test` → all pass (both audio test runs). With the game: `make launcher … && make launcher-check` → passes; the census no longer lists `AudioSessionGetProperty`/`AudioComponentFindNext`; `make launcher-run` → title-screen sounds/clicks audible (ask the owner).
```bash
git add macos/launcher/audio_toolbox.cpp macos/tests/audio_toolbox_test.cpp macos/tools/make_launcher.sh Makefile
git commit -m "Launcher: sound — AudioToolbox provider maps FMOD's RemoteIO to CoreAudio"
```

---

### Task 5: No Xbox Live first-launch prompt

**Files:** modify `shared/launcher/seams.cpp`, `shared/apple/addresses_0_15_10.h`, `shared/tests/launcher_engine_test.cpp`; add findings to `docs/research/macho-launcher.md`.

- [ ] **Step 1: Find what opens the prompt (IDA)**

Run, from the main checkout:
```bash
.venv/bin/python -I tools/ida/q.py game-files/ida/minecraftpe2-arm64.i64 d 0x100223FB8 d 0x100223DE0 d 0x100223D28 x 0x100223FB8 x 0x100223DE0 x 0x100223D28
```
Identify the constructor that installs the vtable containing `0x100224594` (the screen that reads `xbox.firstsignin.*`), then follow `x` (callers) up until the code that decides to push the screen (expect a check like "signed in?" / "first launch shown?" in the start-screen or `MinecraftClient` code). Choose the smallest hook that makes the decision "don't show": the deciding predicate if it is a separate function ≥ 12 bytes (return false), otherwise the function that creates/pushes the screen (no-op). Record the chain in the ledger.

- [ ] **Step 2: Test first, then the hook**

Add to `launcher_engine_test.cpp` an `EXPECT` that a hook named `xbl_first_signin` exists at the chosen address and that calling its replacement with the original's signature returns the "don't show" value (or is a no-op). Run → FAIL. Add the address constant and the `Hook` with a comment explaining the chain. Run → pass; `make test` → all pass.

- [ ] **Step 3: Verify on the game and commit**

`make launcher … && dist/launcher/mcfm-launch --frames 300 --screenshot <scratch>/title.ppm` → the title screen without the prompt (look at the image). `make launcher-check` → passes.
```bash
git add shared/launcher/seams.cpp shared/apple/addresses_0_15_10.h shared/tests/launcher_engine_test.cpp docs/research/macho-launcher.md
git commit -m "Launcher: no Xbox Live first-launch prompt (Xbox Live is dropped)"
```

---

### Task 6: Play session and census-driven fixes

- [ ] **Step 1: Owner session**

Ask the owner to run `make launcher-run` (with `MCFM_CENSUS=build/launcher/census.txt`) and play: create a world, walk/jump/sneak, break/place, open inventory, chat, change settings (incl. GUI scale and full screen), resize the window, quit to title and re-enter the world, quit the app. Collect: crashes (with `lldb` backtraces as in Stage 1b Task 7), wrong behaviour, and the census.

- [ ] **Step 2: Fix loop**

For each issue, one fix with a failing test first (slot → `launcher_app_platform_test`; seam → hook + `launcher_engine_test`; input → `launcher_platform_test`/`mac_keymap_test`/`launcher_text_test`; audio → `audio_toolbox_test`). Seam #4 (TCUI: friend finder, profile card, telemetry `0x1008955B0`, `0x100895924`, `0x100895B80`, `0x1008962C0`, `0x100896978`) becomes no-op hooks as soon as any of them is reached. Ledger each iteration.

Acceptance (Stage 1, docs/LAUNCHER.md): the owner plays a world with keyboard, mouse, sound and resizing; `make test` green; `make launcher-check` green.

---

### Task 7: Stage 1 lands — `app`/`run`/`check` move to the launcher; docs

Only after Task 6's acceptance (the owner confirms).

- [ ] **Step 1: Test first**

In `tools/tests/catalyst_deprecated_test.sh`, change the alias expectations: `make -n app` must run `macos/tools/make_launcher.sh` (not `convert.sh`) and must not print the deprecation note; `run` → `mcfm-launch`; `check` → `--frames 120`; the `catalyst*` targets keep their expectations. Rename the test to `tools/tests/make_targets_test.sh` (update `Makefile`). Run → FAIL.

- [ ] **Step 2: Implement**

Point `app: launcher`, `run: launcher-run`, `check: launcher-check` in the `Makefile`; remove the alias lines from the Catalyst section and update its header comment ("`app`/`run`/`check` now build the launcher").

- [ ] **Step 3: Docs**

`docs/LAUNCHER.md`: Stage 1 ☑ (items 6–9, 1c plan link, acceptance met on <date>), Decisions: `make app/run/check` = launcher. `docs/research/macho-launcher.md`: Stage 1c findings (text protocol, audio path, prompt chain, census after a play session). `docs/HANDOFF.md` (state table: macOS = launcher; daily commands), `README.md` quick start (launcher first; Catalyst as deprecated), `CLAUDE.md` commands.

- [ ] **Step 4: Verify and commit**

`make test` → all pass; `make app GAME=… && make check` → passes.
```bash
git add Makefile tools/tests docs README.md CLAUDE.md
git commit -m "Stage 1 lands: make app/run/check build and run the launcher"
```
