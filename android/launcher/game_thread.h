#pragma once
// The game's render thread in the Android app (docs/LAUNCHER.md, Stage 3c). The UI thread (JNI)
// hands it the window, lifecycle changes and input; only this thread touches the engine and GL.
#include <android/native_window.h>

#include <string>

#include "input_events.h"

namespace mcfm {
namespace android {

struct GamePaths {
  std::string image;  // the converted game (filesDir/game/minecraftpe.dylib)
  std::string data;   // the game's data/ (filesDir/game/data/)
  std::string home;   // worlds and options (<home>/games/com.mojang)
};

// Callbacks into the app (called on the render thread).
struct AppCallbacks {
  void (*show_keyboard)(const std::string &initial_text);
  void (*hide_keyboard)();
  void (*fatal)(const std::string &message);  // the game cannot run: show the message, finish
  void (*thread_exit)();                       // last call on the render thread (JVM detach)
  void (*first_frame)();                       // the first frame is on screen (the splash goes)
  void (*pick_image)();                        // show the image picker (the skin screen); the
                                               // answer comes back as an ImagePicked event
};

// Starts the render thread once; later calls are ignored.
void start_game(const GamePaths &paths, const AppCallbacks &callbacks);

// The window to draw into, from surfaceChanged; returns at once. Null (surfaceDestroyed): returns
// once the thread has let go of the window it draws into (Android destroys it right after).
void set_window(ANativeWindow *window, int width, int height);

// Activity paused (true: once the game runs, returns after it saved) or resumed (returns at
// once: the first load takes seconds); window focus. See render_requests.h.
void set_paused(bool paused);
void set_focus(bool focused);

// Input for the next frame.
void push_event(const Event &event);

}  // namespace android
}  // namespace mcfm
