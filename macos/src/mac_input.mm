#include "mac_input.h"
#include <mcfm/keyboard_mouse.h>
#include "input_policy.h"
#include <mcfm/input/input_state.h>
#include <mcfm/input/keymap.h>
#include "pointer_lock.h"
#include "titlebar.h"

#import <GameController/GameController.h>
#import <UIKit/UIKit.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <cstdlib>
#include <map>
#import <QuartzCore/QuartzCore.h>

namespace macin {
namespace {

constexpr float kLookScale = 1.0f;  // tune with manual check M3
bool gTextActive = false;
bool gMouseConnected = false;
mcfm::HeldSet gKeys, gButtons;
mcfm::DeltaAccumulator gLook;
Class gPressesSuper = Nil;  // UIViewController, captured once (KVO-safe)
NSHashTable *gAttached;  // devices whose handlers are installed
int gX = 0, gY = 0;  // last pointer position in game pixels
bool gInside = false;  // free cursor is over the game view
double gXPt = -1, gYPt = -1;  // last pointer position in window points
__weak UIView *gView = nil;
__weak UIResponder *gVC = nil;
mcfm::ScrollAccumulator gScroll{1.0f};
std::map<SEL, IMP> *gOrig;
namespace kbm = mcfm::keyboard_mouse;

float view_scale() {
  if (!gVC) return 1.0f;
  Ivar iv = class_getInstanceVariable(object_getClass(gVC), "viewScale");
  float s = iv ? *(float *)((uint8_t *)(__bridge void *)gVC + ivar_getOffset(iv)) : 1.0f;
  return s > 0 ? s : 1.0f;
}

bool feeds(mcfm::PointerEvent e) {
  return mcfm::should_feed(e, mcfm::PointerState{pl::captured(), gInside, gXPt, gYPt});
}

void pointer_moved(CGPoint p) {
  gXPt = p.x;
  gYPt = p.y;
  float s = view_scale();
  gX = (int)(p.x * s);
  gY = (int)(p.y * s);
  if (feeds(mcfm::PointerEvent::Move)) kbm::mouse_move_abs(gX, gY);
  if (!pl::captured()) titlebar::pointer_at(p.x, p.y);
}

void button(int btn, BOOL pressed) {
  if (!feeds(pressed ? mcfm::PointerEvent::ButtonDown : mcfm::PointerEvent::ButtonUp)) return;
  if (gButtons.set(btn, pressed)) kbm::mouse_button(btn, pressed, gX, gY);
}

void key(int vk, bool down) {
  // While typing, presses belong to the text field (except Esc); releases still go through.
  if (gTextActive && down && !mcfm::passes_while_typing(vk)) return;
  if (gKeys.set(vk, down)) kbm::key(vk, down);
}

void release_all() {
  gKeys.release_all([](int vk) { kbm::key(vk, false); });
  gButtons.release_all([](int btn) { kbm::mouse_button(btn, false, gX, gY); });
}

bool claim(id device) {  // true the first time a device is seen
  if ([gAttached containsObject:device]) return false;
  [gAttached addObject:device];
  return true;
}

void attach_keyboard(GCKeyboard *kb) {
  if (!kb || !claim(kb)) return;
  kb.keyboardInput.keyChangedHandler = ^(GCKeyboardInput *, GCControllerButtonInput *, GCKeyCode code, BOOL pressed) {
    key(mcfm::hid_to_vk((long)code), pressed);
  };
}

void attach_mouse(GCMouse *mouse) {
  if (!mouse || !claim(mouse)) return;
  gMouseConnected = true;
  GCMouseInput *in = mouse.mouseInput;
  in.mouseMovedHandler = ^(GCMouseInput *, float dx, float dy) {
    if (!pl::captured()) return;
    // GameController deltaY is positive upwards; the engine expects screen space.
    int ox, oy;
    gLook.add(dx * kLookScale, -dy * kLookScale, &ox, &oy);
    kbm::mouse_move_rel(ox, oy);
  };
  in.leftButton.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL p) { button(1, p); };
  in.rightButton.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL p) { button(2, p); };
  in.middleButton.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL p) { button(3, p); };
  in.scroll.yAxis.valueChangedHandler = ^(GCControllerAxisInput *, float v) {
    if (getenv("MCPEKBM_LOG_SCROLL")) NSLog(@"mcfm: scroll %f", v);
    int notches = gScroll.feed(v, CACurrentMediaTime());
    if (notches && feeds(mcfm::PointerEvent::Scroll)) kbm::mouse_wheel(notches, gX, gY);
  };
  NSLog(@"mcfm: mouse connected");
}

// --- swizzles on minecraftpeViewController ---

void touches_gate(id self, SEL cmd, NSSet *touches, UIEvent *event) {
  if (gMouseConnected) {  // clicks come from GCMouse; don't double-feed
    // Hover stops while a button is held, so drags take positions from the touch stream.
    if (cmd == @selector(touchesMoved:withEvent:))
      pointer_moved([touches.anyObject locationInView:((UIViewController *)self).view]);
    return;
  }
  ((void (*)(id, SEL, NSSet *, UIEvent *))(*gOrig)[cmd])(self, cmd, touches, event);
}

BOOL can_become_first_responder(id, SEL) { return YES; }

void presses_swallow(id self, SEL cmd, NSSet *presses, UIPressesEvent *event) {
  if (gTextActive) {  // let UIKit deliver to the text field normally
    struct objc_super sup = {self, gPressesSuper};
    ((void (*)(struct objc_super *, SEL, NSSet *, UIPressesEvent *))objc_msgSendSuper)(&sup, cmd, presses, event);
  }
  // otherwise swallow: GCKeyboard already fed the engine; prevents the macOS beep
}

void hover(id, SEL, UIHoverGestureRecognizer *g) {
  if (g.state == UIGestureRecognizerStateEnded || g.state == UIGestureRecognizerStateCancelled) {
    CGSize size = g.view.bounds.size;
    if (mcfm::hover_end_is_exit(gXPt, gYPt, size.width, size.height)) {
      gInside = false;
      titlebar::pointer_at(-1, -1);
    }
    return;
  }
  gInside = true;
  pointer_moved([g locationInView:g.view]);
}

void swizzle(Class c, SEL sel, IMP imp) {
  Method m = class_getInstanceMethod(c, sel);
  (*gOrig)[sel] = method_setImplementation(m, imp);
}

void add_or_replace(Class c, SEL sel, IMP imp, const char *types) {
  if (!class_addMethod(c, sel, imp, types)) class_replaceMethod(c, sel, imp, types);
}

}  // namespace

void install() {
  gOrig = new std::map<SEL, IMP>();
  gAttached = [NSHashTable weakObjectsHashTable];
  Class vc = objc_getClass("minecraftpeViewController");
  gPressesSuper = class_getSuperclass(vc);
  swizzle(vc, @selector(touchesBegan:withEvent:), (IMP)touches_gate);
  swizzle(vc, @selector(touchesMoved:withEvent:), (IMP)touches_gate);
  swizzle(vc, @selector(touchesEnded:withEvent:), (IMP)touches_gate);
  swizzle(vc, @selector(touchesCancelled:withEvent:), (IMP)touches_gate);
  add_or_replace(vc, @selector(canBecomeFirstResponder), (IMP)can_become_first_responder, "B16@0:8");
  for (SEL s : {@selector(pressesBegan:withEvent:), @selector(pressesEnded:withEvent:),
                @selector(pressesChanged:withEvent:), @selector(pressesCancelled:withEvent:)})
    add_or_replace(vc, s, (IMP)presses_swallow, "v32@0:8@16@24");
  class_addMethod(vc, @selector(mcpekbm_hover:), (IMP)hover, "v24@0:8@16");

  NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
  NSOperationQueue *main = NSOperationQueue.mainQueue;
  for (NSNotificationName n : {UITextFieldTextDidBeginEditingNotification, UITextViewTextDidBeginEditingNotification})
    [nc addObserverForName:n object:nil queue:main usingBlock:^(NSNotification *) {
      gTextActive = true;
      gKeys.release_all([](int vk) { kbm::key(vk, false); });
    }];
  for (NSNotificationName n : {UITextFieldTextDidEndEditingNotification, UITextViewTextDidEndEditingNotification})
    [nc addObserverForName:n object:nil queue:main usingBlock:^(NSNotification *) {
      gTextActive = false;
      [gVC becomeFirstResponder];
    }];
  [nc addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:main
              usingBlock:^(NSNotification *) { release_all(); }];
  [nc addObserverForName:GCKeyboardDidConnectNotification object:nil queue:main
              usingBlock:^(NSNotification *n) { attach_keyboard(n.object); }];
  [nc addObserverForName:GCMouseDidConnectNotification object:nil queue:main
              usingBlock:^(NSNotification *n) { attach_mouse(n.object); }];
  [nc addObserverForName:GCMouseDidDisconnectNotification object:nil queue:main
              usingBlock:^(NSNotification *n) {
                bool any = false;
                for (GCMouse *m in GCMouse.mice) any |= (m != n.object);
                gMouseConnected = any;  // no mouse left: the game's touch path takes over again
                NSLog(@"mcfm: mouse disconnected (%s left)", any ? "others" : "none");
              }];
  dispatch_async(dispatch_get_main_queue(), ^{
    if (GCKeyboard.coalescedKeyboard) attach_keyboard(GCKeyboard.coalescedKeyboard);
    for (GCMouse *m in GCMouse.mice) attach_mouse(m);
  });
  NSLog(@"mcfm: input ready");
}

void attach_view(void *uiView) {
  UIView *view = (__bridge UIView *)uiView;
  if (gView == view) return;
  gView = view;
  UIResponder *next = view.nextResponder;
  if (![next isKindOfClass:objc_getClass("minecraftpeViewController")]) return;
  gVC = next;
  [view addGestureRecognizer:[[UIHoverGestureRecognizer alloc] initWithTarget:next action:@selector(mcpekbm_hover:)]];
  [next becomeFirstResponder];
}

}  // namespace macin
