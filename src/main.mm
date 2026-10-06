#import <Foundation/Foundation.h>
#include "engine.h"
#include "mac_input.h"
#include "platform.h"
#include "pointer_lock.h"
#include "resize.h"
#include "store.h"

__attribute__((constructor)) static void mcpekbm_init() {
  if (!eng::init()) {
    NSLog(@"mcpekbm: unexpected binary, disabled");
    return;
  }
  pl::install();
  platform::install();
  resize::install();
  macin::install();
  store::install();
  NSLog(@"mcpekbm: patched");
}
