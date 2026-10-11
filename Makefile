# Minecraft for macOS / iOS / Android — mods for Minecraft PE 0.15.10.
# Local paths (your own game files) go in config.mk; see config.example.mk.
-include config.mk

BUILD   ?= build
# Your own game files (make game-files), used when config.mk doesn't say otherwise.
GAME_FILES := $(CURDIR)/game-files
GAME    ?= $(wildcard $(GAME_FILES)/ios/Payload/minecraftpe2.app)
OUT_APP ?= $(CURDIR)/dist/minecraftpe.app

SHARED_INC   := -Ishared/include -Ishared/apple
SHARED_CORE  := shared/src/platform.cpp shared/src/win10_ui.cpp shared/src/keyboard_mouse.cpp
SHARED_HEADERS := $(wildcard shared/include/mcfm/*.h shared/include/mcfm/*/*.h shared/apple/*.h)
SHARED_TESTS := keymap_test input_state_test features_test launcher_app_platform_test launcher_engine_test hook_table_test macho_uuid_bounds_test launcher_uuid_race_test launcher_platform_test launcher_text_test macho_file_test macho_fixups_test
MACOS_TESTS  := titlebar_test input_policy_test resize_math_test mac_keymap_test mouse_math_test image_pick_test

# ---------------------------------------------------------------- macOS (Mac Catalyst) — DEPRECATED
# The Catalyst build is a deprecated build mode, kept working: catalyst, catalyst-run,
# catalyst-check. make app/run/check build and run the Mach-O launcher (docs/LAUNCHER.md).
CATALYST_DEPRECATED = @echo "mcfm: note: the Mac Catalyst build is deprecated; make app/run/check use the Mach-O launcher (docs/LAUNCHER.md)" >&2
SDK      := $(shell xcrun --sdk macosx --show-sdk-path)
IOSFW    := $(SDK)/System/iOSSupport/System/Library/Frameworks
MAC_TARGET := arm64-apple-ios15.0-macabi
MAC_DYLIB  := $(BUILD)/macos/libmcfm.dylib
APPLE_SRCS := shared/apple/macho_uuid.cpp shared/apple/address_platform.mm shared/apple/store.mm
MAC_SRCS := $(SHARED_CORE) shared/src/keymap.cpp $(APPLE_SRCS) $(wildcard macos/src/*.mm)
MAC_CXXFLAGS := -target $(MAC_TARGET) -isysroot $(SDK) -iframework $(IOSFW) $(SHARED_INC) \
                -std=c++17 -fobjc-arc -O2 -Wall -Wextra -Wno-unused-parameter
MAC_LDFLAGS  := -dynamiclib -F$(IOSFW) -framework Foundation -framework UIKit \
                -framework GameController -framework QuartzCore \
                -install_name @executable_path/Frameworks/libmcfm.dylib

.PHONY: all macos app run check catalyst catalyst-run catalyst-check test clean ios ios-ipa ios-syntax android android-apk game-files
all: macos

macos: $(MAC_DYLIB)

$(MAC_DYLIB): $(MAC_SRCS) $(SHARED_HEADERS) $(wildcard macos/src/*.h)
	@mkdir -p $(dir $@)
	clang++ $(MAC_CXXFLAGS) $(MAC_LDFLAGS) $(MAC_SRCS) -o $@

catalyst: $(MAC_DYLIB)
	$(CATALYST_DEPRECATED)
	@test -n "$(GAME)" || { echo "Set GAME=<your decrypted minecraftpe2.app> (or put it in config.mk)"; exit 1; }
	bash macos/tools/convert.sh "$(GAME)" "$(OUT_APP)" "$(MAC_DYLIB)"

catalyst-run:
	$(CATALYST_DEPRECATED)
	open "$(OUT_APP)"


# ---------------------------------------------------------------- iOS
IOS_SDK    := $(shell xcrun --sdk iphoneos --show-sdk-path 2>/dev/null)
IOS_TARGET := arm64-apple-ios15.0
IOS_DYLIB  := $(BUILD)/ios/libmcfm.dylib
IOS_IPA    ?= $(CURDIR)/dist/minecraftpe-mcfm.ipa
IOS_SRCS   := $(SHARED_CORE) $(APPLE_SRCS) ios/src/main.mm
IOS_CXXFLAGS := -target $(IOS_TARGET) $(SHARED_INC) -std=c++17 -fobjc-arc -O2 -Wall -Wextra -Wno-unused-parameter

ios: $(IOS_DYLIB)

$(IOS_DYLIB): $(IOS_SRCS) $(SHARED_HEADERS)
	@test -n "$(IOS_SDK)" || { echo "The iOS build needs the iPhoneOS SDK: install Xcode, then run xcode-select -s /Applications/Xcode.app"; exit 1; }
	@mkdir -p $(dir $@)
	clang++ $(IOS_CXXFLAGS) -isysroot $(IOS_SDK) -dynamiclib -framework Foundation \
	  -install_name @executable_path/Frameworks/libmcfm.dylib $(IOS_SRCS) -o $@

ios-ipa: $(IOS_DYLIB)
	@test -n "$(GAME)" || { echo "Set GAME=<your decrypted Minecraft .app or .ipa> (or put it in config.mk)"; exit 1; }
	bash ios/tools/make_ipa.sh "$(GAME)" "$(IOS_IPA)" "$(IOS_DYLIB)"

# Compiles the iOS sources for the iOS target without linking (works without Xcode).
ios-syntax:
	clang++ $(IOS_CXXFLAGS) -isysroot $(SDK) -Wno-incompatible-sysroot -fsyntax-only $(IOS_SRCS)

# ---------------------------------------------------------------- Mach-O launcher (docs/LAUNCHER.md)
LAUNCHER_BIN := $(BUILD)/launcher/mcfm-launch
# Which Mach-O loader runs the game: own (shared/loader, Stage 2; default) or dyld (Apple's).
LOADER ?= own
# Never rebuild dist/launcher under a running game or start a second one on the same worlds.
LAUNCHER_NOT_RUNNING = @! pgrep -x mcfm-launch >/dev/null || { echo "The launcher game is running (mcfm-launch): quit it first"; exit 1; }
AUDIO_PROVIDER := $(BUILD)/launcher/libmcfm_audiotoolbox.dylib
LAUNCHER_SRCS := macos/launcher/main.mm shared/apple/macho_uuid.cpp shared/apple/hook_table.cpp \
                 shared/launcher/app_platform.cpp shared/launcher/engine.cpp shared/launcher/seams.cpp shared/launcher/text_input.cpp \
                 shared/launcher/launcher_platform.cpp macos/launcher/input.mm macos/launcher/mac_keymap.cpp \
                 shared/src/keyboard_mouse.cpp shared/src/platform.cpp shared/src/keymap.cpp \
                 shared/loader/macho_file.cpp shared/loader/fixups.cpp shared/loader/loader.cpp macos/launcher/loader_macos.cpp macos/launcher/image_pick.mm
LAUNCHER_CXXFLAGS := -arch arm64 -mmacosx-version-min=11.0 -std=c++17 -fobjc-arc -O2 -Wall -Wextra \
                     -Wno-unused-parameter -Ishared/apple -Ishared/launcher -Ishared/loader -Imacos/launcher $(SHARED_INC)

$(LAUNCHER_BIN): $(LAUNCHER_SRCS) $(SHARED_HEADERS) $(wildcard shared/launcher/*.h shared/loader/*.h macos/launcher/*.h)
	@mkdir -p $(dir $@)
	clang++ $(LAUNCHER_CXXFLAGS) $(LAUNCHER_SRCS) -framework AppKit -framework QuartzCore -framework GameController \
	  -framework ImageIO -framework UniformTypeIdentifiers \
	  -Wl,-rpath,@executable_path -o $@

LAUNCHER_OUT ?= $(CURDIR)/dist/launcher
ANGLE_DIR ?= $(CURDIR)/$(BUILD)/angle

.PHONY: angle
# Downloads ANGLE once (pinned Electron release, ~130 MB); see tools/launcher/fetch_angle.sh.
angle:
	bash tools/launcher/fetch_angle.sh "$(ANGLE_DIR)"

.PHONY: launcher launcher-check
launcher: $(LAUNCHER_BIN) $(AUDIO_PROVIDER)
	$(LAUNCHER_NOT_RUNNING)
	@test -n "$(GAME)" || { echo "Set GAME=<your decrypted minecraftpe2.app> (or put it in config.mk)"; exit 1; }
	@test -f "$(ANGLE_DIR)/libGLESv2.dylib" -a -f "$(ANGLE_DIR)/libEGL.dylib" || { echo "Run make angle first (downloads ANGLE)"; exit 1; }
	bash macos/tools/make_launcher.sh "$(GAME)" "$(LAUNCHER_OUT)" "$(LAUNCHER_BIN)" "$(ANGLE_DIR)" "$(AUDIO_PROVIDER)"

# Loads the image built by make launcher; the census lists every stub the game called.
launcher-check:
	$(LAUNCHER_NOT_RUNNING)
	@rm -f $(BUILD)/launcher/census.txt; mkdir -p $(BUILD)/launcher
	@OUT="$$(MCFM_CENSUS="$(CURDIR)/$(BUILD)/launcher/census.txt" "$(LAUNCHER_OUT)/mcfm-launch" --loader $(LOADER) --frames 120 2>&1)"; RC=$$?; \
	  echo "$$OUT" | grep -E "^mcfm: (game image|EGL|engine|[0-9]+ frames)" ; \
	  { [ $$RC = 0 ] && echo "$$OUT" | grep -q "game image loaded" && echo "$$OUT" | grep -q "120 frames rendered"; } \
	    || { echo "$$OUT" | tail -25; echo "launcher-check: FAILED (exit $$RC)"; exit 1; }
	@grep -qxF "libobjc:_objc_autoreleasePoolPush" $(BUILD)/launcher/census.txt || { echo "launcher-check: initializers did not reach the stubs"; exit 1; }
	@echo "launcher-check: passed ($$(wc -l < $(BUILD)/launcher/census.txt | tr -d ' ') stubs called, see $(BUILD)/launcher/census.txt)"

# Our loader vs dyld on the game image built by make app: every fixup location must match.
# Our loader (Stage 2); defined before the first rule that lists them as prerequisites.
LOADER_SRCS := shared/loader/macho_file.cpp shared/loader/fixups.cpp shared/loader/loader.cpp shared/apple/hook_table.cpp
LOADER_HDRS := $(wildcard shared/loader/*.h) shared/apple/hook_table.h
LOADER_CHECK := $(BUILD)/launcher/loader-check
$(LOADER_CHECK): macos/tools/loader_check.cpp macos/launcher/loader_macos.cpp macos/launcher/loader_macos.h $(LOADER_SRCS) $(LOADER_HDRS)
	@mkdir -p $(dir $@)
	clang++ -arch arm64 -std=c++17 -Wall -O1 -Ishared/loader -Ishared/apple -Imacos/launcher macos/tools/loader_check.cpp \
	  macos/launcher/loader_macos.cpp $(LOADER_SRCS) -Wl,-rpath,"$(LAUNCHER_OUT)" -o $@
.PHONY: loader-check
loader-check: $(LOADER_CHECK)
	"$(LOADER_CHECK)" "$(LAUNCHER_OUT)/libminecraftpe.dylib"

.PHONY: launcher-run
launcher-run:
	$(LAUNCHER_NOT_RUNNING)
	"$(LAUNCHER_OUT)/mcfm-launch" --loader $(LOADER)

# The default macOS build is the launcher (Stage 1, docs/LAUNCHER.md).
app: launcher
run: launcher-run
check: launcher-check

# ---------------------------------------------------------------- Android
# NDK r10c (x86_64 host build; runs under Rosetta on Apple Silicon).
NDK ?= $(HOME)/Library/Android/ndk/android-ndk-r10c
RELEASE_BUILD ?= 0
ANDROID_OUT := $(CURDIR)/$(BUILD)/android
ANDROID_LIB := $(ANDROID_OUT)/libs/armeabi-v7a/libmcfm.so
HOST_X86 := $(if $(filter arm64,$(shell uname -m)),/usr/bin/arch -x86_64,)

android:
	@test -x "$(NDK)/ndk-build" || { echo "Set NDK=<path to android-ndk-r10c> (or put it in config.mk)"; exit 1; }
	cd android && $(HOST_X86) /bin/bash -c '"$(NDK)/ndk-build" -j8 NDK_PROJECT_PATH=. \
	  NDK_OUT="$(ANDROID_OUT)/obj" NDK_LIBS_OUT="$(ANDROID_OUT)/libs" RELEASE_BUILD=$(RELEASE_BUILD)'
	bash android/tests/lib_test.sh "$(ANDROID_LIB)" "$(NDK)"
	bash android/tests/pick_gnustl_test.sh "$(NDK)" "$(dir $(ANDROID_LIB))"

APK ?= $(firstword $(filter-out %-patched.apk,$(wildcard $(GAME_FILES)/android/apk/*.apk)))
ANDROID_APK ?= $(CURDIR)/dist/minecraftpe-mcfm.apk
android-apk: android
	@test -n "$(APK)" || { echo "Set APK=<your Minecraft PE 0.15.10 .apk> (or put it in config.mk)"; exit 1; }
	NDK="$(NDK)" bash android/tools/build_apk.sh "$(APK)" "$(dir $(ANDROID_LIB))" "$(ANDROID_APK)"

# ---------------------------------------------------------------- Android launcher (Stage 3)
# The iOS image on Android arm64 (docs/LAUNCHER.md): NDK r27d, target SDK 37, minimum API 28,
# 16 KB pages. `make android-sdk` downloads the toolchain once (~4 GB, pinned, checksummed).
ANDROID_SDK ?= $(HOME)/Library/Android/sdk
NDK64 ?= $(ANDROID_SDK)/ndk/27.3.13750724
NDK64_BIN := $(NDK64)/toolchains/llvm/prebuilt/darwin-x86_64/bin
ACC := $(NDK64_BIN)/aarch64-linux-android28-clang
ACXX := $(NDK64_BIN)/aarch64-linux-android28-clang++
ANDROID_LDFLAGS := -Wl,-z,max-page-size=16384
LLVM_RUNTIMES ?= $(CURDIR)/$(BUILD)/llvm-runtimes
API ?= 37

# The app (Stage 3c): JDK + Kotlin compiler, build-tools and the API 37 platform.
JVM_TOOLS ?= $(HOME)/Library/Android/jvm-tools
JAVA_HOME_APP := $(JVM_TOOLS)/jdk-21/Contents/Home
KOTLINC := $(JVM_TOOLS)/kotlinc/bin/kotlinc
BUILD_TOOLS := $(ANDROID_SDK)/build-tools/37.0.0
ANDROID_JAR := $(ANDROID_SDK)/platforms/android-37.0/android.jar
.PHONY: android-app-sdk
android-app-sdk: android-sdk
	bash tools/android/fetch_jvm_tools.sh "$(JVM_TOOLS)"

.PHONY: android-sdk llvm-runtimes android-emulator android-emulator-window android-emulator-stop darwin-abi
# Regenerates the Darwin ABI tables from the macOS SDK (committed; darwin_abi_test checks them).
darwin-abi:
	@mkdir -p $(BUILD)/tools
	clang -arch arm64 -Wall -Werror tools/android/darwin_abi_gen.c -o $(BUILD)/tools/darwin_abi_gen
	$(BUILD)/tools/darwin_abi_gen header > android/launcher/darwin/darwin_abi.h
	$(BUILD)/tools/darwin_abi_gen ctype > android/launcher/darwin/darwin_ctype.inc
	$(BUILD)/tools/darwin_abi_gen strerror > android/launcher/darwin/darwin_strerror.inc
android-sdk:
	bash tools/android/fetch_sdk.sh "$(ANDROID_SDK)"
llvm-runtimes:
	bash tools/android/fetch_llvm_runtimes.sh "$(LLVM_RUNTIMES)"
android-emulator:
	ANDROID_SDK="$(ANDROID_SDK)" bash tools/android/emulator.sh start $(API)
android-emulator-window:
	ANDROID_SDK="$(ANDROID_SDK)" bash tools/android/emulator.sh window $(API)
android-emulator-stop:
	ANDROID_SDK="$(ANDROID_SDK)" bash tools/android/emulator.sh stop

ALAUNCH_OUT := $(BUILD)/android-launcher
DARWIN_HDRS := $(wildcard android/launcher/darwin/*.h android/launcher/darwin/*.inc)
DARWIN_CXXFLAGS := -std=c++11 -Wall -Wextra -O1 -g -fsigned-char -Iandroid/launcher/darwin
DARWIN_PTHREAD_SRCS := android/launcher/darwin/pthread.cpp android/launcher/darwin/errno.cpp
$(ALAUNCH_OUT)/tests/pthread_test: android/launcher/tests/pthread_test.cpp $(DARWIN_PTHREAD_SRCS) $(DARWIN_HDRS)
	@test -x "$(ACXX)" || { echo "NDK r27d not found at $(NDK64) (make android-sdk)"; exit 1; }
	@mkdir -p $(dir $@)
	$(ACXX) $(DARWIN_CXXFLAGS) $(ANDROID_LDFLAGS) -static-libstdc++ $< $(DARWIN_PTHREAD_SRCS) -o $@

# libmcfm_runtime.a: Apple-ABI libc++/libc++abi/libunwind + the Darwin layer (Stage 3a), always
# linked whole with its symbols hidden (see android/launcher/runtime/build_runtime.sh).
RT_OUT := $(ALAUNCH_OUT)/runtime
RT_LIB := $(RT_OUT)/libmcfm_runtime.a
RT_DEPS := $(wildcard android/launcher/runtime/* android/launcher/runtime/include/* android/launcher/darwin/*)
RT_CXXFLAGS := -std=c++17 -Wall -Wextra -O1 -g -fsigned-char -nostdinc++ -isystem $(RT_OUT)/include \
  -isystem $(RT_OUT)/src/libcxx/include -isystem $(RT_OUT)/src/libcxxabi/include -Iandroid/launcher/darwin
RT_LDFLAGS := $(ANDROID_LDFLAGS) -nostdlib++ --unwindlib=none -Wl,--whole-archive $(RT_LIB) -Wl,--no-whole-archive \
  -Wl,--exclude-libs,ALL -ldl
.PHONY: android-runtime
android-runtime: $(RT_LIB)
$(RT_LIB): $(RT_DEPS)
	@test -d "$(LLVM_RUNTIMES)/libcxx" || { echo "LLVM runtimes not found in $(LLVM_RUNTIMES) (make llvm-runtimes)"; exit 1; }
	bash android/launcher/runtime/build_runtime.sh "$(LLVM_RUNTIMES)" "$(NDK64_BIN)" "$(RT_OUT)"
$(ALAUNCH_OUT)/tests/%_test: android/launcher/tests/%_test.cpp $(RT_LIB)
	@mkdir -p $(dir $@)
	$(ACXX) $(RT_CXXFLAGS) $< $(RT_LDFLAGS) -o $@

# libmcfm_launcher.so: the loader, the launcher and the whole runtime, exporting only its entry
# points (android/launcher/launcher.map). mcfm-run is a C program that dlopens it.
LAUNCHER_SO := $(ALAUNCH_OUT)/libmcfm_launcher.so
LAUNCHER_SO_SRCS := android/launcher/run.cpp android/launcher/boot.cpp android/launcher/app.cpp android/launcher/game_thread.cpp \
  android/launcher/input_events.cpp android/launcher/render_requests.cpp android/launcher/loader_android.cpp shared/launcher/launcher_platform.cpp \
  shared/src/keyboard_mouse.cpp shared/src/platform.cpp \
  android/launcher/audio_toolbox.cpp $(LOADER_SRCS) shared/apple/macho_uuid.cpp shared/launcher/app_platform.cpp \
  shared/launcher/engine.cpp shared/launcher/seams.cpp shared/launcher/text_input.cpp shared/launcher/game_import.cpp \
  shared/loader/convert.cpp
$(LAUNCHER_SO): $(LAUNCHER_SO_SRCS) $(wildcard android/launcher/*.h shared/launcher/*.h shared/include/mcfm/*.h) android/launcher/launcher.map $(LOADER_HDRS) $(RT_LIB)
	@mkdir -p $(dir $@)
	$(ACXX) $(RT_CXXFLAGS) -fPIC -shared -Ishared/loader -Ishared/apple -Ishared/launcher -Ishared/include -Iandroid/launcher $(LAUNCHER_SO_SRCS) \
	  $(RT_LDFLAGS) -Wl,--version-script,android/launcher/launcher.map -Wl,-soname,libmcfm_launcher.so -Wl,-z,defs -lEGL -lGLESv3 -laaudio -landroid -llog -lc -lm -o $@
MCFM_RUN := $(ALAUNCH_OUT)/mcfm-run
$(MCFM_RUN): android/launcher/mcfm_run.c $(LAUNCHER_SO)
	$(ACC) -Wall -O1 $(ANDROID_LDFLAGS) $< -ldl -o $@

.PHONY: android-boot-check
# Stage 3a acceptance on the running emulator/device: the converted game (make app) initializes.
android-boot-check: $(MCFM_RUN)
	ANDROID_CC="$(ACC)" bash tools/android/boot_check.sh "$(LAUNCHER_OUT)" "$(ALAUNCH_OUT)"

$(ALAUNCH_OUT)/tests/audio_test: android/launcher/tests/audio_test.cpp android/launcher/audio_toolbox.cpp android/launcher/audio_toolbox.h $(RT_LIB)
	@mkdir -p $(dir $@)
	$(ACXX) $(RT_CXXFLAGS) -Iandroid/launcher $< android/launcher/audio_toolbox.cpp $(RT_LDFLAGS) -laaudio -o $@

# The app (Stage 3c): dist/android/mcfm.apk, a launcher for the game bundled in it: the launcher
# library, the game's stubs (from the committed import list android/launcher/game_imports.tsv:
# mcfm_image.py imports on the game, minus libSystem/libc++/libz, which the launcher provides)
# and the game from $(IPA) (minecraftpe2 and data/ in assets/game: Mojang's files, so the APK is a
# local build, never committed). mcfm-debug.apk is the same app debuggable (run-as) for the checks.
IPA ?= $(firstword $(wildcard $(GAME_FILES)/ios/*.ipa))
APP_APK := $(CURDIR)/dist/android/mcfm.apk
APP_DEBUG_APK := $(CURDIR)/dist/android/mcfm-debug.apk
APP_STUBS := $(ALAUNCH_OUT)/app-stubs
APP_SRCS := $(wildcard android/app/AndroidManifest.xml android/app/res/*/* android/app/kotlin/io/github/d4yvid/mcfm/*.kt)
APP_ENV = BUILD_TOOLS="$(BUILD_TOOLS)" ANDROID_JAR="$(ANDROID_JAR)" KOTLINC="$(KOTLINC)" JAVA_HOME="$(JAVA_HOME_APP)" \
  LAUNCHER_SO="$(LAUNCHER_SO)" STUBS_DIR="$(APP_STUBS)"
.PHONY: android-app android-app-debug
android-app: $(APP_APK)
android-app-debug: $(APP_DEBUG_APK)
$(APP_STUBS)/.built: android/launcher/game_imports.tsv tools/launcher/build_stubs.sh macos/launcher/stub_runtime.c
	rm -rf $(APP_STUBS)
	ANDROID_CC="$(ACC)" bash tools/launcher/build_stubs.sh --target android android/launcher/game_imports.tsv $(APP_STUBS) >/dev/null
	touch $@
$(APP_APK) $(APP_DEBUG_APK): $(APP_SRCS) $(LAUNCHER_SO) $(APP_STUBS)/.built tools/android/build_app.sh $(IPA)
	@test -f "$(IPA)" || { echo "The APK bundles the game: make $(if $(filter $(APP_DEBUG_APK),$@),android-app-debug,android-app) IPA=<decrypted minecraftpe .ipa> (or put it in game-files/ios/)"; exit 1; }
	@test -x "$(KOTLINC)" -a -f "$(ANDROID_JAR)" || { echo "App toolchain missing (make android-app-sdk)"; exit 1; }
	$(APP_ENV) bash tools/android/build_app.sh $(if $(filter $(APP_DEBUG_APK),$@),--debug) --bundle "$(IPA)" $@

.PHONY: android-app-run
# Play the app in the emulator, in a window (API ?= 37; API=28 for Android 9): installs the debug
# APK keeping your worlds and opens the game.
android-app-run: $(APP_DEBUG_APK)
	ANDROID_SDK="$(ANDROID_SDK)" bash tools/android/app_run.sh $(APP_DEBUG_APK) $(API)

.PHONY: android-app-check
# Stage 3c acceptance on the running emulator/device: first start, title screen, a new world,
# lifecycle, re-conversion, the fatal path.
android-app-check: $(APP_DEBUG_APK)
	bash tools/android/app_check.sh $(APP_DEBUG_APK) $(ALAUNCH_OUT)

.PHONY: android-frames-check
# Stage 3b acceptance: the converted game (make app) boots and renders 120 frames on the device.
android-frames-check: $(MCFM_RUN)
	ANDROID_CC="$(ACC)" bash tools/android/frames_check.sh "$(LAUNCHER_OUT)" "$(ALAUNCH_OUT)"

ANDROID_TESTS := pthread_test runtime_test files_test net_test audio_test
.PHONY: android-test
# Needs a running emulator or device (make android-emulator).
android-test: $(addprefix $(ALAUNCH_OUT)/tests/,$(ANDROID_TESTS)) $(MCFM_RUN)
	@for t in $(ANDROID_TESTS); do bash tools/android/adb_run.sh $(ALAUNCH_OUT)/tests/$$t || exit 1; done
	bash tools/tests/android_runtime_symbols_test.sh $(RT_OUT) $(LAUNCHER_SO) $(NDK64_BIN)/llvm-nm $(addprefix $(ALAUNCH_OUT)/tests/,$(filter-out pthread_test,$(ANDROID_TESTS)))
	ANDROID_CC="$(ACC)" bash tools/tests/android_launcher_test.sh $(ALAUNCH_OUT)

# ---------------------------------------------------------------- local game files
# make game-files IOS=<minecraftpe2.ipa|.app> [APK_IN=<0.15.10 .apk>] [IDA=1]
game-files:
	bash tools/setup_game_files.sh $(if $(IOS),--ios "$(IOS)") $(if $(APK_IN),--apk "$(APK_IN)") $(if $(IDA),--ida)

# Needs the built app (make catalyst).
catalyst-check: $(BUILD)/test/macho_uuid_test $(BUILD)/test/keymap_test
	$(CATALYST_DEPRECATED)
	$(BUILD)/test/macho_uuid_test "$(OUT_APP)/minecraftpe" $(BUILD)/test/keymap_test
	bash macos/tests/bundle_test.sh "$(OUT_APP)"
	bash ios/tests/ipa_test.sh "$(GAME)"
	bash tools/tests/setup_game_files_test.sh "$(GAME)"
	bash macos/tests/smoke.sh "$(OUT_APP)"

# ---------------------------------------------------------------- host tests (no game files)
$(BUILD)/test/keymap_test: shared/tests/keymap_test.cpp shared/src/keymap.cpp shared/include/mcfm/input/keymap.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -O1 $(SHARED_INC) shared/tests/keymap_test.cpp shared/src/keymap.cpp -o $@

$(BUILD)/test/input_state_test: shared/tests/input_state_test.cpp shared/include/mcfm/input/input_state.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -O1 $(SHARED_INC) shared/tests/input_state_test.cpp -o $@

$(BUILD)/test/features_test: shared/tests/features_test.cpp shared/tests/fake_platform.h $(SHARED_CORE) $(wildcard shared/include/mcfm/*.h)
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 $(SHARED_INC) shared/tests/features_test.cpp $(SHARED_CORE) -o $@

$(BUILD)/test/macho_uuid_test: shared/apple/macho_uuid_test.cpp shared/apple/macho_uuid.cpp shared/apple/macho_uuid.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -O1 $(SHARED_INC) shared/apple/macho_uuid_test.cpp shared/apple/macho_uuid.cpp -o $@

LAUNCHER_SHARED_INC := -Ishared/launcher -Ishared/apple
$(BUILD)/test/launcher_app_platform_test: shared/tests/launcher_app_platform_test.cpp shared/launcher/app_platform.cpp shared/launcher/app_platform.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 $(LAUNCHER_SHARED_INC) shared/tests/launcher_app_platform_test.cpp shared/launcher/app_platform.cpp -o $@

$(BUILD)/test/launcher_engine_test: shared/tests/launcher_engine_test.cpp shared/launcher/engine.cpp shared/launcher/engine.h shared/launcher/seams.cpp shared/launcher/seams.h shared/launcher/app_platform.cpp shared/apple/addresses_0_15_10.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 $(LAUNCHER_SHARED_INC) shared/tests/launcher_engine_test.cpp shared/launcher/engine.cpp shared/launcher/seams.cpp shared/launcher/app_platform.cpp -o $@

$(BUILD)/test/resize_math_test: macos/tests/resize_math_test.cpp macos/launcher/resize_math.h
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -Wall -O1 -Imacos/launcher macos/tests/resize_math_test.cpp -o $@

$(BUILD)/test/screenshot_test: macos/tests/screenshot_test.cpp shared/launcher/screenshot.h
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -Wall -O1 -Ishared/launcher macos/tests/screenshot_test.cpp -o $@

$(BUILD)/test/hook_table_test: shared/tests/hook_table_test.cpp shared/apple/hook_table.cpp shared/apple/hook_table.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 -Ishared/apple shared/tests/hook_table_test.cpp shared/apple/hook_table.cpp -o $@

$(BUILD)/test/macho_uuid_bounds_test: shared/tests/macho_uuid_bounds_test.cpp shared/apple/macho_uuid.cpp shared/apple/macho_uuid.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -O1 -g -fsanitize=address -Ishared/apple shared/tests/macho_uuid_bounds_test.cpp shared/apple/macho_uuid.cpp -o $@

$(BUILD)/test/launcher_uuid_race_test: shared/tests/launcher_uuid_race_test.cpp shared/launcher/app_platform.cpp shared/launcher/app_platform.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -O1 -g -fsanitize=thread $(LAUNCHER_SHARED_INC) shared/tests/launcher_uuid_race_test.cpp shared/launcher/app_platform.cpp -o $@

$(BUILD)/test/launcher_platform_test: shared/tests/launcher_platform_test.cpp shared/launcher/launcher_platform.cpp shared/launcher/launcher_platform.h shared/apple/engine_mouse.h shared/src/keyboard_mouse.cpp shared/src/platform.cpp
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 $(LAUNCHER_SHARED_INC) $(SHARED_INC) shared/tests/launcher_platform_test.cpp shared/launcher/launcher_platform.cpp shared/src/keyboard_mouse.cpp shared/src/platform.cpp -o $@

$(BUILD)/test/launcher_text_test: shared/tests/launcher_text_test.cpp shared/launcher/text_input.cpp shared/launcher/text_input.h shared/launcher/app_platform.cpp shared/launcher/app_platform.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 $(LAUNCHER_SHARED_INC) shared/tests/launcher_text_test.cpp shared/launcher/text_input.cpp shared/launcher/app_platform.cpp -o $@

$(BUILD)/test/mac_keymap_test: macos/tests/mac_keymap_test.cpp macos/launcher/mac_keymap.cpp macos/launcher/mac_keymap.h
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -Wall -O1 -Imacos/launcher macos/tests/mac_keymap_test.cpp macos/launcher/mac_keymap.cpp -o $@

$(AUDIO_PROVIDER): macos/launcher/audio_toolbox.cpp
	@mkdir -p $(dir $@)
	clang++ -arch arm64 -mmacosx-version-min=11.0 -std=c++17 -O2 -Wall -Wextra -dynamiclib macos/launcher/audio_toolbox.cpp \
	  -install_name @rpath/libmcfm_audiotoolbox.dylib -o $@
# arm64 like the provider it loads (the launcher is Apple Silicon only).
$(BUILD)/test/audio_toolbox_test: macos/tests/audio_toolbox_test.cpp
	@mkdir -p $(dir $@)
	clang++ -arch arm64 -std=c++17 -Wall -O1 macos/tests/audio_toolbox_test.cpp -o $@

$(BUILD)/test/mouse_math_test: macos/tests/mouse_math_test.cpp macos/launcher/mouse_math.h
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -Wall -O1 -Imacos/launcher macos/tests/mouse_math_test.cpp -o $@

$(BUILD)/test/image_pick_test: macos/tests/image_pick_test.mm macos/launcher/image_pick.mm macos/launcher/image_pick.h
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -fobjc-arc -Wall -O1 -Imacos/launcher macos/tests/image_pick_test.mm macos/launcher/image_pick.mm \
	  -framework Foundation -framework AppKit -framework ImageIO -framework UniformTypeIdentifiers -o $@

$(BUILD)/test/macho_file_test: shared/tests/macho_file_test.cpp $(LOADER_SRCS) $(LOADER_HDRS)
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 -g -fsanitize=address -Ishared/loader -Ishared/apple shared/tests/macho_file_test.cpp $(LOADER_SRCS) -o $@

$(BUILD)/test/macho_fixups_test: shared/tests/macho_fixups_test.cpp $(LOADER_SRCS) $(LOADER_HDRS)
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Ishared/loader -Ishared/apple shared/tests/macho_fixups_test.cpp $(LOADER_SRCS) -o $@

$(BUILD)/test/game_import_test: shared/tests/game_import_test.cpp shared/launcher/game_import.cpp shared/launcher/game_import.h \
  shared/loader/convert.cpp shared/loader/convert.h shared/apple/macho_uuid.cpp
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 -g -fsanitize=address,undefined -Ishared/loader -Ishared/launcher -Ishared/apple \
	  shared/tests/game_import_test.cpp shared/launcher/game_import.cpp shared/loader/convert.cpp shared/apple/macho_uuid.cpp -o $@

$(BUILD)/test/input_events_test: android/launcher/tests/input_events_test.cpp android/launcher/input_events.cpp android/launcher/input_events.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 -g -fsanitize=thread -Iandroid/launcher $< android/launcher/input_events.cpp -o $@

$(BUILD)/test/render_requests_test: android/launcher/tests/render_requests_test.cpp android/launcher/render_requests.cpp android/launcher/render_requests.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 -g -fsanitize=thread -Iandroid/launcher $< android/launcher/render_requests.cpp -o $@

$(BUILD)/test/convert_tool: tools/loader/convert_tool.cpp shared/loader/convert.cpp shared/loader/convert.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Ishared/loader \
	  tools/loader/convert_tool.cpp shared/loader/convert.cpp -o $@

$(BUILD)/test/fixups_dump: tools/loader/fixups_dump.cpp $(LOADER_SRCS) $(LOADER_HDRS)
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -O1 -Ishared/loader -Ishared/apple tools/loader/fixups_dump.cpp $(LOADER_SRCS) -o $@

$(BUILD)/test/loader_core_test: shared/tests/loader_core_test.cpp shared/tests/macho_builder.h $(LOADER_SRCS) $(LOADER_HDRS)
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 -g -fsanitize=address -Ishared/loader -Ishared/apple shared/tests/loader_core_test.cpp $(LOADER_SRCS) -o $@

$(BUILD)/test/loader_run: tools/loader/loader_run.cpp macos/launcher/loader_macos.cpp macos/launcher/loader_macos.h $(LOADER_SRCS) $(LOADER_HDRS)
	@mkdir -p $(dir $@)
	clang++ -arch arm64 -std=c++17 -Wall -O1 -Ishared/loader -Ishared/apple -Imacos/launcher tools/loader/loader_run.cpp macos/launcher/loader_macos.cpp $(LOADER_SRCS) -o $@

$(BUILD)/test/titlebar_test: macos/tests/titlebar_test.cpp macos/src/titlebar_zone.h
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -Wall -O1 macos/tests/titlebar_test.cpp -o $@

$(BUILD)/test/input_policy_test: macos/tests/input_policy_test.cpp macos/src/input_policy.h
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -Wall -O1 $(SHARED_INC) macos/tests/input_policy_test.cpp -o $@

test: $(addprefix $(BUILD)/test/,$(SHARED_TESTS) $(MACOS_TESTS))
	@for t in $(SHARED_TESTS) $(MACOS_TESTS); do $(BUILD)/test/$$t || exit 1; done
	$(MAKE) --no-print-directory ios-syntax
	bash tools/tests/inject_test.sh
	bash tools/tests/check_game_test.sh $(GAME)
	bash macos/tests/convert_guard_test.sh
	bash android/tests/patch_smali_test.sh
	bash android/tests/check_apk_lib_test.sh
	clang++ -std=c++11 -Wall android/tests/vtable_scan_test.cpp -o $(BUILD)/test/vtable_scan_test && $(BUILD)/test/vtable_scan_test
	bash tools/tests/config_example_test.sh
	bash tools/tests/makefile_deps_test.sh
	bash tools/tests/make_targets_test.sh
	bash tools/tests/launcher_imports_test.sh
	bash tools/tests/launcher_stubs_test.sh
	bash tools/tests/launcher_image_test.sh
	bash tools/tests/mcfm_launch_test.sh
	bash tools/tests/launcher_hooks_test.sh
	bash tools/tests/launcher_converter_edges_test.sh
	bash tools/tests/thin_arm64_test.sh
	bash tools/tests/loader_test.sh
	bash tools/tests/convert_test.sh
	$(MAKE) --no-print-directory $(BUILD)/test/input_events_test && $(BUILD)/test/input_events_test
	$(MAKE) --no-print-directory $(BUILD)/test/render_requests_test && $(BUILD)/test/render_requests_test
	$(MAKE) --no-print-directory $(AUDIO_PROVIDER) $(BUILD)/test/audio_toolbox_test && $(BUILD)/test/audio_toolbox_test $(AUDIO_PROVIDER) && MCFM_AUDIO_DISABLE=1 $(BUILD)/test/audio_toolbox_test $(AUDIO_PROVIDER)
	bash tools/tests/launcher_provider_test.sh
	bash tools/tests/fetch_angle_test.sh
	bash tools/tests/fetch_sdk_test.sh
	bash tools/tests/darwin_abi_test.sh
	bash tools/tests/android_symbols_test.sh
	$(MAKE) --no-print-directory $(BUILD)/test/screenshot_test && $(BUILD)/test/screenshot_test "$$(mktemp -d)/shot.ppm"
	bash tools/tests/no_game_files_test.sh
	bash tools/tests/setup_game_files_test.sh

clean:
	rm -rf $(BUILD)
