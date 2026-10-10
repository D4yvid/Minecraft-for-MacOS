#pragma once
// Typed text for the engine's Keyboard text queue (kKeyboardText), as iOS's ShowKeyboardView
// feeds it: one element per character, "\b" per deleted character, {"\n", true} for return.
// The queue is the engine's std::vector<TextEvent>; the launcher always runs the iOS binary
// (libc++, same ABI as ours on Apple). C++11.
#include <cstdint>
#include <string>

namespace mcfm {
namespace launcher {

struct TextEvent {
  std::string text;
  bool newline;
};
static_assert(sizeof(TextEvent) == 32, "TextEvent must match the engine (libc++ arm64)");

void push_text(uintptr_t queue, const std::string &utf8);  // one element per UTF-8 character
void push_backspace(uintptr_t queue);
void push_return(uintptr_t queue);

}  // namespace launcher
}  // namespace mcfm
