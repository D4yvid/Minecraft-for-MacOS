#include <mcfm/modules/win10_ui.h>

#include <string>

namespace mcfm {
namespace {

// One instance per process: the replacements are plain functions in the vtable.
Win10UiModule::EnabledFn gEnabled = 0;
void *gOrigEdition = 0, *gOrigScaling = 0, *gOrigCentered = 0;

bool enabled() { return gEnabled && gEnabled(); }

// AppPlatform virtuals. `self` arrives where the engine passes `this`; a std::string
// result uses the same hidden return slot as the engine's own member function.
std::string get_edition(void *self) {
  if (enabled()) return "win10";
  return ((std::string(*)(void *))gOrigEdition)(self);
}

int ui_scaling_rules(void *self) {
  if (enabled()) return 0;
  return ((int (*)(void *))gOrigScaling)(self);
}

bool use_centered_gui(void *self) {
  if (enabled()) return true;
  return ((bool (*)(void *))gOrigCentered)(self);
}

bool patch(Platform &p, engine::Slot slot, const char *slotName, void *fn, void **orig) {
  if (p.patch_slot(slot, fn, orig)) return true;
  logf(p, "win10_ui: %s unknown on this platform, skipped", slotName);
  return false;
}

}  // namespace

Win10UiModule::Win10UiModule(EnabledFn enabled) { gEnabled = enabled; }

bool Win10UiModule::init(Platform &p) {
  bool edition = patch(p, engine::Slot::GetEdition, "GetEdition", (void *)&get_edition, &gOrigEdition);
  patch(p, engine::Slot::UIScalingRules, "UIScalingRules", (void *)&ui_scaling_rules, &gOrigScaling);
  patch(p, engine::Slot::UseCenteredGUI, "UseCenteredGUI", (void *)&use_centered_gui, &gOrigCentered);
  return edition;
}

}  // namespace mcfm
