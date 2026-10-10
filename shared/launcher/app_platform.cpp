#include "app_platform.h"

#include <cstdio>
#include <random>

namespace mcfm {
namespace launcher {
namespace {

// Slot functions: `self` is the engine's `this`; std::string results use the engine's ABI
// (returned through x8), which a free function returning std::string matches.
HostInfo &host() { static HostInfo info; return info; }

std::string data_url(void *) { return host().data_dir; }
void no_op(void *) {}
void pick_image(void *, void *) {}
const std::string &region(void *) { return host().region; }
const std::string &external_dir(void *) { return host().external_dir; }
const std::string &internal_dir(void *) { return host().internal_dir; }
const std::string &userdata_dir(void *) { return host().userdata_dir; }
const std::string &temp_dir(void *) { return host().temp_dir; }
std::string asset_full_path(void *, const std::string &rel) { return host().data_dir + rel; }
bool yes(void *) { return true; }
bool no(void *) { return false; }
int zero(void *) { return 0; }
int mouse_input(void *) { return 1; }
std::string application_id(void *) { return "com.mojang.minecraftpe"; }
std::string device_id(void *) { return host().device_id; }
std::string edition(void *) { return "win10"; }

std::string create_uuid(void *) {
  static std::mt19937_64 rng{std::random_device{}()};
  unsigned char b[16];
  for (int i = 0; i < 16; i += 8) {
    unsigned long long r = rng();
    for (int k = 0; k < 8; k++) b[i + k] = static_cast<unsigned char>(r >> (8 * k));
  }
  b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);  // version 4
  b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);  // RFC 4122 variant
  char s[37];
  std::snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  return s;
}

template <class F> void *fn(F f) { return reinterpret_cast<void *>(f); }

}  // namespace

void set_host_info(const HostInfo &info) { host() = info; }

void build_vtable(void **out, void *const *base, const EngineFns &fns) {
  for (size_t i = 0; i < kBaseSlots; i++) out[i] = base[i];
  out[2] = fn(&data_url);           // getDataUrl
  out[4] = fn(&data_url);           // getPackagePath
  out[18] = fn(&no_op);             // swapBuffers: the host presents
  out[20] = fn(&region);            // getSystemRegion
  out[21] = fns.graphics_vendor;
  out[22] = fns.graphics_renderer;
  out[23] = fns.graphics_version;
  out[24] = fns.graphics_extensions;
  out[25] = fn(&pick_image);        // pickImage
  out[33] = fn(&external_dir);      // getExternalStoragePath
  out[34] = fn(&internal_dir);      // getInternalStoragePath
  out[35] = fn(&userdata_dir);      // getUserdataPath
  out[53] = fn(&asset_full_path);   // getAssetFileFullPath
  out[66] = fn(&yes);               // useMetadataDrivenScreens
  out[68] = fn(&yes);               // useCenteredGUI
  out[69] = fn(&zero);              // getPlatformType: desktop
  out[74] = fn(&application_id);    // getApplicationId
  out[80] = fn(&device_id);         // getDeviceId
  out[81] = fn(&create_uuid);       // createUUID
  out[82] = fn(&no);                // isFirstSnoopLaunch
  out[83] = fn(&no);                // hasHardwareInformationChanged
  out[84] = fn(&no);                // isTablet
  out[93] = fn(&edition);           // getEdition
  out[96] = fn(&mouse_input);       // getDefaultInputMode
  out[99] = fn(&zero);              // getPlatformUIScalingRules: desktop
  out[100] = fn(&temp_dir);         // getPlatformTempPath
}

}  // namespace launcher
}  // namespace mcfm
