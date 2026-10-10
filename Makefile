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
SHARED_TESTS := keymap_test input_state_test features_test launcher_app_platform_test launcher_engine_test hook_table_test macho_uuid_bounds_test launcher_uuid_race_test
MACOS_TESTS  := titlebar_test input_policy_test resize_math_test

# ---------------------------------------------------------------- macOS (Mac Catalyst) — DEPRECATED
# The Catalyst build is a deprecated build mode: it keeps working until the Mach-O launcher
# (docs/LAUNCHER.md) replaces it. Targets: catalyst, catalyst-run, catalyst-check; the old
# names app, run, check are aliases (they will move to the launcher).
CATALYST_DEPRECATED = @echo "mcfm: note: the Mac Catalyst build is deprecated; it will be replaced by the Mach-O launcher (docs/LAUNCHER.md)" >&2
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

app: catalyst
run: catalyst-run
check: catalyst-check

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
LAUNCHER_SRCS := macos/launcher/main.mm shared/apple/macho_uuid.cpp shared/apple/hook_table.cpp \
                 shared/launcher/app_platform.cpp shared/launcher/engine.cpp shared/launcher/seams.cpp
LAUNCHER_CXXFLAGS := -arch arm64 -mmacosx-version-min=11.0 -std=c++17 -fobjc-arc -O2 -Wall -Wextra \
                     -Wno-unused-parameter -Ishared/apple -Ishared/launcher -Imacos/launcher

$(LAUNCHER_BIN): $(LAUNCHER_SRCS) $(wildcard shared/launcher/*.h macos/launcher/*.h) shared/apple/hook_table.h shared/apple/macho_uuid.h shared/apple/addresses_0_15_10.h
	@mkdir -p $(dir $@)
	clang++ $(LAUNCHER_CXXFLAGS) $(LAUNCHER_SRCS) -framework AppKit -framework QuartzCore \
	  -Wl,-rpath,@executable_path -o $@

LAUNCHER_OUT ?= $(CURDIR)/dist/launcher
ANGLE_DIR ?= $(CURDIR)/$(BUILD)/angle

.PHONY: angle
# Downloads ANGLE once (pinned Electron release, ~130 MB); see tools/launcher/fetch_angle.sh.
angle:
	bash tools/launcher/fetch_angle.sh "$(ANGLE_DIR)"

.PHONY: launcher launcher-check
launcher: $(LAUNCHER_BIN)
	@test -n "$(GAME)" || { echo "Set GAME=<your decrypted minecraftpe2.app> (or put it in config.mk)"; exit 1; }
	@test -f "$(ANGLE_DIR)/libGLESv2.dylib" -a -f "$(ANGLE_DIR)/libEGL.dylib" || { echo "Run make angle first (downloads ANGLE)"; exit 1; }
	bash macos/tools/make_launcher.sh "$(GAME)" "$(LAUNCHER_OUT)" "$(LAUNCHER_BIN)" "$(ANGLE_DIR)"

# Loads the image built by make launcher; the census lists every stub the game called.
launcher-check:
	@rm -f $(BUILD)/launcher/census.txt; mkdir -p $(BUILD)/launcher
	@OUT="$$(MCFM_CENSUS="$(CURDIR)/$(BUILD)/launcher/census.txt" "$(LAUNCHER_OUT)/mcfm-launch" --frames 120 2>&1)"; \
	  echo "$$OUT" | grep -E "^mcfm: (game image|EGL|engine|[0-9]+ frames)" ; \
	  { echo "$$OUT" | grep -q "game image loaded" && echo "$$OUT" | grep -q "120 frames rendered"; } || { echo "$$OUT" | tail -25; echo "launcher-check: FAILED"; exit 1; }
	@grep -qxF "libobjc:_objc_autoreleasePoolPush" $(BUILD)/launcher/census.txt || { echo "launcher-check: initializers did not reach the stubs"; exit 1; }
	@echo "launcher-check: passed ($$(wc -l < $(BUILD)/launcher/census.txt | tr -d ' ') stubs called, see $(BUILD)/launcher/census.txt)"

.PHONY: launcher-run
launcher-run:
	"$(LAUNCHER_OUT)/mcfm-launch"

# ---------------------------------------------------------------- Android
# NDK r10c (x86_64 host build; runs under Rosetta on Apple Silicon).
NDK ?= $(HOME)/Library/Android/ndk/android-ndk-r10c
RELEASE_BUILD ?= 0
ANDROID_OUT := $(CURDIR)/$(BUILD)/android
ANDROID_LIB := $(ANDROID_OUT)/libs/armeabi-v7a/librunet.so
HOST_X86 := $(if $(filter arm64,$(shell uname -m)),/usr/bin/arch -x86_64,)

android:
	@test -x "$(NDK)/ndk-build" || { echo "Set NDK=<path to android-ndk-r10c> (or put it in config.mk)"; exit 1; }
	cd android && $(HOST_X86) /bin/bash -c '"$(NDK)/ndk-build" -j8 NDK_PROJECT_PATH=. \
	  NDK_OUT="$(ANDROID_OUT)/obj" NDK_LIBS_OUT="$(ANDROID_OUT)/libs" RELEASE_BUILD=$(RELEASE_BUILD)'
	bash android/tests/lib_test.sh "$(ANDROID_LIB)" "$(NDK)"
	bash android/tests/pick_gnustl_test.sh "$(NDK)" "$(dir $(ANDROID_LIB))"

APK ?= $(firstword $(filter-out %runet-patched.apk,$(wildcard $(GAME_FILES)/android/apk/*.apk)))
ANDROID_APK ?= $(CURDIR)/dist/minecraftpe-mcfm.apk
android-apk: android
	@test -n "$(APK)" || { echo "Set APK=<your Minecraft PE 0.15.10 .apk> (or put it in config.mk)"; exit 1; }
	NDK="$(NDK)" bash android/tools/build_apk.sh "$(APK)" "$(dir $(ANDROID_LIB))" "$(ANDROID_APK)"

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

$(BUILD)/test/screenshot_test: macos/tests/screenshot_test.cpp macos/launcher/screenshot.h
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -Wall -O1 -Imacos/launcher macos/tests/screenshot_test.cpp -o $@

$(BUILD)/test/hook_table_test: shared/tests/hook_table_test.cpp shared/apple/hook_table.cpp shared/apple/hook_table.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 -Ishared/apple shared/tests/hook_table_test.cpp shared/apple/hook_table.cpp -o $@

$(BUILD)/test/macho_uuid_bounds_test: shared/tests/macho_uuid_bounds_test.cpp shared/apple/macho_uuid.cpp shared/apple/macho_uuid.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -O1 -g -fsanitize=address -Ishared/apple shared/tests/macho_uuid_bounds_test.cpp shared/apple/macho_uuid.cpp -o $@

$(BUILD)/test/launcher_uuid_race_test: shared/tests/launcher_uuid_race_test.cpp shared/launcher/app_platform.cpp shared/launcher/app_platform.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -O1 -g -fsanitize=thread $(LAUNCHER_SHARED_INC) shared/tests/launcher_uuid_race_test.cpp shared/launcher/app_platform.cpp -o $@

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
	bash tools/tests/catalyst_deprecated_test.sh
	bash tools/tests/launcher_imports_test.sh
	bash tools/tests/launcher_stubs_test.sh
	bash tools/tests/launcher_image_test.sh
	bash tools/tests/mcfm_launch_test.sh
	bash tools/tests/launcher_hooks_test.sh
	bash tools/tests/launcher_converter_edges_test.sh
	bash tools/tests/thin_arm64_test.sh
	bash tools/tests/launcher_provider_test.sh
	bash tools/tests/fetch_angle_test.sh
	$(MAKE) --no-print-directory $(BUILD)/test/screenshot_test && $(BUILD)/test/screenshot_test "$$(mktemp -d)/shot.ppm"
	bash tools/tests/no_game_files_test.sh
	bash tools/tests/setup_game_files_test.sh

clean:
	rm -rf $(BUILD)
