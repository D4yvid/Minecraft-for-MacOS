# AppPlatform vtable map (Minecraft PE 0.15.10)

Generated from the Android `libminecraftpe.so` (which keeps symbol names) and the stripped iOS
binary (IDA). The base-class part of the vtable has the same order on both, so Android names label
the iOS slots; this was cross-checked on the five slots the mod patches (13, 14, 93, 96, 99).

- iOS `AppPlatform_iOS` vtable (vptr value): `0x100EABE00` · base `AppPlatform`: `0x100E649C0`
- iOS singleton: `AppPlatform*` at `0x100F5E850` (all addresses unslid, image base `0x100000000`)
- Byte offset on iOS (arm64) = slot × 8; on Android (armv7) = slot × 4 from the vptr
  (the `_ZTV` symbol starts 2 words earlier: offset-to-top, typeinfo).
- **iOS override**: ✓ = AppPlatform_iOS replaces the base implementation. **iOS body**: one-line
  decompilation for trivial functions, otherwise size. Slots 101–102 exist only on iOS (names unknown).

| Slot | iOS off | Name (Android) | Android23 override | iOS fn | iOS override | iOS body |
|---:|---:|---|---|---|:-:|---|
| 0 | 0 | `AppPlatform::~AppPlatform()` | `AppPlatform_android::~AppPlatform_android()` | `0x100705988` | ✓ | return sub_1007058D4(); } |
| 1 | 8 | `AppPlatform::~AppPlatform()` | `AppPlatform_android23::~AppPlatform_android23()` | `0x10070598C` | ✓ | (20 bytes) |
| 2 | 16 | `__cxa_pure_virtual` | `AppPlatform_android::getDataUrl() const` | `0x1007059B0` | ✓ | (164 bytes) |
| 3 | 24 | `AppPlatform::getAlternateDataUrl() const` |  | `0x100460E14` |  | return (*(__int64 (__fastcall **)(__int64))(*(_QWORD *)a1 + 16LL))(a1); } |
| 4 | 32 | `__cxa_pure_virtual` | `AppPlatform_android::getPackagePath()` | `0x100705A54` | ✓ | (164 bytes) |
| 5 | 40 | `AppPlatform::loadPNG(TextureData&, std::string const&)` | `AppPlatform_android::loadPNG(TextureData&, std::string const&)` | `0x100705AF8` | ✓ | (668 bytes) |
| 6 | 48 | `AppPlatform::loadTGA(TextureData&, std::string const&)` | `AppPlatform_android::loadTGA(TextureData&, std::string const&)` | `0x10045F8A8` |  | return sub_10048B138(a2, a3); } |
| 7 | 56 | `AppPlatform::loadJPEG(TextureData&, std::string const&)` | `AppPlatform_android::loadJPEG(TextureData&, std::string const&)` | `0x10045F8B4` |  | return sub_10048B138(a2, a3); } |
| 8 | 64 | `AppPlatform::getKeyFromKeyCode(int, int, int)` | `AppPlatform_android::getKeyFromKeyCode(int, int, int)` | `0x100460E20` |  | return 0; } |
| 9 | 72 | `AppPlatform::showKeyboard(std::string const&, int, bool, bool, Vec2 const&)` | `AppPlatform_android::showKeyboard(std::string const&, int, bool, bool, Vec2 const&)` | `0x10070654C` | ✓ | (148 bytes) |
| 10 | 80 | `AppPlatform::hideKeyboard()` | `AppPlatform_android::hideKeyboard()` | `0x1007065E0` | ✓ | (48 bytes) |
| 11 | 88 | `AppPlatform::isFullScreenKeyboard() const` | `AppPlatform_android::isFullScreenKeyboard() const` | `0x100460E28` |  | return 0; } |
| 12 | 96 | `AppPlatform::getKeyboardHeight() const` | `AppPlatform_android::getKeyboardHeight() const` | `0x100706610` | ✓ | return objc_msgSend(*(id *)(a1 + 504), "getKeyboardHeight"); } |
| 13 | 104 | `AppPlatform::hideMousePointer()` |  | `0x100460E38` |  | ; } |
| 14 | 112 | `AppPlatform::showMousePointer()` |  | `0x100460E3C` |  | ; } |
| 15 | 120 | `AppPlatform::getPointerFocus()` |  | `0x100460E40` |  | return *(unsigned __int8 *)(a1 + 8); } |
| 16 | 128 | `AppPlatform::setPointerFocus(bool)` |  | `0x100460E48` |  | (8 bytes) |
| 17 | 136 | `AppPlatform::toggleSimulateTouchWithMouse()` |  | `0x100460E50` |  | ; } |
| 18 | 144 | `__cxa_pure_virtual` | `AppPlatform_android::swapBuffers()` | `0x1007059A0` | ✓ | return objc_msgSend(*(id *)(a1 + 504), "swapBuffers"); } |
| 19 | 152 | `AppPlatform::discardBackbuffer()` |  | `0x100460E54` |  | ; } |
| 20 | 160 | `__cxa_pure_virtual` | `AppPlatform_android::getSystemRegion() const` | `0x1007074FC` | ✓ | return a1 + 384; } |
| 21 | 168 | `__cxa_pure_virtual` | `AppPlatform_android::getGraphicsVendor()` | `0x100706F60` | ✓ | return sub_10003A850(); } |
| 22 | 176 | `__cxa_pure_virtual` | `AppPlatform_android::getGraphicsRenderer()` | `0x100706F64` | ✓ | return sub_10003A8AC(); } |
| 23 | 184 | `__cxa_pure_virtual` | `AppPlatform_android::getGraphicsVersion()` | `0x100706F68` | ✓ | return sub_10003A680(); } |
| 24 | 192 | `__cxa_pure_virtual` | `AppPlatform_android::getGraphicsExtensions()` | `0x100706F6C` | ✓ | return sub_10003A908(); } |
| 25 | 200 | `__cxa_pure_virtual` | `AppPlatform_android::pickImage(ImagePickingCallback&)` | `0x100706F70` | ✓ | return objc_msgSend(*(id *)(a1 + 504), "pickSkinImage:", a2); } |
| 26 | 208 | `AppPlatform::pickFile(FilePickerSettings&)` |  | `0x100460E58` |  | ; } |
| 27 | 216 | `AppPlatform::supportsFilePicking() const` |  | `0x100460E5C` |  | return 0; } |
| 28 | 224 | `AppPlatform::pushNotificationReceived(PushNotificationMessage const&)` |  | `0x100460408` |  | (124 bytes) |
| 29 | 232 | `AppPlatform::createHolographicPlatform() const` | `AppPlatform_android::createHolographicPlatform() const` | `0x10046057C` |  | *a1 = 0; } |
| 30 | 240 | `AppPlatform::createAndroidLaunchIntent()` | `AppPlatform_android::createAndroidLaunchIntent()` | `0x100460E64` |  | return 0; } |
| 31 | 248 | `AppPlatform::updateLocalization(std::string const&)` | `AppPlatform_android::updateLocalization(std::string const&)` | `0x100460598` |  | ; } |
| 32 | 256 | `AppPlatform::setSleepEnabled(bool)` |  | `0x100706A48` | ✓ | (60 bytes) |
| 33 | 264 | `__cxa_pure_virtual` | `AppPlatform_android::getExternalStoragePath()` | `0x100706B00` | ✓ | return a1 + 408; } |
| 34 | 272 | `__cxa_pure_virtual` | `AppPlatform_android::getInternalStoragePath()` | `0x100707504` | ✓ | return a1 + 456; } |
| 35 | 280 | `__cxa_pure_virtual` | `AppPlatform_android::getUserdataPath()` | `0x10070750C` | ✓ | return a1 + 480; } |
| 36 | 288 | `AppPlatform::getUserdataPathForLevels()` | `AppPlatform_android::getUserdataPathForLevels()` | `0x100460E70` |  | return (*(__int64 (__fastcall **)(__int64))(*(_QWORD *)a1 + 280LL))(a1); } |
| 37 | 296 | `AppPlatform::getApiEnvironmentPath()` |  | `0x100460E8C` |  | return (*(__int64 (__fastcall **)(__int64))(*(_QWORD *)a1 + 280LL))(a1); } |
| 38 | 304 | `AppPlatform::showDialog(int)` | `AppPlatform_android::showDialog(int)` | `0x100460EA8` |  | ; } |
| 39 | 312 | `AppPlatform::createUserInput()` | `AppPlatform_android::createUserInput()` | `0x100460EAC` |  | ; } |
| 40 | 320 | `AppPlatform::getUserInputStatus()` | `AppPlatform_android::getUserInputStatus()` | `0x100706188` | ✓ | return objc_msgSend(*(id *)(a1 + 504), "getUserInputStatus"); } |
| 41 | 328 | `AppPlatform::getUserInput()` | `AppPlatform_android::getUserInput()` | `0x100706198` | ✓ | (32 bytes) |
| 42 | 336 | `AppPlatform::_tick()` |  | `0x100460EC4` |  | ; } |
| 43 | 344 | `AppPlatform::getScreenWidth()` | `AppPlatform_android::getScreenWidth()` | `0x1007060B8` | ✓ | return objc_msgSend(*(id *)(a1 + 504), "width"); } |
| 44 | 352 | `AppPlatform::getScreenHeight()` | `AppPlatform_android::getScreenHeight()` | `0x1007060C8` | ✓ | return objc_msgSend(*(id *)(a1 + 504), "height"); } |
| 45 | 360 | `AppPlatform::setScreenSize(int, int)` |  | `0x100460ED8` |  | ; } |
| 46 | 368 | `AppPlatform::setWindowSize(int, int)` |  | `0x100460EDC` |  | ; } |
| 47 | 376 | `AppPlatform::setWindowText(std::string const&)` |  | `0x100460EE0` |  | ; } |
| 48 | 384 | `AppPlatform::getPixelsPerMillimeter()` | `AppPlatform_android::getPixelsPerMillimeter()` | `0x1007060D8` | ✓ | (164 bytes) |
| 49 | 392 | `AppPlatform::updateTextBoxText(std::string const&)` | `AppPlatform_android::updateTextBoxText(std::string const&)` | `0x100706620` | ✓ | return objc_msgSend(*(id *)(a1 + 504), "updateTextView:", a2); } |
| 50 | 400 | `AppPlatform::isKeyboardVisible()` |  | `0x100460EF0` |  | return *(unsigned __int8 *)(a1 + 9); } |
| 51 | 408 | `AppPlatform::supportsVibration()` | `AppPlatform_android::supportsVibration()` | `0x100707514` | ✓ | return *(unsigned __int8 *)(a1 + 520); } |
| 52 | 416 | `AppPlatform::vibrate(int)` | `AppPlatform_android::vibrate(int)` | `0x100706184` | ✓ | ; } |
| 53 | 424 | `AppPlatform::getAssetFileFullPath(std::string const&)` |  | `0x100705EC0` | ✓ | (192 bytes) |
| 54 | 432 | `AppPlatform::readAssetFile(std::string const&)` | `AppPlatform_android23::readAssetFile(std::string const&)` | `0x100705DA8` | ✓ | (280 bytes) |
| 55 | 440 | `AppPlatform::readAssetFileZipped(std::string const&, std::string const&)` |  | `0x10046067C` |  | (596 bytes) |
| 56 | 448 | `AppPlatform::listAssetFilesIn(std::string const&, std::string const&) const` |  | `0x10046094C` |  | (236 bytes) |
| 57 | 456 | `AppPlatform::getDateString(int)` | `AppPlatform_android::getDateString(int)` | `0x100705FDC` | ✓ | (220 bytes) |
| 58 | 464 | `AppPlatform::checkLicense()` | `AppPlatform_android::checkLicense()` | `0x10070751C` | ✓ | return 0; } |
| 59 | 472 | `AppPlatform::hasBuyButtonWhenInvalidLicense()` | `AppPlatform_android::hasBuyButtonWhenInvalidLicense()` | `0x100460F30` |  | return 0; } |
| 60 | 480 | `AppPlatform::uploadPlatformDependentData(int, void*)` | `AppPlatform_android::uploadPlatformDependentData(int, void*)` | `0x100460F38` |  | ; } |
| 61 | 488 | `AppPlatform::isNetworkEnabled(bool)` | `AppPlatform_android::isNetworkEnabled(bool)` | `0x100706504` | ✓ | (72 bytes) |
| 62 | 496 | `AppPlatform::isPowerVR()` | `AppPlatform_android::isPowerVR()` | `0x1007066B4` | ✓ | (60 bytes) |
| 63 | 504 | `AppPlatform::buyGame()` | `AppPlatform_android::buyGame()` | `0x100705F80` | ✓ | (92 bytes) |
| 64 | 512 | `AppPlatform::finish()` | `AppPlatform_android::finish()` | `0x100460F48` |  | ; } |
| 65 | 520 | `AppPlatform::launchUri(std::string const&)` | `AppPlatform_android::launchUri(std::string const&)` | `0x100706638` | ✓ | (124 bytes) |
| 66 | 528 | `AppPlatform::useMetadataDrivenScreens() const` | `AppPlatform_android::useMetadataDrivenScreens() const` | `0x100460A44` |  | return 0; } |
| 67 | 536 | `AppPlatform::useXboxControlHelpers() const` |  | `0x100460A4C` |  | return 0; } |
| 68 | 544 | `AppPlatform::useCenteredGUI() const` |  | `0x100460A54` |  | return 0; } |
| 69 | 552 | `AppPlatform::getPlatformType() const` |  | `0x100460A5C` |  | return 1; } |
| 70 | 560 | `AppPlatform::setControllerType(ControllerType)` |  | `0x100460A90` |  | (8 bytes) |
| 71 | 568 | `AppPlatform::getControllerType() const` |  | `0x100460A98` |  | return *(unsigned int *)(a1 + 12); } |
| 72 | 576 | `AppPlatform::hasIDEProfiler()` |  | `0x100707524` | ✓ | return *(unsigned __int8 *)(a1 + 521); } |
| 73 | 584 | `AppPlatform::getPlatformStringVar(int)` | `AppPlatform_android::getPlatformStringVar(int)` | `0x100460F54` |  | (28 bytes) |
| 74 | 592 | `__cxa_pure_virtual` | `AppPlatform_android::getApplicationId()` | `0x10070752C` | ✓ | (28 bytes) |
| 75 | 600 | `AppPlatform::getAvailableMemory()` | `AppPlatform_android::getAvailableMemory()` | `0x100706A84` | ✓ | (124 bytes) |
| 76 | 608 | `AppPlatform::getTotalMemory()` |  | `0x100460F78` |  | return 0; } |
| 77 | 616 | `AppPlatform::getBroadcastAddresses()` | `AppPlatform_android::getBroadcastAddresses()` | `0x1007066F0` | ✓ | (428 bytes) |
| 78 | 624 | `AppPlatform::getIPAddresses()` | `AppPlatform_android::getIPAddresses()` | `0x10070689C` | ✓ | (428 bytes) |
| 79 | 632 | `AppPlatform::getModelName()` | `AppPlatform_android::getModelName()` | `0x100460F80` |  | return std::string::basic_string(a2, (const std::string *)(a1 + 16)); } |
| 80 | 640 | `__cxa_pure_virtual` | `AppPlatform_android::getDeviceId()` | `0x100706B10` | ✓ | (416 bytes) |
| 81 | 648 | `__cxa_pure_virtual` | `AppPlatform_android::createUUID()` | `0x100706CC8` | ✓ | (296 bytes) |
| 82 | 656 | `__cxa_pure_virtual` | `AppPlatform_android::isFirstSnoopLaunch()` | `0x100706E04` | ✓ | return *(unsigned __int8 *)(a1 + 522); } |
| 83 | 664 | `__cxa_pure_virtual` | `AppPlatform_android::hasHardwareInformationChanged()` | `0x100706E0C` | ✓ | (220 bytes) |
| 84 | 672 | `__cxa_pure_virtual` | `AppPlatform_android::isTablet()` | `0x100706EE8` | ✓ | (120 bytes) |
| 85 | 680 | `AppPlatform::registerUriListener(UriListener&)` |  | `0x10045FB64` |  | (184 bytes) |
| 86 | 688 | `AppPlatform::registerUriListener(std::string const&, UriListener&)` |  | `0x10045FC48` |  | (316 bytes) |
| 87 | 696 | `AppPlatform::unregisterUriListener(UriListener const&)` |  | `0x10045FDD8` |  | (316 bytes) |
| 88 | 704 | `AppPlatform::notifyUriListeners(ActivationUri const&)` |  | `0x10045FF14` |  | (124 bytes) |
| 89 | 712 | `AppPlatform::notifyUriListenerRegistrationDone()` |  | `0x1004600BC` |  | (340 bytes) |
| 90 | 720 | `AppPlatform::setFullscreenMode(FullscreenMode)` |  | `0x100460F8C` |  | ; } |
| 91 | 728 | `AppPlatform::isNetworkThrottled()` |  | `0x100460F90` |  | return (*(unsigned int (__fastcall **)(__int64, __int64))(*(_QWORD *)a1 + 488LL))(a1, 1) ^ |
| 92 | 736 | `AppPlatform::collectGraphicsHardwareDetails()` |  | `0x100460AB8` |  | ; } |
| 93 | 744 | `AppPlatform::getEdition() const` |  | `0x100460ABC` |  | (28 bytes) |
| 94 | 752 | `AppPlatform::realmsBeta() const` | `AppPlatform_android::realmsBeta() const` | `0x100707548` | ✓ | return 1; } |
| 95 | 760 | `AppPlatform::isFireTV() const` | `AppPlatform_android::isFireTV() const` | `0x100460FBC` |  | return 0; } |
| 96 | 768 | `AppPlatform::getDefaultInputMode() const` | `AppPlatform_android::getDefaultInputMode() const` | `0x1007074F4` | ✓ | return 2; } |
| 97 | 776 | `AppPlatform::_notifyUriListeners(ActivationUri const&, bool)` |  | `0x10045FF90` |  | (276 bytes) |
| 98 | 784 | `AppPlatform::getPlatformDpi() const` |  | `0x100706F88` | ✓ | (1220 bytes) |
| 99 | 792 | `AppPlatform::getPlatformUIScalingRules() const` | `AppPlatform_android::getPlatformUIScalingRules() const` | `0x1007074EC` | ✓ | return 1; } |
| 100 | 800 | `__cxa_pure_virtual` | `AppPlatform_android::getPlatformTempPath()` | `0x100706B08` | ✓ | return a1 + 432; } |
| 101 | 808 | `(iOS-only)` | `AppPlatform_android23::initWithActivity(ANativeActivity*) (Android-only)` | `0x10070617C` | ✓ | return 1; } |
| 102 | 816 | `(iOS-only)` |  | `0x1007061B8` | ✓ | (844 bytes) |
