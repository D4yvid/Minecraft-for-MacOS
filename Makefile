# Minecraft for macOS / iOS / Android — mods for Minecraft PE 0.15.10.
# Local paths (your own game files) go in config.mk; see config.example.mk.
-include config.mk

BUILD   ?= build
GAME    ?=
OUT_APP ?= $(CURDIR)/dist/minecraftpe.app

SHARED_INC   := -Ishared/include -Ishared/apple
SHARED_CORE  := shared/src/platform.cpp shared/src/win10_ui.cpp shared/src/keyboard_mouse.cpp
SHARED_TESTS := keymap_test input_state_test features_test
MACOS_TESTS  := titlebar_test input_policy_test

# ---------------------------------------------------------------- macOS (Mac Catalyst)
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

.PHONY: all macos app run check test clean ios ios-ipa ios-syntax android android-apk
all: macos

macos: $(MAC_DYLIB)

$(MAC_DYLIB): $(MAC_SRCS) $(wildcard macos/src/*.h shared/include/mcfm/*/*.h shared/apple/*.h)
	@mkdir -p $(dir $@)
	clang++ $(MAC_CXXFLAGS) $(MAC_LDFLAGS) $(MAC_SRCS) -o $@

app: $(MAC_DYLIB)
	@test -n "$(GAME)" || { echo "Set GAME=<your decrypted minecraftpe2.app> (or put it in config.mk)"; exit 1; }
	bash macos/tools/convert.sh "$(GAME)" "$(OUT_APP)" "$(MAC_DYLIB)"

run:
	open "$(OUT_APP)"

# ---------------------------------------------------------------- iOS
IOS_SDK    := $(shell xcrun --sdk iphoneos --show-sdk-path 2>/dev/null)
IOS_TARGET := arm64-apple-ios15.0
IOS_DYLIB  := $(BUILD)/ios/libmcfm.dylib
IOS_IPA    ?= $(CURDIR)/dist/minecraftpe-mcfm.ipa
IOS_SRCS   := $(SHARED_CORE) $(APPLE_SRCS) ios/src/main.mm
IOS_CXXFLAGS := -target $(IOS_TARGET) $(SHARED_INC) -std=c++17 -fobjc-arc -O2 -Wall -Wextra -Wno-unused-parameter

ios: $(IOS_DYLIB)

$(IOS_DYLIB): $(IOS_SRCS) $(wildcard shared/include/mcfm/*.h shared/apple/*.h)
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

APK ?=
ANDROID_APK ?= $(CURDIR)/dist/minecraftpe-mcfm.apk
android-apk: android
	@test -n "$(APK)" || { echo "Set APK=<your Minecraft PE 0.15.10 .apk> (or put it in config.mk)"; exit 1; }
	bash android/tools/build_apk.sh "$(APK)" "$(dir $(ANDROID_LIB))" "$(ANDROID_APK)"

# Needs the built app (make app).
check: $(BUILD)/test/macho_uuid_test $(BUILD)/test/keymap_test
	$(BUILD)/test/macho_uuid_test "$(OUT_APP)/minecraftpe" $(BUILD)/test/keymap_test
	bash macos/tests/bundle_test.sh "$(OUT_APP)"
	bash ios/tests/ipa_test.sh "$(GAME)"
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

clean:
	rm -rf $(BUILD)
