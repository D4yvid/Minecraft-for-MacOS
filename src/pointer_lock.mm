#include "pointer_lock.h"

#import <UIKit/UIKit.h>
#include <dlfcn.h>

namespace pl {
namespace {

bool gWanted = false, gActive = true, gApplied = false;

// CoreGraphics cursor control, not exposed in the Catalyst headers.
using AssocFn = int32_t (*)(bool);
using CursorFn = int32_t (*)(uint32_t);
AssocFn gAssociate;
CursorFn gHide, gShow;

void apply() {
  bool want = gWanted && gActive;
  if (want == gApplied || !gAssociate) return;
  gApplied = want;
  gAssociate(!want);
  want ? gHide(0) : gShow(0);  // 0 = kCGDirectMainDisplay is ignored for cursor calls
  NSLog(@"mcpekbm: pointer %s", want ? "captured" : "released");
}

}  // namespace

void install() {
  void *cg = dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_LAZY);
  gAssociate = (AssocFn)dlsym(cg, "CGAssociateMouseAndMouseCursorPosition");
  gHide = (CursorFn)dlsym(cg, "CGDisplayHideCursor");
  gShow = (CursorFn)dlsym(cg, "CGDisplayShowCursor");
  if (!gAssociate || !gHide || !gShow) NSLog(@"mcpekbm: CoreGraphics cursor API missing, no capture");

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
