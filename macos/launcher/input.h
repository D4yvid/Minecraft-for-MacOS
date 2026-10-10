#pragma once
#import <AppKit/AppKit.h>
#include "launcher_platform.h"

namespace mcfm {
namespace launcher {
namespace input {
void install(NSView *view, void **vtable, const InputAddresses &addresses);
void key_event(NSEvent *e);      // keyDown / keyUp
void flags_changed(NSEvent *e);  // modifier keys
void mouse_event(NSEvent *e);    // move, drag, buttons
void scroll_event(NSEvent *e);
void focus_lost();               // app/window resigned: release keys, buttons, pointer
}  // namespace input
}  // namespace launcher
}  // namespace mcfm
