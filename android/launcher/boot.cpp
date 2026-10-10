// mcfm-run --boot (docs/LAUNCHER.md, Stage 3b): boots the engine of the loaded game image with
// our AppPlatform (shared/launcher, the same code as the macOS launcher) in the current GLES 3
// context, renders frames headless, drains the main dispatch queue between frames, writes a
// screenshot of the last frame, suspends (the game saves) and ends the process (_exit, as on
// macOS). A window comes with Stage 3c's APK.
#include "boot.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

#include "darwin.h"
#include "engine.h"
#include "screenshot.h"

namespace mcfm {
namespace android {
namespace {

// mkdir -p.
void make_dirs(const std::string &path) {
  for (size_t i = 1; i <= path.size(); i++)
    if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0700);
}

}  // namespace

int boot(uintptr_t slide, const BootOptions &o) {
  using namespace mcfm::launcher;
  std::string data = o.data_dir;
  if (data.empty() || data.back() != '/') data += '/';
  HostInfo info = make_host_info(o.home_dir, data, o.home_dir + "/tmp");
  for (const std::string &d : {info.internal_dir, info.userdata_dir, info.temp_dir}) make_dirs(d);
  static Engine engine;  // lives as long as the process: the engine keeps pointers into it
  if (!engine.start(EngineAddresses::for_slide(slide), info, o.width, o.height)) {
    std::fprintf(stderr, "mcfm: engine start failed\n");
    return 5;
  }
  std::printf("mcfm: engine started (%dx%d)\n", o.width, o.height);
  EGLDisplay display = eglGetCurrentDisplay();
  EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
  for (long frame = 1; frame <= o.frames; frame++) {
    engine.frame();
    mcfm_darwin_drain_main_queue();
    if (frame == o.frames && !o.screenshot.empty()) {
      std::vector<unsigned char> pixels(static_cast<size_t>(o.width) * o.height * 4);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, o.width, o.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      if (write_ppm(o.screenshot.c_str(), o.width, o.height, pixels.data()))
        std::printf("mcfm: screenshot %s\n", o.screenshot.c_str());
      else
        std::fprintf(stderr, "mcfm: cannot write %s (errno %d)\n", o.screenshot.c_str(), errno);
    }
    eglSwapBuffers(display, surface);
  }
  std::printf("mcfm: %ld frames rendered\n", o.frames);
  std::printf("mcfm: quitting: game saving (app suspended)\n");
  engine.suspend();
  mcfm_darwin_drain_main_queue();
  // As the macOS launcher: the engine's threads (FMOD's mixer, the AAudio callback, ...) are still
  // running, so the game's static destructors (and ours) must not run: leave without exit().
  std::printf("mcfm: saved\n");
  std::fflush(stdout);
  std::fflush(stderr);
  _exit(0);
}

}  // namespace android
}  // namespace mcfm
