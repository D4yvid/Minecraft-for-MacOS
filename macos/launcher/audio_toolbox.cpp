// AudioToolbox for the game image's FMOD (docs/LAUNCHER.md, Stage 1c): the functions FMOD's
// iOS output calls, on macOS. RemoteIO (iOS only) becomes the default output unit, the iOS-only
// AudioSession is answered here, the rest goes to the real AudioToolbox. No Apple headers:
// AudioSessionGetProperty is unavailable in the macOS SDK.
#include <dlfcn.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

typedef int32_t OSStatus;
struct ComponentDesc { uint32_t type, subtype, manufacturer, flags, mask; };

constexpr uint32_t fourcc(const char (&s)[5]) {
  return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 | uint8_t(s[3]);
}
constexpr OSStatus kUnimplemented = -4;  // kAudio_UnimplementedError

void *real(const char *name) {
  static void *lib = std::getenv("MCFM_AUDIO_DISABLE")
                         ? nullptr
                         : dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox", RTLD_NOW | RTLD_LOCAL);
  return lib ? dlsym(lib, name) : nullptr;
}

#define FORWARD(ret, name, params, args, fallback)        \
  extern "C" ret name params {                            \
    using Fn = ret(*) params;                             \
    static Fn fn = reinterpret_cast<Fn>(real(#name));     \
    return fn ? fn args : fallback;                       \
  }

}  // namespace

extern "C" OSStatus AudioSessionGetProperty(uint32_t id, uint32_t *size, void *data) {
  auto put = [&](const void *value, uint32_t n) -> OSStatus {
    if (!size || *size < n) return static_cast<OSStatus>(fourcc("!siz"));
    std::memcpy(data, value, n);
    *size = n;
    return 0;
  };
  if (id == fourcc("choc")) { uint32_t v = 2; return put(&v, 4); }               // output channels
  if (id == fourcc("chsr")) { double v = 48000.0; return put(&v, 8); }           // hardware sample rate
  if (id == fourcc("acat")) { uint32_t v = fourcc("ambi"); return put(&v, 4); }  // ambient: no recording
  return static_cast<OSStatus>(fourcc("pty?"));
}

extern "C" void *AudioComponentFindNext(void *after, const ComponentDesc *desc) {
  using Fn = void *(*)(void *, const ComponentDesc *);
  static Fn fn = reinterpret_cast<Fn>(real("AudioComponentFindNext"));
  if (!fn || !desc) return nullptr;
  ComponentDesc d = *desc;
  if (d.type == fourcc("auou") && d.subtype == fourcc("rioc")) d.subtype = fourcc("def ");  // RemoteIO -> default output
  return fn(after, &d);
}

FORWARD(OSStatus, AudioComponentInstanceNew, (void *c, void **out), (c, out), kUnimplemented)
FORWARD(OSStatus, AudioComponentInstanceDispose, (void *u), (u), kUnimplemented)
FORWARD(OSStatus, AudioUnitInitialize, (void *u), (u), kUnimplemented)
FORWARD(OSStatus, AudioUnitUninitialize, (void *u), (u), kUnimplemented)
FORWARD(OSStatus, AudioUnitSetProperty, (void *u, uint32_t id, uint32_t scope, uint32_t el, const void *d, uint32_t n),
        (u, id, scope, el, d, n), kUnimplemented)
FORWARD(OSStatus, AudioUnitGetProperty, (void *u, uint32_t id, uint32_t scope, uint32_t el, void *d, uint32_t *n),
        (u, id, scope, el, d, n), kUnimplemented)
FORWARD(OSStatus, AudioUnitRender, (void *u, uint32_t *flags, const void *ts, uint32_t bus, uint32_t frames, void *list),
        (u, flags, ts, bus, frames, list), kUnimplemented)
FORWARD(OSStatus, AudioOutputUnitStart, (void *u), (u), kUnimplemented)
FORWARD(OSStatus, AudioOutputUnitStop, (void *u), (u), kUnimplemented)
