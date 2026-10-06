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
constexpr NSInteger kTitleHidden = 1;                 // NSWindowTitleHidden

__weak id gWindow = nil;
bool gShown = true;

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

void show_buttons(bool show) {
  id w = gWindow;
  if (!w || show == gShown) return;
  gShown = show;
  for (NSInteger kind = 0; kind <= 2; kind++) {  // close, miniaturize, zoom
    id button = ((id (*)(id, SEL, NSInteger))objc_msgSend)(w, sel_registerName("standardWindowButton:"), kind);
    ((void (*)(id, SEL, BOOL))objc_msgSend)(button, sel_registerName("setHidden:"), !show);
  }
}

}  // namespace

// Runs on every layout pass: full-screen transitions can reset the window style,
// so re-apply whenever it has drifted. Cheap when nothing changed.
void setup_window() {
  id w = gWindow ?: ns_window();
  if (!w) return;  // window not on screen yet; next layout retries
  NSUInteger mask = ((NSUInteger (*)(id, SEL))objc_msgSend)(w, sel_registerName("styleMask"));
  BOOL transparent = ((BOOL (*)(id, SEL))objc_msgSend)(w, sel_registerName("titlebarAppearsTransparent"));
  if (w == gWindow && (mask & kFullSizeContentView) && transparent) return;
  for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
    if (![scene isKindOfClass:UIWindowScene.class]) continue;
    UITitlebar *tb = ((UIWindowScene *)scene).titlebar;
    tb.titleVisibility = UITitlebarTitleVisibilityHidden;
    tb.toolbar = nil;
  }
  ((void (*)(id, SEL, NSUInteger))objc_msgSend)(w, sel_registerName("setStyleMask:"), mask | kFullSizeContentView);
  ((void (*)(id, SEL, BOOL))objc_msgSend)(w, sel_registerName("setTitlebarAppearsTransparent:"), YES);
  ((void (*)(id, SEL, NSInteger))objc_msgSend)(w, sel_registerName("setTitleVisibility:"), kTitleHidden);
  bool first = !gWindow;
  gWindow = w;
  gShown = true;  // AppKit may have re-shown the buttons; force them hidden again
  show_buttons(false);
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

void pointer_at(double xPt, double yPt) { show_buttons(mcpekbm::in_titlebar_hot_zone(xPt, yPt)); }

}  // namespace titlebar
