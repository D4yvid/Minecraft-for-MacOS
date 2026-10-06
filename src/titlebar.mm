#include "titlebar.h"
#include "titlebar_zone.h"

#import <UIKit/UIKit.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <initializer_list>

// Catalyst has no AppKit headers, so the NSWindow is driven through the ObjC runtime.
namespace titlebar {
namespace {

constexpr NSUInteger kTitled = 1 << 0;                // NSWindowStyleMaskTitled
constexpr NSUInteger kFullSizeContentView = 1 << 15;  // NSWindowStyleMaskFullSizeContentView
constexpr NSInteger kTitleVisible = 0;                // NSWindowTitleVisible

__weak id gWindow = nil;
mcpekbm::TitlebarReveal gReveal;

id ns_window() {
  Class appClass = objc_getClass("NSApplication");
  if (!appClass) return nil;
  id app = ((id (*)(Class, SEL))objc_msgSend)(appClass, sel_registerName("sharedApplication"));
  // The game has one titled window; skip panels, sheets and other untitled helpers.
  auto titled = [](id w) {
    if (!w) return false;
    NSUInteger mask = ((NSUInteger (*)(id, SEL))objc_msgSend)(w, sel_registerName("styleMask"));
    return (mask & kTitled) != 0 && ((BOOL (*)(id, SEL))objc_msgSend)(w, sel_registerName("isVisible"));
  };
  for (const char *sel : {"mainWindow", "keyWindow"}) {
    id w = ((id (*)(id, SEL))objc_msgSend)(app, sel_registerName(sel));
    if (titled(w)) return w;
  }
  NSArray *windows = ((NSArray * (*)(id, SEL)) objc_msgSend)(app, sel_registerName("windows"));
  for (id w in windows)
    if (titled(w)) return w;
  return nil;
}

constexpr double kFadeSeconds = 0.2;

// The view holding the whole title bar (background, title, traffic lights):
// close button -> NSTitlebarView -> NSTitlebarContainerView.
id bar_view(id w) {
  id close = ((id (*)(id, SEL, NSInteger))objc_msgSend)(w, sel_registerName("standardWindowButton:"), 0);
  id titlebar = ((id (*)(id, SEL))objc_msgSend)(close, sel_registerName("superview"));
  id container = ((id (*)(id, SEL))objc_msgSend)(titlebar, sel_registerName("superview"));
  return container ?: titlebar;
}

void set_hidden(id view, bool hidden) {
  ((void (*)(id, SEL, BOOL))objc_msgSend)(view, sel_registerName("setHidden:"), hidden);
}

void set_alpha(id view, CGFloat alpha) {
  ((void (*)(id, SEL, CGFloat))objc_msgSend)(view, sel_registerName("setAlphaValue:"), alpha);
}

CGFloat alpha_of(id view) { return ((CGFloat (*)(id, SEL))objc_msgSend)(view, sel_registerName("alphaValue")); }

// The bar is always a normal opaque title bar; showing/hiding fades the whole of it.
// Once faded out it is also hidden, so its invisible buttons cannot be clicked.
void fade_bar(id w, bool shown, bool animated) {
  id view = bar_view(w);
  Class ctxClass = objc_getClass("NSAnimationContext");
  if (!view) return;
  if (shown) set_hidden(view, false);
  if (!animated || !ctxClass) {
    set_alpha(view, shown ? 1 : 0);
    if (!shown) set_hidden(view, true);
    return;
  }
  void (^changes)(id) = ^(id context) {
    ((void (*)(id, SEL, double))objc_msgSend)(context, sel_registerName("setDuration:"), kFadeSeconds);
    id animator = ((id (*)(id, SEL))objc_msgSend)(view, sel_registerName("animator"));
    set_alpha(animator, shown ? 1 : 0);
  };
  void (^done)(void) = ^{
    if (!gReveal.shown) set_hidden(view, true);  // a newer fade-in may have started meanwhile
  };
  ((void (*)(Class, SEL, id, id))objc_msgSend)(ctxClass, sel_registerName("runAnimationGroup:completionHandler:"),
                                              changes, done);
}

bool style_applied(id w) {
  NSUInteger mask = ((NSUInteger (*)(id, SEL))objc_msgSend)(w, sel_registerName("styleMask"));
  BOOL transparent = ((BOOL (*)(id, SEL))objc_msgSend)(w, sel_registerName("titlebarAppearsTransparent"));
  id view = bar_view(w);
  bool alphaOk = !view || (alpha_of(view) > 0.5) == gReveal.shown;
  return (mask & kFullSizeContentView) && !transparent && alphaOk;
}

}  // namespace

// Runs on every layout pass: full-screen transitions can reset the window style,
// so re-apply whenever it has drifted. Cheap when nothing changed.
void setup_window() {
  id w = gWindow ?: ns_window();
  if (!w) return;  // window not on screen yet; next layout retries
  if (w == gWindow && style_applied(w)) return;
  for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
    if (![scene isKindOfClass:UIWindowScene.class]) continue;
    UITitlebar *tb = ((UIWindowScene *)scene).titlebar;
    tb.titleVisibility = UITitlebarTitleVisibilityHidden;
    tb.toolbar = nil;
  }
  NSUInteger mask = ((NSUInteger (*)(id, SEL))objc_msgSend)(w, sel_registerName("styleMask"));
  ((void (*)(id, SEL, NSUInteger))objc_msgSend)(w, sel_registerName("setStyleMask:"), mask | kFullSizeContentView);
  ((void (*)(id, SEL, BOOL))objc_msgSend)(w, sel_registerName("setTitlebarAppearsTransparent:"), NO);
  ((void (*)(id, SEL, NSInteger))objc_msgSend)(w, sel_registerName("setTitleVisibility:"), kTitleVisible);
  bool first = !gWindow;
  gWindow = w;
  fade_bar(w, gReveal.shown, false);
  NSLog(first ? @"mcpekbm: titlebar hidden" : @"mcpekbm: titlebar style re-applied");
}

bool window_center(double *cgX, double *cgY) {
  id w = gWindow ?: ns_window();
  Class screenClass = objc_getClass("NSScreen");
  if (!w || !screenClass) return false;
  NSArray *screens = ((NSArray * (*)(Class, SEL)) objc_msgSend)(screenClass, sel_registerName("screens"));
  if (screens.count == 0) return false;
  CGRect primary = ((CGRect (*)(id, SEL))objc_msgSend)(screens[0], sel_registerName("frame"));
  CGRect f = ((CGRect (*)(id, SEL))objc_msgSend)(w, sel_registerName("frame"));
  mcpekbm::window_center_cg(f.origin.x, f.origin.y, f.size.width, f.size.height, primary.size.height, cgX, cgY);
  return true;
}

void pointer_at(double xPt, double yPt) {
  bool was = gReveal.shown;
  if (gReveal.update(xPt, yPt) != was && gWindow) fade_bar(gWindow, gReveal.shown, true);
}

}  // namespace titlebar
