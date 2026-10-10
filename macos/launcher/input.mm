#include "input.h"

#include <mcfm/input/input_state.h>
#include <mcfm/input/keymap.h>
#include <mcfm/keyboard_mouse.h>

#import <QuartzCore/QuartzCore.h>
#include <ApplicationServices/ApplicationServices.h>

#include "app_platform.h"
#include "mac_keymap.h"
#include "resize_math.h"
#include "text_input.h"

namespace mcfm {
namespace launcher {
namespace input {
namespace {

namespace kbm = mcfm::keyboard_mouse;
__weak NSView *g_view = nil;
LauncherPlatform *g_platform = nullptr;
uintptr_t g_text_queue = 0;
bool g_text_mode = false, g_capture_wanted = false, g_captured = false;
mcfm::HeldSet g_keys, g_buttons;
mcfm::ScrollAccumulator g_scroll{1.0f};
int g_x = 0, g_y = 0;

void apply_capture() {
  bool want = g_capture_wanted && NSApp.isActive && g_view.window.isKeyWindow;
  if (want == g_captured) return;
  g_captured = want;
  CGAssociateMouseAndMouseCursorPosition(want ? false : true);
  if (want) {
    [NSCursor hide];
    NSRect f = [g_view.window convertRectToScreen:[g_view convertRect:g_view.bounds toView:nil]];
    CGFloat top = NSScreen.screens.firstObject.frame.size.height;  // CG uses top-left origin
    CGWarpMouseCursorPosition(CGPointMake(NSMidX(f), top - NSMidY(f)));
  } else {
    [NSCursor unhide];
  }
}

void key(int vk, bool down) {
  if (g_text_mode && down && !mcfm::passes_while_typing(vk)) return;  // presses belong to the text box
  if (g_keys.set(vk, down)) kbm::key(vk, down);
}

void button(int btn, bool down) {
  if (g_buttons.set(btn, down)) kbm::mouse_button(btn, down, g_x, g_y);
}

void update_position(NSEvent *e) {
  NSPoint p = [g_view convertPoint:e.locationInWindow fromView:nil];
  view_to_pixels(p.x, p.y, g_view.bounds.size.height, g_view.window.backingScaleFactor, &g_x, &g_y);
}

void type_text(NSEvent *e) {  // text mode: characters go to the engine's text queue
  NSString *chars = e.characters;
  for (NSUInteger i = 0; i < chars.length; i++) {
    unichar c = [chars characterAtIndex:i];
    if (c == 0x7F || c == 0x08) push_backspace(g_text_queue);
    else if (c == '\r' || c == '\n' || c == 0x03) push_return(g_text_queue);
    else if (c >= 0xF700 && c <= 0xF8FF) continue;  // function/arrow keys: no text
    else if (c < 0x20) continue;
    else {
      NSRange r = [chars rangeOfComposedCharacterSequenceAtIndex:i];
      push_text(g_text_queue, [[chars substringWithRange:r] UTF8String]);
      i = NSMaxRange(r) - 1;
    }
  }
}

}  // namespace

void install(NSView *view, void **vtable, const InputAddresses &a) {
  g_view = view;
  g_text_queue = a.keyboard_text;
  g_platform = new LauncherPlatform(vtable, a);
  kbm::PointerCallbacks pc = {[] { g_capture_wanted = true; apply_capture(); },
                              [] { g_capture_wanted = false; apply_capture(); }};
  kbm::install(*g_platform, pc);
  KeyboardCallbacks kc = {[](const std::string &) { g_text_mode = true; }, [] { g_text_mode = false; }};
  set_keyboard_callbacks(kc);
  NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
  [nc addObserverForName:NSApplicationDidResignActiveNotification object:nil queue:NSOperationQueue.mainQueue
              usingBlock:^(NSNotification *) { focus_lost(); }];
  [nc addObserverForName:NSApplicationDidBecomeActiveNotification object:nil queue:NSOperationQueue.mainQueue
              usingBlock:^(NSNotification *) { apply_capture(); }];
}

void key_event(NSEvent *e) {
  bool down = e.type == NSEventTypeKeyDown;
  int vk = mac_keycode_to_vk(e.keyCode);
  if (g_text_mode && down && vk != 27) {
    type_text(e);
    return;
  }
  if (down && e.isARepeat) return;  // the engine has its own key repeat
  if (vk) key(vk, down);
}

void flags_changed(NSEvent *e) {
  int vk = mac_keycode_to_vk(e.keyCode);
  if (!vk) return;
  NSEventModifierFlags f = e.modifierFlags;
  bool down = (vk == 16 && (f & NSEventModifierFlagShift)) || (vk == 17 && (f & NSEventModifierFlagControl)) ||
              (vk == 18 && (f & NSEventModifierFlagOption)) || (vk == 91 && (f & NSEventModifierFlagCommand));
  key(vk, down);
}

void mouse_event(NSEvent *e) {
  if (g_captured && (e.type == NSEventTypeMouseMoved || e.type == NSEventTypeLeftMouseDragged ||
                     e.type == NSEventTypeRightMouseDragged || e.type == NSEventTypeOtherMouseDragged)) {
    kbm::mouse_move_rel(static_cast<int>(e.deltaX), static_cast<int>(e.deltaY));
  } else {
    update_position(e);
    kbm::mouse_move_abs(g_x, g_y);
  }
  switch (e.type) {
    case NSEventTypeLeftMouseDown: button(1, true); break;
    case NSEventTypeLeftMouseUp: button(1, false); break;
    case NSEventTypeRightMouseDown: button(2, true); break;
    case NSEventTypeRightMouseUp: button(2, false); break;
    case NSEventTypeOtherMouseDown: button(3, true); break;
    case NSEventTypeOtherMouseUp: button(3, false); break;
    default: break;
  }
}

void scroll_event(NSEvent *e) {
  int notches = g_scroll.feed(static_cast<float>(e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / 10.0 : e.scrollingDeltaY),
                              CACurrentMediaTime());
  if (notches) kbm::mouse_wheel(notches, g_x, g_y);
}

void focus_lost() {
  g_keys.release_all([](int vk) { kbm::key(vk, false); });
  g_buttons.release_all([](int b) { kbm::mouse_button(b, false, g_x, g_y); });
  apply_capture();
}

}  // namespace input
}  // namespace launcher
}  // namespace mcfm
