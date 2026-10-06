#pragma once
namespace pl {
void install();
void set_wanted(bool wanted);  // called by the game via hide/showMousePointer
bool captured();               // wanted && app active
}
