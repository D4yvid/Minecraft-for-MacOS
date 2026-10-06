#pragma once

namespace titlebar {
void setup_window();                     // idempotent; call once the window exists
void pointer_at(double xPt, double yPt);  // window points; negative = gone / captured
}
