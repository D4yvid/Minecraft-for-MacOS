#include "engine.h"
#include "addresses.h"
#include "macho_uuid.h"

#import <Foundation/Foundation.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <cstdlib>
#include <cstring>

namespace eng {
namespace {

intptr_t gSlide = 0;

template <class T> struct RawVec { T *begin; T *end; T *cap; };

struct KeyEvent { int32_t state; uint8_t key; uint8_t pad[3]; };
static_assert(sizeof(KeyEvent) == 8, "KeyEvent layout");

struct MouseAction {
  int16_t x, y, dx, dy;
  int8_t button, data;
  uint8_t pad[6];
};
static_assert(sizeof(MouseAction) == 16, "MouseAction layout");

int16_t clamp16(int v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }

template <class T> void push(uintptr_t vecUnslid, uintptr_t growUnslid, const T &e) {
  auto *v = (RawVec<T> *)at(vecUnslid);
  if (v->end < v->cap) {
    *v->end = e;
    v->end++;
  } else {
    T copy = e;
    ((void (*)(void *, T *))at(growUnslid))(v, &copy);
  }
}

void device_feed(int btn, int state, int x, int y) {
  ((void (*)(void *, int, int, int, int))at(addr::kMouseDeviceFeed))(
      (void *)at(addr::kMouseDevice), btn, state, clamp16(x), clamp16(y));
}

bool slot_is(uintptr_t off, uintptr_t fnUnslid) {
  return *(uintptr_t *)at(addr::kIOSPlatformVtable + off) == at(fnUnslid);
}

}  // namespace

uintptr_t at(uintptr_t unslid) { return unslid + gSlide; }

bool init() {
  bool found = false;
  for (uint32_t i = 0; i < _dyld_image_count(); i++) {
    const char *name = _dyld_get_image_name(i);
    size_t n = strlen(name);
    if (n >= 13 && strcmp(name + n - 13, "/minecraftpe2") == 0) {
      // Identify the build by LC_UUID before touching any hard-coded address.
      if (!mcpekbm::is_expected_game_image(_dyld_get_image_header(i))) return false;
      gSlide = _dyld_get_image_vmaddr_slide(i);
      found = true;
      break;
    }
  }
  if (!found) return false;
  if (getenv("MCPEKBM_FORCE_GUARD_FAIL")) return false;
  return slot_is(addr::kSlotHideMousePointer, addr::kFnHideMousePointer) &&
         slot_is(addr::kSlotShowMousePointer, addr::kFnShowMousePointer) &&
         slot_is(addr::kSlotGetEdition, addr::kFnGetEdition) &&
         slot_is(addr::kSlotDefaultInputMode, addr::kFnDefaultInputModeIOS) &&
         slot_is(addr::kSlotUIScalingRules, addr::kFnUIScalingRulesIOS);
}

bool patch_slot(uintptr_t vtableUnslid, uintptr_t byteOffset, void *fn) {
  uintptr_t slot = at(vtableUnslid + byteOffset);
  vm_address_t page = slot & ~(vm_address_t)(vm_page_size - 1);
  // Remember the page's protection so it can be put back after the write.
  vm_address_t region = page;
  vm_size_t regionSize = 0;
  vm_region_basic_info_data_64_t info;
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t object = MACH_PORT_NULL;
  vm_prot_t original = VM_PROT_READ;
  if (vm_region_64(mach_task_self(), &region, &regionSize, VM_REGION_BASIC_INFO_64,
                   (vm_region_info_t)&info, &count, &object) == KERN_SUCCESS)
    original = info.protection;
  kern_return_t kr = vm_protect(mach_task_self(), page, vm_page_size, false,
                                VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
  if (kr != KERN_SUCCESS) {
    NSLog(@"mcpekbm: vm_protect failed (%d) for slot +%lu", kr, (unsigned long)byteOffset);
    return false;
  }
  *(void **)slot = fn;
  vm_protect(mach_task_self(), page, vm_page_size, false, original);
  return true;
}

void key(int vk, bool down) {
  if (vk <= 0 || vk > 255) return;
  KeyEvent e{down ? 1 : 0, (uint8_t)vk, {0, 0, 0}};
  push(addr::kKeyboardInputs, addr::kKeyboardInputsGrow, e);
  ((int32_t *)at(addr::kKeyboardStates))[vk] = down ? 1 : 0;
}

void mouse_button(int btn, bool down, int x, int y) { device_feed(btn, down ? 1 : 0, x, y); }

void mouse_move_abs(int x, int y) { device_feed(0, 0, x, y); }

void mouse_move_rel(int dx, int dy) {
  if (!dx && !dy) return;
  MouseAction a{};
  a.dx = clamp16(dx);
  a.dy = clamp16(dy);
  push(addr::kMouseInputs, addr::kMouseInputsGrow, a);
}

void mouse_wheel(int notches, int x, int y) {
  if (!notches) return;
  device_feed(4, notches > 0 ? 127 : -127, x, y);
}

}  // namespace eng
