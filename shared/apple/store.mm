#include "store.h"

#import <Foundation/Foundation.h>
#include <objc/runtime.h>

namespace store {
namespace {

// This copy was not installed by the App Store, so it has no receipt. The game's
// StoreManager then starts an SKReceiptRefreshRequest at launch, which makes macOS
// ask for an Apple Account. In-app purchases cannot work here anyway; skip it.
void skip_receipt_refresh(id, SEL) {}

}  // namespace

void install() {
  Method m = class_getInstanceMethod(objc_getClass("StoreManager"), @selector(startRefreshReceiptRequest));
  if (!m) {
    NSLog(@"mcfm: StoreManager not found, receipt refresh left alone");
    return;
  }
  method_setImplementation(m, (IMP)skip_receipt_refresh);
  NSLog(@"mcfm: receipt refresh disabled");
}

}  // namespace store
