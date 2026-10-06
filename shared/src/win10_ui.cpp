#include <mcfm/win10_ui.h>

#include <string>

namespace mcfm {
namespace win10_ui {
namespace {

// AppPlatform virtuals. `self` arrives where the engine passes `this`; a std::string
// result uses the same hidden return slot as the engine's own member function.
std::string get_edition(void *) { return "win10"; }
int ui_scaling_rules(void *) { return 0; }  // 0 desktop, 1-2 pocket
bool use_centered_gui(void *) { return true; }
int platform_type(void *) { return 0; }  // 0 desktop, 1 mobile
bool use_metadata_driven_screens(void *) { return true; }

bool patch(Platform &p, engine::Slot slot, const char *slotName, void *fn) {
  if (p.patch_slot(slot, fn, 0)) return true;
  logf(p, "win10_ui: %s unknown on this platform, skipped", slotName);
  return false;
}

}  // namespace

bool install(Platform &p) {
  bool edition = patch(p, engine::Slot::GetEdition, "GetEdition", (void *)&get_edition);
  patch(p, engine::Slot::UIScalingRules, "UIScalingRules", (void *)&ui_scaling_rules);
  patch(p, engine::Slot::UseCenteredGUI, "UseCenteredGUI", (void *)&use_centered_gui);
  patch(p, engine::Slot::PlatformType, "PlatformType", (void *)&platform_type);
  patch(p, engine::Slot::UseMetadataDrivenScreens, "UseMetadataDrivenScreens",
        (void *)&use_metadata_driven_screens);
  logf(p, "win10_ui: %s", edition ? "installed" : "FAILED");
  return edition;
}

}  // namespace win10_ui
}  // namespace mcfm
