SDK      := $(shell xcrun --sdk macosx --show-sdk-path)
IOSFW    := $(SDK)/System/iOSSupport/System/Library/Frameworks
TARGET   := arm64-apple-ios15.0-macabi
BUILD    := build
DYLIB    := $(BUILD)/libmcpekbm.dylib
ORIG_APP ?= /Users/dayvid/Downloads/Payload/minecraftpe2.app
OUT_APP  ?= /Users/dayvid/Downloads/Payload/MinecraftPE-Mac/minecraftpe2.app

SRCS := src/keymap.cpp src/macho_uuid.cpp src/engine.mm src/pointer_lock.mm src/platform.mm \
        src/resize.mm src/mac_input.mm src/titlebar.mm src/main.mm

CXXFLAGS := -target $(TARGET) -isysroot $(SDK) -iframework $(IOSFW) \
            -std=c++17 -fobjc-arc -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS  := -dynamiclib -F$(IOSFW) -framework Foundation -framework UIKit \
            -framework GameController -framework QuartzCore \
            -install_name @executable_path/Frameworks/libmcpekbm.dylib

.PHONY: all test install smoke clean
all: $(DYLIB)

$(DYLIB): $(SRCS) $(wildcard src/*.h)
	@mkdir -p $(BUILD)
	clang++ $(CXXFLAGS) $(LDFLAGS) $(SRCS) -o $@

$(BUILD)/keymap_test: tests/keymap_test.cpp src/keymap.cpp src/keymap.h
	@mkdir -p $(BUILD)
	clang++ -std=c++17 -Wall -O1 tests/keymap_test.cpp src/keymap.cpp -o $@

$(BUILD)/titlebar_test: tests/titlebar_test.cpp src/titlebar_zone.h
	@mkdir -p $(BUILD)
	clang++ -std=c++17 -Wall -O1 tests/titlebar_test.cpp -o $@

$(BUILD)/macho_uuid_test: tests/macho_uuid_test.cpp src/macho_uuid.cpp src/macho_uuid.h
	@mkdir -p $(BUILD)
	clang++ -std=c++17 -Wall -O1 tests/macho_uuid_test.cpp src/macho_uuid.cpp -o $@

$(BUILD)/input_policy_test: tests/input_policy_test.cpp src/input_policy.h
	@mkdir -p $(BUILD)
	clang++ -std=c++17 -Wall -O1 tests/input_policy_test.cpp -o $@

test: $(BUILD)/keymap_test $(BUILD)/titlebar_test $(BUILD)/macho_uuid_test $(BUILD)/input_policy_test
	$(BUILD)/input_policy_test
	$(BUILD)/keymap_test
	$(BUILD)/titlebar_test
	$(BUILD)/macho_uuid_test "$(OUT_APP)/minecraftpe2" $(BUILD)/keymap_test
	bash tests/inject_test.sh
	bash tests/convert_guard_test.sh

install: $(DYLIB)
	bash tools/convert.sh "$(ORIG_APP)" "$(OUT_APP)" "$(DYLIB)"

smoke:
	bash tests/smoke.sh "$(OUT_APP)"

clean:
	rm -rf $(BUILD)
