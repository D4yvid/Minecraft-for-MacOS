#import <Foundation/Foundation.h>
#include "engine.h"
#include "mac_input.h"
#include "platform.h"
#include "pointer_lock.h"
#include "resize.h"

__attribute__((constructor)) static void mcpekbm_init() {
  if (!eng::init()) {
    NSLog(@"mcpekbm: unexpected binary, disabled");
    return;
  }
  pl::install();
  platform::install();
  resize::install();
  macin::install();
  NSLog(@"mcpekbm: patched");
}
