#include "resize.h"
#include "addresses.h"
#include "mac_input.h"

#import <UIKit/UIKit.h>
#include <objc/runtime.h>

namespace resize {
namespace {

IMP gOrigLayout = nullptr;
int gLastW = 0, gLastH = 0;
bool gMinSizeSet = false;

template <class T> T *ivar_ptr(id obj, const char *name) {
  Ivar iv = class_getInstanceVariable(object_getClass(obj), name);
  return iv ? (T *)((uint8_t *)(__bridge void *)obj + ivar_getOffset(iv)) : nullptr;
}

float view_scale(id vc) {
  float *s = ivar_ptr<float>(vc, "viewScale");
  return (s && *s > 0) ? *s : 1.0f;
}

int vc_width(id self, SEL) {
  return (int)([(UIViewController *)self view].bounds.size.width * view_scale(self));
}

int vc_height(id self, SEL) {
  return (int)([(UIViewController *)self view].bounds.size.height * view_scale(self));
}

void set_min_window_size() {
  if (gMinSizeSet) return;
  for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
    if ([scene isKindOfClass:UIWindowScene.class]) {
      ((UIWindowScene *)scene).sizeRestrictions.minimumSize = CGSizeMake(800, 500);
      gMinSizeSet = true;
    }
  }
}

void notify_engine(UIView *view) {
  UIResponder *next = view.nextResponder;
  Class vcClass = objc_getClass("minecraftpeViewController");
  if (!next || ![next isKindOfClass:vcClass]) return;
  void **app = ivar_ptr<void *>(next, "_app");
  if (!app || !*app) return;  // before initView
  int w = vc_width(next, nullptr), h = vc_height(next, nullptr);
  if (w <= 0 || h <= 0 || (w == gLastW && h == gLastH)) return;
  gLastW = w;
  gLastH = h;
  void **vt = *(void ***)*app;
  ((void (*)(void *, int, int))vt[addr::kAppSlotSetSize])(*app, w, h);
  ((void (*)(void *, int, int, float))vt[addr::kAppSlotSetSizeAndScale])(*app, w, h, 0.0f);
  NSLog(@"mcpekbm: engine size %dx%d", w, h);
}

void hooked_layout(UIView *self, SEL cmd) {
  ((void (*)(id, SEL))gOrigLayout)(self, cmd);
  set_min_window_size();
  macin::attach_view((__bridge void *)self);
  notify_engine(self);
}

}  // namespace

void install() {
  Class vc = objc_getClass("minecraftpeViewController");
  Class gl = objc_getClass("EAGLView");
  method_setImplementation(class_getInstanceMethod(vc, @selector(width)), (IMP)vc_width);
  method_setImplementation(class_getInstanceMethod(vc, @selector(height)), (IMP)vc_height);
  Method m = class_getInstanceMethod(gl, @selector(layoutSubviews));
  gOrigLayout = method_setImplementation(m, (IMP)hooked_layout);
}

}  // namespace resize
