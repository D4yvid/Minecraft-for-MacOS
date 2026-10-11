#pragma once
// The iOS app's launcher (docs/LAUNCHER.md, Stage 4), called by the Swift app (ios/app) on the
// main thread, which is also the engine's thread. C API (the Swift bridging header imports it).
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void (*show_keyboard)(void);         // the engine opened a text box
  void (*hide_keyboard)(void);
  void (*pick_image)(void);            // "Choose New Skin": answer with mcfm_ios_image_picked
  void (*fatal)(const char *message);  // the game cannot run
} McfmIosCallbacks;

// Loads the game image (dyld, hooks) and starts the engine at this drawable size. 1 on success;
// 0 after callbacks.fatal(message).
int mcfm_ios_start(const char *image_path, const char *data_dir, const char *home_dir,
                   int width_px, int height_px, McfmIosCallbacks callbacks);
void mcfm_ios_frame(void);  // one engine frame; the caller binds its framebuffer first
void mcfm_ios_resize(int width_px, int height_px);
void mcfm_ios_pause(int paused);  // 1: suspend (the game saves before this returns); 0: resume
void mcfm_ios_focus(int focused);

// action: 0 down, 1 move, 2 up, 3 cancel; pointer: a small id per finger; pixels.
void mcfm_ios_touch(int action, int pointer, float x_px, float y_px);
void mcfm_ios_key(int hid_usage, int down);  // a hardware key (UIKey.keyCode)
void mcfm_ios_text(const char *utf8);
void mcfm_ios_backspace(void);
void mcfm_ios_return(int press_enter);  // press_enter: the soft keyboard's return (ends editing)
void mcfm_ios_image_picked(const char *png_path);  // NULL: cancelled

// The drawable size for a view of w x h points at this scale (landscape).
void mcfm_ios_pixel_size(double w_pt, double h_pt, double scale, int *w_px, int *h_px);

#ifdef __cplusplus
}
#endif
