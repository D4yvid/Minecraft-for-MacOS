#include "input.h"

#include <mcfm/input/input_state.h>
#include <mcfm/input/keymap.h>
#include <mcfm/keyboard_mouse.h>

#import <GameController/GameController.h>
#import <QuartzCore/QuartzCore.h>
#include <ApplicationServices/ApplicationServices.h>

#include "app_platform.h"
#include "mac_keymap.h"
#include "mouse_math.h"
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
HotbarScroll g_scroll;
LookConverter g_look(1.0);      // AppKit deltas; scale set in install()
LookConverter g_raw_look(1.0);  // GCMouse deltas (raw, unaccelerated)
LookSource g_look_source;
NSHashTable *g_mice = nil;      // GCMouse devices with a handler installed
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
    // The engine still has the pre-capture position; give it where the cursor is now.
    NSPoint p = [g_view convertPoint:[g_view.window mouseLocationOutsideOfEventStream] fromView:nil];
    view_to_pixels(p.x, p.y, g_view.bounds.size.height, g_view.window.backingScaleFactor, &g_x, &g_y);
    kbm::mouse_move_abs(g_x, g_y);
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
      const char *utf8 = [[chars substringWithRange:r] UTF8String];  // NULL for an unpaired surrogate
      if (utf8) push_text(g_text_queue, utf8);
      i = NSMaxRange(r) - 1;
    }
  }
}

// Raw look motion: GameController reports the mouse's own counts, without the system's
// pointer acceleration (fast flicks must not turn further than slow ones of the same length).
void paste_text() {  // Cmd-V in a text box
  NSString *text = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
  const char *utf8 = text.UTF8String;
  if (utf8) push_text(g_text_queue, utf8);
}

void attach_mouse(GCMouse *mouse) {
  if (!mouse || [g_mice containsObject:mouse]) return;
  [g_mice addObject:mouse];
  mouse.handlerQueue = dispatch_get_main_queue();  // the engine's input queues are main-thread only
  mouse.mouseInput.mouseMovedHandler = ^(GCMouseInput *, float dx, float dy) {
    g_look_source.raw_seen(CACurrentMediaTime());
    if (!g_captured) return;
    int ox = 0, oy = 0;
    g_raw_look.feed_raw(dx, dy, &ox, &oy);
    if (ox || oy) kbm::mouse_move_rel(ox, oy);
  };
}

}  // namespace

void install(NSView *view, void **vtable, const InputAddresses &a) {
  g_view = view;
  g_text_queue = a.keyboard_text;
  // Look speed: AppKit points -> pixels; MCFM_LOOK_SCALE multiplies it (default 1).
  const char *scale = getenv("MCFM_LOOK_SCALE");
  double factor = scale ? atof(scale) : 1.0;
  g_look.set_scale(view.window.backingScaleFactor * (factor > 0 ? factor : 1.0));
  g_raw_look.set_scale(factor > 0 ? factor : 1.0);
  g_mice = [NSHashTable weakObjectsHashTable];
  for (GCMouse *m in GCMouse.mice) attach_mouse(m);
  [NSNotificationCenter.defaultCenter addObserverForName:GCMouseDidConnectNotification object:nil
                                                   queue:NSOperationQueue.mainQueue
                                              usingBlock:^(NSNotification *n) { attach_mouse(n.object); }];
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
  if (down && (e.modifierFlags & NSEventModifierFlagCommand)) {
    // Command shortcuts belong to the app (menu) or the text box, never to the game.
    if (g_text_mode && [e.charactersIgnoringModifiers isEqualToString:@"v"]) paste_text();
    return;
  }
  if (g_text_mode && down && e.isARepeat && vk && g_keys.held[vk]) return;  // still held from before the box opened
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
  // AppKit swallows key-ups released while Command is held: on Command release, free every
  // held key except modifiers (a real press goes through again afterwards).
  if (vk == 91 && !down)
    g_keys.release_if([](int c) { return !mcfm::is_modifier_vk(c); }, [](int c) { kbm::key(c, false); });
}

void mouse_event(NSEvent *e) {
  if (g_captured && (e.type == NSEventTypeMouseMoved || e.type == NSEventTypeLeftMouseDragged ||
                     e.type == NSEventTypeRightMouseDragged || e.type == NSEventTypeOtherMouseDragged)) {
    // Captured: look motion. Raw from GCMouse once it reports (no pointer acceleration);
    // AppKit's accelerated deltas only until then.
    if (g_look_source.use_appkit_delta(CACurrentMediaTime())) {
      int dx = 0, dy = 0;
      g_look.feed(e.deltaX, e.deltaY, &dx, &dy);
      if (dx || dy) kbm::mouse_move_rel(dx, dy);
    }
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
  if (e.momentumPhase != NSEventPhaseNone) return;  // trackpad momentum: no hotbar spinning
  double dy = e.scrollingDeltaY;
  // A wheel turns the hotbar the same way whatever "natural scrolling" says (as on Windows).
  if (!e.hasPreciseScrollingDeltas && e.isDirectionInvertedFromDevice) dy = -dy;
  int notches = g_scroll.feed(dy, e.hasPreciseScrollingDeltas, CACurrentMediaTime());
  if (notches) kbm::mouse_wheel(notches, g_x, g_y);
}

void window_became_key() { apply_capture(); }

void set_backing_scale(double scale) {
  const char *s = getenv("MCFM_LOOK_SCALE");
  double factor = s ? atof(s) : 1.0;
  g_look.set_scale(scale * (factor > 0 ? factor : 1.0));
}

void focus_lost() {
  g_keys.release_all([](int vk) { kbm::key(vk, false); });
  g_buttons.release_all([](int b) { kbm::mouse_button(b, false, g_x, g_y); });
  apply_capture();
}

}  // namespace input
}  // namespace launcher
}  // namespace mcfm
