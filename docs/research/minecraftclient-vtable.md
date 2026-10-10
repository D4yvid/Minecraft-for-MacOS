# MinecraftClient / App vtable (Minecraft PE 0.15.10)

Names from the Android `libminecraftpe.so` (`_ZTV15MinecraftClient`, 66 slots). On iOS the object is
`minecraftpeViewController->_app` (a `MinecraftClient`); its vtable address is not mapped yet, but the
slot order is the same (verified for 19, 20, 21, which the iOS view controller calls).

- `drawFrame` calls slot 19 `update()` once per CADisplayLink frame.
- `initView` (and the macOS resize fix) call slot 21 `setRenderingSize(w, h)` and slot 20
  `setUISizeAndScale(w, h, scale)`; the iOS code passes scale 0.

| Slot | iOS off | Android armv7 off | Function |
|---:|---:|---:|---|
| 0 | 0 | 0 | `MinecraftClient::~MinecraftClient()` |
| 1 | 8 | 4 | `MinecraftClient::~MinecraftClient()` |
| 2 | 16 | 8 | `MinecraftClient::onLowMemory()` |
| 3 | 24 | 12 | `MinecraftClient::onAppSuspended()` |
| 4 | 32 | 16 | `MinecraftClient::onAppResumed()` |
| 5 | 40 | 20 | `MinecraftClient::onAppFocusLost()` |
| 6 | 48 | 24 | `MinecraftClient::onAppFocusGained()` |
| 7 | 56 | 28 | `AppPlatformListener::onAppTerminated()` |
| 8 | 64 | 32 | `MinecraftClient::onPushNotificationReceived(PushNotificationMessage const&)` |
| 9 | 72 | 36 | `MinecraftClient::audioEngineOn()` |
| 10 | 80 | 40 | `MinecraftClient::audioEngineOff()` |
| 11 | 88 | 44 | `MinecraftClient::muteAudio()` |
| 12 | 96 | 48 | `MinecraftClient::unMuteAudio()` |
| 13 | 104 | 52 | `App::destroy()` |
| 14 | 112 | 56 | `App::loadState(void*, int)` |
| 15 | 120 | 60 | `App::saveState(void**, int*)` |
| 16 | 128 | 64 | `MinecraftClient::useTouchscreen()` |
| 17 | 136 | 68 | `MinecraftClient::setTextboxText(std::string const&)` |
| 18 | 144 | 72 | `App::draw()` |
| 19 | 152 | 76 | `MinecraftClient::update()` |
| 20 | 160 | 80 | `MinecraftClient::setUISizeAndScale(int, int, float)` |
| 21 | 168 | 84 | `MinecraftClient::setRenderingSize(int, int)` |
| 22 | 176 | 88 | `App::quit()` |
| 23 | 184 | 92 | `App::wantToQuit()` |
| 24 | 192 | 96 | `MinecraftClient::init()` |
| 25 | 200 | 100 | `MinecraftClient::handleBack(bool)` |
| 26 | 208 | 104 | `MinecraftClient::onInternetUpdate()` |
| 27 | 216 | 108 | `MinecraftClient::canActivateKeyboard()` |
| 28 | 224 | 112 | `MinecraftClient::onLevelCorrupt()` |
| 29 | 232 | 116 | `MinecraftClient::onGameModeChanged()` |
| 30 | 240 | 120 | `MinecraftClient::onGameSessionReset()` |
| 31 | 248 | 124 | `MinecraftClient::createSkin()` |
| 32 | 256 | 128 | `MinecraftClient::onLevelExit()` |
| 33 | 264 | 132 | `MinecraftClient::onTick(int, int)` |
| 34 | 272 | 136 | `MinecraftClient::vibrate(int)` |
| 35 | 280 | 140 | `0xfffffff0` |
| 36 | 288 | 144 | `__cxa_finalize` |
| 37 | 296 | 148 | `non-virtual thunk to MinecraftClient::~MinecraftClient()` |
| 38 | 304 | 152 | `non-virtual thunk to MinecraftClient::~MinecraftClient()` |
| 39 | 312 | 156 | `non-virtual thunk to MinecraftClient::vibrate(int)` |
| 40 | 320 | 160 | `0xffffffec` |
| 41 | 328 | 164 | `__cxa_finalize` |
| 42 | 336 | 168 | `non-virtual thunk to MinecraftClient::~MinecraftClient()` |
| 43 | 344 | 172 | `non-virtual thunk to MinecraftClient::~MinecraftClient()` |
| 44 | 352 | 176 | `non-virtual thunk to MinecraftClient::onLevelCorrupt()` |
| 45 | 360 | 180 | `non-virtual thunk to MinecraftClient::onGameModeChanged()` |
| 46 | 368 | 184 | `non-virtual thunk to MinecraftClient::onTick(int, int)` |
| 47 | 376 | 188 | `non-virtual thunk to MinecraftClient::onInternetUpdate()` |
| 48 | 384 | 192 | `non-virtual thunk to MinecraftClient::onGameSessionReset()` |
| 49 | 392 | 196 | `non-virtual thunk to MinecraftClient::onLevelExit()` |
| 50 | 400 | 200 | `0xffffffe8` |
| 51 | 408 | 204 | `__cxa_finalize` |
| 52 | 416 | 208 | `non-virtual thunk to MinecraftClient::~MinecraftClient()` |
| 53 | 424 | 212 | `non-virtual thunk to MinecraftClient::~MinecraftClient()` |
| 54 | 432 | 216 | `non-virtual thunk to MinecraftClient::createSkin()` |
| 55 | 440 | 220 | `0xffffffe4` |
| 56 | 448 | 224 | `__cxa_finalize` |
| 57 | 456 | 228 | `non-virtual thunk to MinecraftClient::~MinecraftClient()` |
| 58 | 464 | 232 | `non-virtual thunk to MinecraftClient::~MinecraftClient()` |
| 59 | 472 | 236 | `MinecraftKeyboardManager::enableKeyboard(std::string const&, int, bool, bool, Vec2 const&)` |
| 60 | 480 | 240 | `MinecraftKeyboardManager::isFullScreenKeyboard() const` |
| 61 | 488 | 244 | `MinecraftKeyboardManager::disableKeyboard()` |
| 62 | 496 | 248 | `non-virtual thunk to MinecraftClient::canActivateKeyboard()` |
| 63 | 504 | 252 | `MinecraftKeyboardManager::isKeyboardEnabled() const` |
| 64 | 512 | 256 | `MinecraftKeyboardManager::isKeyboardActive() const` |
| 65 | 520 | 260 | `MinecraftKeyboardManager::getKeyboardHeight() const` |
