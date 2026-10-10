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
};

// Starts the render thread once; later calls are ignored.
void start_game(const GamePaths &paths, const AppCallbacks &callbacks);

// The window to draw into (null: none). Returns once the thread uses it, or has let go of the
// previous one (Android destroys the window right after surfaceDestroyed returns).
void set_window(ANativeWindow *window, int width, int height);

// Activity paused (true: the game saves before this returns) or resumed; window focus.
void set_paused(bool paused);
void set_focus(bool focused);

// Input for the next frame.
void push_event(const Event &event);

}  // namespace android
}  // namespace mcfm
