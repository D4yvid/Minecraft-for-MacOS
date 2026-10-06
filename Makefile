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

.PHONY: all macos app run check test clean
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

# Needs the built app (make app).
check: $(BUILD)/test/macho_uuid_test $(BUILD)/test/keymap_test
	$(BUILD)/test/macho_uuid_test "$(OUT_APP)/minecraftpe" $(BUILD)/test/keymap_test
	bash macos/tests/bundle_test.sh "$(OUT_APP)"
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
	bash tools/tests/inject_test.sh
	bash macos/tests/convert_guard_test.sh

clean:
	rm -rf $(BUILD)
