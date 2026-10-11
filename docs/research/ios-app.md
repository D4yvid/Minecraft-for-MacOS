# iOS app — research (Stage 4)

Our own Swift iOS app running the game's iOS image ([LAUNCHER.md](../LAUNCHER.md) Stage 4).
✅ = verified on the owner's iPhone 16 Pro Max (iPhone17,2, iOS 27), ❓ = to verify.

## Loading: dyld, not our loader
- iOS runs no code that was not signed when the app was built (no unsigned executable memory
  without a debugger attached), so our loader (`shared/loader`, which copies segments into
  anonymous memory) cannot run on iOS. The game binary is converted at build time
  (`mcfm_image.py dylib --hooks --platform ios --host AudioToolbox`: an MH_DYLIB tagged iOS 15),
  signed into `Frameworks/` and loaded with `dlopen`; `_dyld_register_func_for_add_image` fills
  the hook table before the game's initializers run, as the macOS launcher's `--loader dyld`. ✅
- The LC_UUID guard runs on the file before `dlopen`. ✅
- The game's iOS glue (UIKit, its view controllers, Xbox, StoreKit, GameController, …) gets the
  same generated stubs as on the Mac (`build_stubs.sh --target ios`); libSystem, libc++, libz and
  AudioToolbox stay the system's (everything the game imports from AudioToolbox, including the
  deprecated `AudioSessionGetProperty`, is still in the iOS 27 SDK). ✅
- The UIScene lifecycle is used: Apple requires it for apps built with the SDK after iOS 26. ✅

## Graphics: ANGLE, not the system OpenGL ES
- First tried with the system's OpenGL ES (EAGL, `CAEAGLLayer`, our framebuffer): the engine
  binds framebuffer **0** for the screen (`glBindFramebuffer(GL_FRAMEBUFFER, 0)` /
  `GL_DRAW_FRAMEBUFFER, 0` in its render targets), which iOS does not have
  (`GL_FRAMEBUFFER_UNDEFINED`, `GL_INVALID_FRAMEBUFFER_OPERATION` every frame, a black screen).
  The game's own `-[EAGLView setFramebuffer]` binds its FBO before each frame, so something in
  its iOS glue must route 0 — not needed once ANGLE draws: an EGL window surface **is**
  framebuffer 0, as on the Mac and Android. ✅
- ANGLE for iOS: Godot's static build (`godotengine/godot-angle-static`, release `chromium/7578`,
  `godot-angle-static-arm64-ios-release.zip`, 4.1 MB, SHA-256
  `ee57e8e6…548f`) linked into one `libGLESv2.dylib` (EGL + GLES, 8.5 MB) by `make angle-ios`.
  Godot supplies three functions itself, here `tools/launcher/angle_ios_shims.cpp`:
  `angle::GetCurrentSystemTime`, `angle::SetCurrentThreadName`, and astcenc (the ASTC software
  decoder: reported unavailable; every iOS GPU since the A8 samples ASTC). ✅
- The game's OpenGLES stub re-exports that dylib (a provider, as ANGLE on the Mac); the EAGL
  classes stay stubbed. The Swift app links no OpenGLES: all its GL goes through
  `ios/launcher/egl_view.mm` to ANGLE. ✅
- Metal display (`EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE`), window surface on the view's
  `CAMetalLayer` (`drawableSize` = the native pixel size): the title screen at 2868×1320, full
  screen, no status bar, 60 fps target (`CADisplayLink`). ✅

## Lifecycle, input
- At launch the first drawable comes before the scene is active: the engine starts, pauses
  (`suspend`, saves), and resumes when the scene becomes active; the app reads the scene's real
  state after starting. Frames stop while inactive (no GL in the background). ✅
- Touch: per-finger ids (`UITouch` → small integers), pixels, through the shared
  `touch_input` to `Multitouch::feed` (Play → Create a World by touch). ✅
- Keyboard: the view is always first responder (hardware keys via `pressesBegan`, HID usage →
  VK, `ios_keymap`); the soft keyboard is shown by swapping an empty input view; return
  presses Enter as iOS's `textViewShouldReturn`. ❓ (hand checklist)
- Skins: `PHPickerViewController` → PNG in the game's temp dir → `image_picked`. ❓
- The game's home is `Documents` (as the original iOS game); `UIFileSharingEnabled` and
  `LSSupportsOpeningDocumentsInPlace` show worlds in the Files app. ❓
- Loading takes a second or two: a splash (the game's name and a spinner) covers the view until
  the first frame; the game starts one run loop turn after it is on screen. ✅

## Signing (free Apple ID)
- Xcode makes a 7-day profile for `io.github.d4yvid.mcfm.ios` once a project with that bundle id
  has the Personal Team chosen (`com.mojang.*` cannot be registered by a free team). The build
  signs every dylib in `Frameworks/`, then the app with the profile's entitlements
  (`ios/tools/signing.sh`). The first launch needs the developer trusted on the device. ✅
- `xcodebuild` cannot pick the team by itself (no cached team id until Xcode used it once); the
  team id is read from the project after the owner chooses it.
- A hidden failed install showed up as "invalid code signature … not trusted" at launch;
  reinstalling fixed it.
