# mcpe-kbm: keyboard + mouse for Minecraft PE 0.15.10 (iOS build on Apple Silicon macOS)

## Goal
Play the Catalyst-converted `minecraftpe2.app` (0.15.10, iOS arm64, decrypted) with PC-style
controls and the Win10 desktop GUI: WASD, mouse look with captured cursor, left/right click,
scroll/number-key hotbar, E inventory, Esc back/pause, normal cursor with hover in menus.
Resizable window with the game following the window size.
Single-player focus. The original `minecraftpe2.app` is never modified.

## Non-goals
Gamepad support, in-mod key remapping, multiplayer / Xbox Live fixes, other game versions.

## Background (reverse-engineered, unslid addresses, image base 0x100000000)
Reference: ry-diffusion/Lachy-Launcher (`mcpelauncher-client/src/window_callbacks.cpp`,
`main.cpp`) feeds the Android build of the same engine.

| Item | Address | Notes |
|---|---|---|
| AppPlatform singleton ptr | `0x100F5E850` | `AppPlatform*` |
| AppPlatform_iOS vtable (vptr value) | `0x100EABE00` | base AppPlatform vtable `0x100E649C0` |
| slot `+744` getEdition | returns `"pocket"` (std::string via x8 sret) | want `"win10"` |
| slot `+768` default input mode | iOS `2` (touch), base `1` (mouse) | want 1 |
| slot `+792` UI scaling rules | iOS `1` (pocket), base `0` (desktop) | want 0 |
| slot `+104` hideMousePointer | empty | called when game grabs mouse (Minecraft+321 flag) |
| slot `+112` showMousePointer | empty | called when game releases mouse |
| `Keyboard::_inputs` | `0x100F59FF8` | `std::vector<{int32 state; uint8 key; pad}>` (8 B) |
| `Keyboard::_states` | `0x100F59BF8` | `int32[256]`, indexed by key |
| Keyboard text inputs | `0x100F5A010` | `std::vector<{std::string; bool}>` (32 B), used by iOS text view |
| `Mouse::_instance` (MouseDevice) | `0x100F5A040` | `_inputs` vector at `+0x18` (`0x100F5A058`), 16 B actions |
| `MouseDevice::feed(dev, btn, state, x, y)` | `sub_1000201BC` | dx/dy written as 0 |
| `Multitouch::feed` | `sub_100020EFC` | used by `touchesBegan` etc. |

MouseAction layout (16 B): `int16 x, y, dx, dy; int8 button; int8 data; pad`. Buttons: 0 move,
1 left, 2 right, 3 middle, 4 wheel (data = signed delta). Mouse mapper (`sub_1000209C0`) turns
non-zero dx/dy into look deltas and zero-delta moves into pointer position. Keyboard and mouse
mappers are always constructed (`sub_100017854`), the input mode only selects UI/behaviour.
Key codes are Windows virtual-key codes (Enter = 13 confirmed in iOS text-view code).

## Design
`libmcpekbm.dylib` (Objective-C++, arm64, Mac Catalyst platform) loaded into the converted app.

### Units
1. **`addresses.h`**: all constants above, resolved at runtime as `_dyld_get_image_vmaddr_slide(0) + addr`.
   A startup check verifies the bytes of the patched vtable slots point at the expected
   functions (`0x100460ABC`, `0x1007074F4`, `0x1007074EC`, base empty fns); on mismatch the
   dylib logs and does nothing (wrong binary guard).
2. **`platform_patch`**: makes the vtable page writable (`vm_protect` with `VM_PROT_COPY`) and
   replaces slots 744/768/792/104/112 with our functions. getEdition constructs a libc++ std::string "win10"
   into the sret pointer (x8).
3. **`engine_input`**: thin C API over the engine globals:
   `ei_key(uint8 vk, bool down)`, `ei_mouse_button(btn, down, x, y)`, `ei_mouse_move_abs(x, y)`,
   `ei_mouse_move_rel(dx, dy)`, `ei_mouse_wheel(delta, x, y)`. Pushes into the engine vectors
   using the app's own libc++ `std::vector` layout (begin/end/cap); growth via engine's
   `push_back` slow path is avoided by our own realloc-free strategy: if `end == cap`, call
   the engine's slow-path functions (`sub_10070B400` for keyboard, `sub_100020404` for mouse).
   All calls run on the main thread (UIKit / GameController handlers dispatch to main queue; game ticks in CADisplayLink on main).
4. **`keymap`**: pure function GCKeyCode (HID usage) to Windows VK. Unit-tested on host.
5. **`mac_input`**: GCKeyboard / GCMouse handlers (`GCKeyboardDidConnectNotification`,
   `GCMouseDidConnectNotification`, plus already-connected `GCKeyboard.coalescedKeyboard` /
   `GCMouse.current`). Keyboard events are suppressed while a `UITextField`/`UITextView`
   is first responder. Mouse deltas are fed as relative motion only while captured.
   A `UIHoverGestureRecognizer` on the game view feeds absolute position (points × contentScaleFactor,
   same scale as the touch path) while not captured. Game's `touches*` methods on
   `minecraftpeViewController` are swizzled to no-ops so clicks don't double-feed.
6. **`pointer_lock`**: hide/show slots set a `wanted` flag; capture applies while the app is active:
   warp the cursor to the window centre (`CGWarpMouseCursorPosition`), then
   `CGAssociateMouseAndMouseCursorPosition(false)` + `CGDisplayHideCursor` (all via dlsym; works windowed
   and full screen). Window losing focus releases capture; regaining it re-captures if still wanted.
   (`prefersPointerLocked` was dropped: CoreGraphics covers both cases.)

7. **`window_resize`**: window resizing.
   - Background: `-[EAGLView layoutSubviews]` already deletes the framebuffer and
     `setFramebuffer` recreates it at the new size. But the engine is told its size only once, in
     `-[minecraftpeViewController initView]`, via `App` vtable slots 21 (`(app, w, h)`) and
     20 (`(app, w, h, float 0)`). `-[minecraftpeViewController width/height]` use
     `max/min(UIScreen.mainScreen.bounds) * viewScale`, i.e. the display, not the window. Result
     today: the game renders at the launch size in the bottom-left corner, with black around it.
   - Swizzle `width` / `height` to return `view.bounds.size.{width,height} * viewScale`.
   - Swizzle `-[EAGLView layoutSubviews]`: call the original, then if the new pixel size differs
     from the last one sent, call App slots 21 and 20 exactly as `initView` does (main thread, same
     as drawFrame, so no race).
   - Set `windowScene.sizeRestrictions.minimumSize` to 800×500 points.
   - Bundle step: delete `UIRequiresFullScreen` from the converted copy's Info.plist (Catalyst
     otherwise locks the window size).

### Injection
Dev: `DYLD_INSERT_LIBRARIES=libmcpekbm.dylib ./minecraftpe2`. Final: `tools/inject.py` adds
`LC_LOAD_DYLIB @executable_path/Frameworks/libmcpekbm.dylib` into header padding of the
converted binary, then ad-hoc re-sign the bundle. Only the copy at `MinecraftPE-Mac/` is changed.

### Error handling
Address guard failure: no patching, log `mcpekbm: unexpected binary, disabled`. Missing
GCMouse/GCKeyboard: log and keep touch path (do not swizzle touches). All logging via `NSLog` prefixed `mcpekbm:`.

### Testing
- Host unit test for `keymap`.
- Launch test: log shows `mcpekbm: patched`, app stays alive >15 s, no crash report.
- Manual (user): window resize re-lays out the game with no black area; desktop GUI visible, menu hover/click, world: WASD/mouse look/click/E/Esc/1-9/scroll, chat typing.
