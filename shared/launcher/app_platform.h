#pragma once
// Our AppPlatform for the Mach-O launcher (docs/LAUNCHER.md): the engine's base AppPlatform
// object with a vtable we build — base slots kept, pure-virtual ones implemented, desktop
// (Win10 UI) policy set. Slot numbers: docs/research/appplatform-vtable.md. C++11.
#include <cstddef>
#include <string>

namespace mcfm {
namespace launcher {

struct HostInfo {
  std::string data_dir;      // the game's data/ directory, ending with '/'
  std::string external_dir;  // worlds live in <external_dir>/games/com.mojang
  std::string internal_dir;
  std::string userdata_dir;
  std::string temp_dir;
  std::string region;        // language_REGION, e.g. en_US (as iOS reports it)
  std::string device_id;
  int input_mode = 1;        // getDefaultInputMode: 1 mouse (desktop), 2 touch (Android)
};

constexpr size_t kBaseSlots = 101;  // base AppPlatform vtable (iOS adds 101-102)

// Engine functions some slots point at directly (slid addresses).
struct EngineFns {
  void *graphics_vendor, *graphics_renderer, *graphics_version, *graphics_extensions;
};

void set_host_info(const HostInfo &info);

// The storage layout AppPlatform_iOS builds from its Documents directory: worlds and options in
// <documents>/games/com.mojang/ (minecraftWorlds/, minecraftpe/), as in the Catalyst build.
HostInfo make_host_info(const std::string &documents_dir, const std::string &data_dir, const std::string &temp_dir);

// The engine opens / closes a text box (AppPlatform slots 9 showKeyboard, 10 hideKeyboard).
struct KeyboardCallbacks {
  void (*show)(const std::string &initial_text);
  void (*hide)();
};
void set_keyboard_callbacks(const KeyboardCallbacks &callbacks);

// out[i] = base[i] for every slot we do not implement.
void build_vtable(void **out, void *const *base, const EngineFns &fns);

}  // namespace launcher
}  // namespace mcfm
