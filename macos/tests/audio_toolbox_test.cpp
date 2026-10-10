// The AudioToolbox provider: iOS session answers, RemoteIO mapped to the default output,
// and a clean failure when the real framework is unavailable.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

typedef int32_t OSStatus;
struct ComponentDesc { uint32_t type, subtype, manufacturer, flags, mask; };
static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

int main(int, char **argv) {
  void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) { std::printf("FAIL: %s\n", dlerror()); return 1; }
  auto session = (OSStatus (*)(uint32_t, uint32_t *, void *))dlsym(lib, "AudioSessionGetProperty");
  auto find = (void *(*)(void *, const ComponentDesc *))dlsym(lib, "AudioComponentFindNext");
  auto create = (OSStatus (*)(void *, void **))dlsym(lib, "AudioComponentInstanceNew");
  auto dispose = (OSStatus (*)(void *))dlsym(lib, "AudioComponentInstanceDispose");
  EXPECT(session && find && create && dispose);
  uint32_t channels = 0, size = 4;
  EXPECT(session(FOURCC('c', 'h', 'o', 'c'), &size, &channels) == 0 && channels == 2 && size == 4);
  double rate = 0; size = 8;
  EXPECT(session(FOURCC('c', 'h', 's', 'r'), &size, &rate) == 0 && rate == 48000.0);
  uint32_t category = 0; size = 4;
  EXPECT(session(FOURCC('a', 'c', 'a', 't'), &size, &category) == 0 && category == FOURCC('a', 'm', 'b', 'i'));
  uint32_t x = 0; size = 4;
  EXPECT(session(FOURCC('x', 'x', 'x', 'x'), &size, &x) == (OSStatus)FOURCC('p', 't', 'y', '?'));
  size = 1;
  EXPECT(session(FOURCC('c', 'h', 'o', 'c'), &size, &channels) == (OSStatus)FOURCC('!', 's', 'i', 'z'));
  ComponentDesc remote_io = {FOURCC('a', 'u', 'o', 'u'), FOURCC('r', 'i', 'o', 'c'), FOURCC('a', 'p', 'p', 'l'), 0, 0};
  bool disabled = std::getenv("MCFM_AUDIO_DISABLE") != nullptr;
  void *component = find(nullptr, &remote_io);
  if (disabled) {
    EXPECT(component == nullptr);
    void *unit = nullptr;
    EXPECT(create(nullptr, &unit) != 0);
  } else {
    EXPECT(component != nullptr);  // RemoteIO does not exist on macOS: mapped to DefaultOutput
    void *unit = nullptr;
    EXPECT(component && create(component, &unit) == 0 && unit != nullptr);
    if (unit) EXPECT(dispose(unit) == 0);
  }
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("audio_toolbox_test: all passed%s\n", disabled ? " (framework disabled)" : "");
  return 0;
}
