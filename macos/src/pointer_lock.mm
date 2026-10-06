#include "pointer_lock.h"
#include "titlebar.h"

#import <UIKit/UIKit.h>
#include <dlfcn.h>

namespace pl {
namespace {

bool gWanted = false, gActive = true, gApplied = false;

// CoreGraphics cursor control, not exposed in the Catalyst headers.
using AssocFn = int32_t (*)(bool);
using CursorFn = int32_t (*)(uint32_t);
using WarpFn = int32_t (*)(CGPoint);
AssocFn gAssociate;
CursorFn gHide, gShow;
WarpFn gWarp;

void apply() {
  bool want = gWanted && gActive;
  if (want == gApplied || !gAssociate) return;
  gApplied = want;
  if (want) {
    titlebar::pointer_at(-1, -1);
    // Park the hidden cursor mid-window so clicks can't land on the title strip or an edge.
    double x, y;
    if (gWarp && titlebar::window_center(&x, &y)) gWarp(CGPointMake(x, y));
  }
  gAssociate(!want);
  want ? gHide(0) : gShow(0);  // 0 = kCGDirectMainDisplay is ignored for cursor calls
  NSLog(@"mcfm: pointer %s", want ? "captured" : "released");
}

}  // namespace

void install() {
  void *cg = dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_LAZY);
  gAssociate = (AssocFn)dlsym(cg, "CGAssociateMouseAndMouseCursorPosition");
  gHide = (CursorFn)dlsym(cg, "CGDisplayHideCursor");
  gShow = (CursorFn)dlsym(cg, "CGDisplayShowCursor");
  gWarp = (WarpFn)dlsym(cg, "CGWarpMouseCursorPosition");
  if (!gAssociate || !gHide || !gShow) NSLog(@"mcfm: CoreGraphics cursor API missing, no capture");

  NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
  [nc addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:NSOperationQueue.mainQueue
              usingBlock:^(NSNotification *) { gActive = false; apply(); }];
  [nc addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:NSOperationQueue.mainQueue
              usingBlock:^(NSNotification *) { gActive = true; apply(); }];
}

void set_wanted(bool wanted) {
  gWanted = wanted;
  apply();
}

bool captured() { return gApplied; }

}  // namespace pl
