#include "mac_input.h"
#include "engine.h"
#include "keymap.h"
#include "pointer_lock.h"

#import <GameController/GameController.h>
#import <UIKit/UIKit.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <map>

namespace macin {
namespace {

constexpr float kLookScale = 1.0f;  // tune with manual check M3
bool gTextActive = false;
bool gMouseConnected = false;
bool gHeld[256] = {};
int gX = 0, gY = 0;  // last pointer position in game pixels
__weak UIView *gView = nil;
__weak UIResponder *gVC = nil;
mcpekbm::ScrollAccumulator gScroll{1.0f, 0.0f};
std::map<SEL, IMP> *gOrig;

float view_scale() {
  if (!gVC) return 1.0f;
  Ivar iv = class_getInstanceVariable(object_getClass(gVC), "viewScale");
  float s = iv ? *(float *)((uint8_t *)(__bridge void *)gVC + ivar_getOffset(iv)) : 1.0f;
  return s > 0 ? s : 1.0f;
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
  in.leftButton.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL p) { eng::mouse_button(1, p, gX, gY); };
  in.rightButton.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL p) { eng::mouse_button(2, p, gX, gY); };
  in.middleButton.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL p) { eng::mouse_button(3, p, gX, gY); };
  in.scroll.yAxis.valueChangedHandler = ^(GCControllerAxisInput *, float v) {
    eng::mouse_wheel(gScroll.feed(v), gX, gY);
  };
  NSLog(@"mcpekbm: mouse connected");
}

// --- swizzles on minecraftpeViewController ---

void touches_gate(id self, SEL cmd, NSSet *touches, UIEvent *event) {
  if (gMouseConnected) return;  // clicks come from GCMouse; don't double-feed
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
  UIView *v = g.view;
  CGPoint p = [g locationInView:v];
  float s = view_scale();
  gX = (int)(p.x * s);
  gY = (int)(p.y * s);
  if (!pl::captured()) eng::mouse_move_abs(gX, gY);
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
