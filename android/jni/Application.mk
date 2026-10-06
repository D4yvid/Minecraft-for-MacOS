# libminecraftpe.so (0.15.10) is armeabi-v7a and built against gnustl: match both, or
# std::string values crossing into the game would have the wrong layout.
APP_ABI := armeabi-v7a
APP_PLATFORM := android-21
APP_STL := gnustl_shared
APP_CPPFLAGS += -O2 -std=gnu++11 -Wall -Werror=return-type -DRELEASE_BUILD=$(RELEASE_BUILD)
NDK_TOOLCHAIN_VERSION := 4.9
