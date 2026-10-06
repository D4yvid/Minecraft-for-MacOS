#include "platform.h"
#include "addresses.h"
#include "engine.h"
#include "pointer_lock.h"

#import <Foundation/Foundation.h>
#include <string>

namespace platform {
namespace {

// AppPlatform virtuals. `self` arrives in x0; the std::string result is
// returned through x8 exactly like the engine's own getEdition.
std::string get_edition(void *) { return "win10"; }
int default_input_mode(void *) { return 1; }  // 1 mouse, 2 touch, 3 gamepad
int ui_scaling_rules(void *) { return 0; }    // 0 desktop, 1 pocket
void hide_mouse_pointer(void *) { pl::set_wanted(true); }
void show_mouse_pointer(void *) { pl::set_wanted(false); }

}  // namespace

void install() {
  bool ok = eng::patch_slot(addr::kIOSPlatformVtable, addr::kSlotGetEdition, (void *)&get_edition) &&
            eng::patch_slot(addr::kIOSPlatformVtable, addr::kSlotDefaultInputMode, (void *)&default_input_mode) &&
            eng::patch_slot(addr::kIOSPlatformVtable, addr::kSlotUIScalingRules, (void *)&ui_scaling_rules) &&
            eng::patch_slot(addr::kIOSPlatformVtable, addr::kSlotHideMousePointer, (void *)&hide_mouse_pointer) &&
            eng::patch_slot(addr::kIOSPlatformVtable, addr::kSlotShowMousePointer, (void *)&show_mouse_pointer);
  NSLog(ok ? @"mcpekbm: platform win10" : @"mcpekbm: platform patch FAILED");
}

}  // namespace platform
