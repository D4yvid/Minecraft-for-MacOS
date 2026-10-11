// The Android app's native side (docs/LAUNCHER.md, Stage 3c): JNI entry points of
// io.github.d4yvid.mcfm.Native. The import runs on the caller's (background) thread; the game
// runs on a render thread (game_thread.cpp) fed through an event queue; JNI calls come from the
// UI thread and never touch the engine.
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "game_import.h"
#include "game_thread.h"
#include "input_events.h"
#include "seams.h"

namespace {

std::string from_java(JNIEnv *env, jstring s) {
  if (!s) return std::string();
  const char *chars = env->GetStringUTFChars(s, nullptr);
  std::string out(chars ? chars : "");
  if (chars) env->ReleaseStringUTFChars(s, chars);
  return out;
}

// Calls into io.github.d4yvid.mcfm.Native's static methods, from the render thread, which stays
// attached to the JVM until it ends (a thread that exits attached aborts the process).
JavaVM *g_vm = nullptr;
jclass g_native = nullptr;
jmethodID g_show_keyboard = nullptr, g_hide_keyboard = nullptr, g_fatal = nullptr, g_pick_image = nullptr,
          g_first_frame = nullptr;

JNIEnv *thread_env() {
  JNIEnv *env = nullptr;
  if (g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
    g_vm->AttachCurrentThread(&env, nullptr);  // the render thread stays attached for its life
  return env;
}

void show_keyboard(const std::string &text) {
  JNIEnv *env = thread_env();
  jstring s = env->NewStringUTF(text.c_str());
  env->CallStaticVoidMethod(g_native, g_show_keyboard, s);
  env->DeleteLocalRef(s);
}

void hide_keyboard() { thread_env()->CallStaticVoidMethod(g_native, g_hide_keyboard); }
void pick_image() { thread_env()->CallStaticVoidMethod(g_native, g_pick_image); }
void first_frame() { thread_env()->CallStaticVoidMethod(g_native, g_first_frame); }

void fatal(const std::string &message) {
  std::fprintf(stderr, "mcfm: %s\n", message.c_str());
  JNIEnv *env = thread_env();
  jstring s = env->NewStringUTF(message.c_str());
  env->CallStaticVoidMethod(g_native, g_fatal, s);
  env->DeleteLocalRef(s);
}

// An app's stdout and stderr go nowhere: the launcher's and the game's lines (mcfm: …) go to
// logcat instead, tag "mcfm", one entry per line.
void forward_output_to_logcat() {
  int fds[2];
  if (pipe(fds) != 0) return;
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  dup2(fds[1], STDOUT_FILENO);
  dup2(fds[1], STDERR_FILENO);
  close(fds[1]);
  std::thread([](int fd) {
    std::string line;
    char buf[1024];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
      for (ssize_t i = 0; i < n; i++) {
        if (buf[i] != '\n') { line += buf[i]; continue; }
        __android_log_write(ANDROID_LOG_INFO, "mcfm", line.c_str());
        line.clear();
      }
    }
  }, fds[0]).detach();
}

void thread_exit() {
  JNIEnv *env = nullptr;
  if (g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_OK) g_vm->DetachCurrentThread();
}

void push(mcfm::android::EventType type, int a = 0, int b = 0, float x = 0, float y = 0) {
  mcfm::android::Event e;
  e.type = type;
  e.a = a;
  e.b = b;
  e.x = x;
  e.y = y;
  mcfm::android::push_event(e);
}

}  // namespace

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
  g_vm = vm;
  JNIEnv *env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
  jclass c = env->FindClass("io/github/d4yvid/mcfm/Native");
  if (!c) {  // loaded by something else than the app (mcfm-run): no Java side
    env->ExceptionClear();
    return JNI_VERSION_1_6;
  }
  forward_output_to_logcat();
  g_native = static_cast<jclass>(env->NewGlobalRef(c));
  g_show_keyboard = env->GetStaticMethodID(c, "showKeyboard", "(Ljava/lang/String;)V");
  g_hide_keyboard = env->GetStaticMethodID(c, "hideKeyboard", "()V");
  g_fatal = env->GetStaticMethodID(c, "fatal", "(Ljava/lang/String;)V");
  g_pick_image = env->GetStaticMethodID(c, "pickImage", "()V");
  g_first_frame = env->GetStaticMethodID(c, "firstFrame", "()V");
  return JNI_VERSION_1_6;
}

// Starts the game's render thread (once). It draws when a window is set.
JNIEXPORT void JNICALL Java_io_github_d4yvid_mcfm_Native_nativeStart(JNIEnv *env, jclass, jstring image, jstring data, jstring home) {
  mcfm::android::GamePaths paths{from_java(env, image), from_java(env, data), from_java(env, home)};
  mcfm::android::start_game(paths, mcfm::android::AppCallbacks{&show_keyboard, &hide_keyboard, &fatal, &thread_exit, &first_frame, &pick_image});
}

// surfaceChanged (a Surface) / surfaceDestroyed (null): returns once the game uses / let go of it.
JNIEXPORT void JNICALL Java_io_github_d4yvid_mcfm_Native_nativeSurface(JNIEnv *env, jclass, jobject surface, jint width, jint height) {
  ANativeWindow *window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
  mcfm::android::set_window(window, width, height);
  if (window) ANativeWindow_release(window);  // set_window took its own reference
}

// onPause (true: returns after the game saved) / onResume (false).
JNIEXPORT void JNICALL Java_io_github_d4yvid_mcfm_Native_nativePause(JNIEnv *, jclass, jboolean paused) {
  mcfm::android::set_paused(paused == JNI_TRUE);
}

JNIEXPORT void JNICALL Java_io_github_d4yvid_mcfm_Native_nativeFocus(JNIEnv *, jclass, jboolean focused) {
  mcfm::android::set_focus(focused == JNI_TRUE);
}

// action: 0 down, 1 move, 2 up, 3 cancel; x/y in surface pixels.
JNIEXPORT void JNICALL Java_io_github_d4yvid_mcfm_Native_nativeTouch(JNIEnv *, jclass, jint action, jint pointer, jfloat x, jfloat y) {
  push(mcfm::android::EventType::Touch, action, pointer, x, y);
}

// An Android key code; false when the game has no key for it (the app may handle it).
JNIEXPORT jboolean JNICALL Java_io_github_d4yvid_mcfm_Native_nativeKey(JNIEnv *, jclass, jint keycode, jboolean down) {
  int vk = mcfm::android::android_key_to_vk(keycode);
  if (!vk) return JNI_FALSE;
  push(mcfm::android::EventType::Key, vk, down == JNI_TRUE ? 1 : 0);
  return JNI_TRUE;
}

// A mouse: kind 0 button (a = button 1 left / 2 right / 3 middle, b = down), 1 move, 2 wheel (a = notches).
JNIEXPORT void JNICALL Java_io_github_d4yvid_mcfm_Native_nativeMouse(JNIEnv *, jclass, jint kind, jint a, jint b, jfloat x, jfloat y) {
  using mcfm::android::EventType;
  push(kind == 0 ? EventType::MouseButton : kind == 1 ? EventType::MouseMove : EventType::MouseWheel, a, b, x, y);
}

// Text from the soft keyboard: committed text, a backspace, or return.
JNIEXPORT void JNICALL Java_io_github_d4yvid_mcfm_Native_nativeText(JNIEnv *env, jclass, jstring text) {
  mcfm::android::Event e;
  e.type = mcfm::android::EventType::Text;
  e.text = from_java(env, text);
  mcfm::android::push_event(e);
}

JNIEXPORT void JNICALL Java_io_github_d4yvid_mcfm_Native_nativeBackspace(JNIEnv *, jclass) {
  push(mcfm::android::EventType::Backspace);
}

// Return: the newline for the text box; press_enter (the soft keyboard's return, as iOS's
// textViewShouldReturn) also presses Enter, which ends editing. A hardware Enter sends its own key.
JNIEXPORT void JNICALL Java_io_github_d4yvid_mcfm_Native_nativeReturn(JNIEnv *, jclass, jboolean press_enter) {
  push(mcfm::android::EventType::Return);
  if (press_enter == JNI_TRUE) {
    push(mcfm::android::EventType::Key, 0x0D, 1);
    push(mcfm::android::EventType::Key, 0x0D, 0);
  }
}

// The image picker's answer: the path of a PNG, or null (cancelled).
JNIEXPORT void JNICALL Java_io_github_d4yvid_mcfm_Native_nativeImagePicked(JNIEnv *env, jclass, jstring png_path) {
  mcfm::android::Event e;
  e.type = mcfm::android::EventType::ImagePicked;
  e.text = from_java(env, png_path);
  mcfm::android::push_event(e);
}

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
