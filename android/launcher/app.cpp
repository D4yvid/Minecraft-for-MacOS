// The Android app's native side (docs/LAUNCHER.md, Stage 3c): JNI entry points of
// io.github.d4yvid.mcfm.Native. The import runs on the caller's (background) thread; the game
// runs on a render thread (game_thread.cpp) fed through an event queue.
#include <jni.h>

#include <string>
#include <vector>

#include "game_import.h"
#include "seams.h"

namespace {

std::string from_java(JNIEnv *env, jstring s) {
  if (!s) return std::string();
  const char *chars = env->GetStringUTFChars(s, nullptr);
  std::string out(chars ? chars : "");
  if (chars) env->ReleaseStringUTFChars(s, chars);
  return out;
}

}  // namespace

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM *, void *) { return JNI_VERSION_1_6; }

// Converts the extracted game binary into <dir>/minecraftpe.dylib. Returns null, or a message.
JNIEXPORT jstring JNICALL Java_io_github_d4yvid_mcfm_Native_nativeImport(JNIEnv *env, jclass, jstring binary, jstring dir) {
  mcfm::launcher::ImportOptions options;
  size_t n = 0;
  const mcfm::launcher::Hook *h = mcfm::launcher::hooks(&n);
  for (size_t i = 0; i < n; i++) options.hooks.push_back({h[i].name, h[i].address});
  std::string error = mcfm::launcher::import_game(from_java(env, binary), from_java(env, dir), options);
  return error.empty() ? nullptr : env->NewStringUTF(error.c_str());
}

}  // extern "C"
