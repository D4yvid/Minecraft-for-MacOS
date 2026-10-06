#include "mac_input.h"
#include "engine.h"
#include "input_policy.h"
#include "keymap.h"
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
bool gHeld[256] = {};
int gX = 0, gY = 0;  // last pointer position in game pixels
bool gInside = false;  // free cursor is over the game view
double gXPt = -1, gYPt = -1;  // last pointer position in window points
__weak UIView *gView = nil;
__weak UIResponder *gVC = nil;
mcpekbm::ScrollAccumulator gScroll{1.0f};
std::map<SEL, IMP> *gOrig;

float view_scale() {
  if (!gVC) return 1.0f;
  Ivar iv = class_getInstanceVariable(object_getClass(gVC), "viewScale");
  float s = iv ? *(float *)((uint8_t *)(__bridge void *)gVC + ivar_getOffset(iv)) : 1.0f;
  return s > 0 ? s : 1.0f;
}

bool feeds(mcpekbm::PointerEvent e) {
  return mcpekbm::should_feed(e, mcpekbm::PointerState{pl::captured(), gInside, gXPt, gYPt});
}

void pointer_moved(CGPoint p) {
  gXPt = p.x;
  gYPt = p.y;
  float s = view_scale();
  gX = (int)(p.x * s);
  gY = (int)(p.y * s);
  if (feeds(mcpekbm::PointerEvent::Move)) eng::mouse_move_abs(gX, gY);
  if (!pl::captured()) titlebar::pointer_at(p.x, p.y);
}

void button(int btn, BOOL pressed) {
  if (feeds(pressed ? mcpekbm::PointerEvent::ButtonDown : mcpekbm::PointerEvent::ButtonUp))
    eng::mouse_button(btn, pressed, gX, gY);
}

void key(int vk, bool down) {
  if (vk <= 0 || vk > 255 || gHeld[vk] == down) return;
  gHeld[vk] = down;
  eng::key(vk, down);
}

void release_all_keys() {
  for (int vk = 1; vk < 256; vk++) if (gHeld[vk]) key(vk, false);
}

void attach_keyboard(GCKeyboard *kb) {
  kb.keyboardInput.keyChangedHandler = ^(GCKeyboardInput *, GCControllerButtonInput *, GCKeyCode code, BOOL pressed) {
    if (gTextActive && pressed) return;  // releases still go through so nothing sticks
    key(mcpekbm::hid_to_vk((long)code), pressed);
  };
}

void attach_mouse(GCMouse *mouse) {
  gMouseConnected = true;
  GCMouseInput *in = mouse.mouseInput;
  in.mouseMovedHandler = ^(GCMouseInput *, float dx, float dy) {
    // GameController deltaY is positive upwards; the engine expects screen space.
    if (pl::captured()) eng::mouse_move_rel((int)(dx * kLookScale), (int)(-dy * kLookScale));
  };
  in.leftButton.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL p) { button(1, p); };
  in.rightButton.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL p) { button(2, p); };
  in.middleButton.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL p) { button(3, p); };
  in.scroll.yAxis.valueChangedHandler = ^(GCControllerAxisInput *, float v) {
    if (getenv("MCPEKBM_LOG_SCROLL")) NSLog(@"mcpekbm: scroll %f", v);
    int notches = gScroll.feed(v, CACurrentMediaTime());
    if (notches && feeds(mcpekbm::PointerEvent::Scroll)) eng::mouse_wheel(notches, gX, gY);
  };
  NSLog(@"mcpekbm: mouse connected");
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
    struct objc_super sup = {self, class_getSuperclass(object_getClass(self))};
    ((void (*)(struct objc_super *, SEL, NSSet *, UIPressesEvent *))objc_msgSendSuper)(&sup, cmd, presses, event);
  }
  // otherwise swallow: GCKeyboard already fed the engine; prevents the macOS beep
}

void hover(id, SEL, UIHoverGestureRecognizer *g) {
  if (g.state == UIGestureRecognizerStateEnded || g.state == UIGestureRecognizerStateCancelled) {
    CGSize size = g.view.bounds.size;
    if (mcpekbm::hover_end_is_exit(gXPt, gYPt, size.width, size.height)) {
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
  Class vc = objc_getClass("minecraftpeViewController");
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
      release_all_keys();
    }];
  for (NSNotificationName n : {UITextFieldTextDidEndEditingNotification, UITextViewTextDidEndEditingNotification})
    [nc addObserverForName:n object:nil queue:main usingBlock:^(NSNotification *) {
      gTextActive = false;
      [gVC becomeFirstResponder];
    }];
  [nc addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:main
              usingBlock:^(NSNotification *) { release_all_keys(); }];
  [nc addObserverForName:GCKeyboardDidConnectNotification object:nil queue:main
              usingBlock:^(NSNotification *n) { attach_keyboard(n.object); }];
  [nc addObserverForName:GCMouseDidConnectNotification object:nil queue:main
              usingBlock:^(NSNotification *n) { attach_mouse(n.object); }];
  dispatch_async(dispatch_get_main_queue(), ^{
    if (GCKeyboard.coalescedKeyboard) attach_keyboard(GCKeyboard.coalescedKeyboard);
    for (GCMouse *m in GCMouse.mice) attach_mouse(m);
  });
  NSLog(@"mcpekbm: input ready");
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
