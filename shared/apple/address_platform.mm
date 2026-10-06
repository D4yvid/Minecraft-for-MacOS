#include "address_platform.h"
#include "addresses_0_15_10.h"
#include "macho_uuid.h"

#import <Foundation/Foundation.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <cstdlib>

namespace mcfm {
namespace apple {
namespace {

// Byte offset of each AppPlatform slot in AppPlatform_iOS's vtable; 0 = not mapped.
uintptr_t slot_offset(engine::Slot s) {
  switch (s) {
    case engine::Slot::GetEdition: return addr::kSlotGetEdition;
    case engine::Slot::DefaultInputMode: return addr::kSlotDefaultInputMode;
    case engine::Slot::UIScalingRules: return addr::kSlotUIScalingRules;
    case engine::Slot::HideMousePointer: return addr::kSlotHideMousePointer;
    case engine::Slot::ShowMousePointer: return addr::kSlotShowMousePointer;
    case engine::Slot::UseCenteredGUI: return 0;  // not located in the iOS binary yet
  }
  return 0;
}

struct MouseAction {  // MouseDevice::_inputs element
  int16_t x, y, dx, dy;
  int8_t button, data;
  uint8_t pad[6];
};
static_assert(sizeof(MouseAction) == 16, "MouseAction layout");

template <class T> struct RawVector { T *begin, *end, *cap; };

int16_t clamp16(int v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }

}  // namespace

bool AddressPlatform::attach() {
  bool found = false;
  for (uint32_t i = 0; i < _dyld_image_count() && !found; i++) {
    // Identify the game by LC_UUID before touching any hard-coded address.
    if (mcfm::is_expected_game_image(_dyld_get_image_header(i))) {
      slide_ = _dyld_get_image_vmaddr_slide(i);
      found = true;
    }
  }
  if (!found || getenv("MCFM_FORCE_GUARD_FAIL")) return false;
  struct { uintptr_t off, fn; } expected[] = {
      {addr::kSlotHideMousePointer, addr::kFnHideMousePointer},
      {addr::kSlotShowMousePointer, addr::kFnShowMousePointer},
      {addr::kSlotGetEdition, addr::kFnGetEdition},
      {addr::kSlotDefaultInputMode, addr::kFnDefaultInputModeIOS},
      {addr::kSlotUIScalingRules, addr::kFnUIScalingRulesIOS},
  };
  for (auto &e : expected)
    if (*(uintptr_t *)at(addr::kIOSPlatformVtable + e.off) != at(e.fn)) return false;
  attached_ = true;
  return true;
}

void AddressPlatform::log(const char *msg) { NSLog(@"mcfm: %s", msg); }

bool AddressPlatform::patch_slot(engine::Slot s, void *replacement, void **original) {
  uintptr_t off = slot_offset(s);
  if (!attached_ || !off) return false;
  uintptr_t slot = at(addr::kIOSPlatformVtable + off);
  vm_address_t page = slot & ~(vm_address_t)(vm_page_size - 1);
  // Remember the page's protection so it can be put back after the write.
  vm_address_t region = page;
  vm_size_t regionSize = 0;
  vm_region_basic_info_data_64_t info;
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t object = MACH_PORT_NULL;
  vm_prot_t protection = VM_PROT_READ;
  if (vm_region_64(mach_task_self(), &region, &regionSize, VM_REGION_BASIC_INFO_64,
                   (vm_region_info_t)&info, &count, &object) == KERN_SUCCESS)
    protection = info.protection;
  kern_return_t kr = vm_protect(mach_task_self(), page, vm_page_size, false,
                                VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
  if (kr != KERN_SUCCESS) {
    logf(*this, "vm_protect failed (%d) for slot +%lu", kr, (unsigned long)off);
    return false;
  }
  if (original) *original = *(void **)slot;
  *(void **)slot = replacement;
  vm_protect(mach_task_self(), page, vm_page_size, false, protection);
  return true;
}

void *AddressPlatform::global(engine::Global g) {
  if (!attached_) return nullptr;
  switch (g) {
    case engine::Global::KeyboardInputs: return (void *)at(addr::kKeyboardInputs);
    case engine::Global::KeyboardStates: return (void *)at(addr::kKeyboardStates);
  }
  return nullptr;
}

void AddressPlatform::mouse_feed(int btn, int state, int x, int y, int dx, int dy) {
  if (!attached_) return;
  if (dx || dy) {
    // MouseDevice::feed in this build has no dx/dy, so relative look motion is pushed
    // straight into Mouse::_instance's queue (the engine's slow path grows it).
    MouseAction a{};
    a.dx = clamp16(dx);
    a.dy = clamp16(dy);
    auto *v = (RawVector<MouseAction> *)at(addr::kMouseInputs);
    if (v->end < v->cap) {
      *v->end = a;
      v->end++;
    } else {
      ((void (*)(void *, MouseAction *))at(addr::kMouseInputsGrow))(v, &a);
    }
    return;
  }
  ((void (*)(void *, int, int, int, int))at(addr::kMouseDeviceFeed))(
      (void *)at(addr::kMouseDevice), btn, state, clamp16(x), clamp16(y));
}

}  // namespace apple
}  // namespace mcfm
