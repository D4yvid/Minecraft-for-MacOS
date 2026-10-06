#pragma once
namespace macin {
// Feeds Mac keyboard / mouse (GameController + UIKit) into mcfm::keyboard_mouse.
void install();
void attach_view(void *uiView);  // called from the layoutSubviews hook
}
