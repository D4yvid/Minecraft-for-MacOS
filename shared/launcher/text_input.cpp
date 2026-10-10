#include "text_input.h"

#include <vector>

namespace mcfm {
namespace launcher {
namespace {

std::vector<TextEvent> &queue_at(uintptr_t queue) { return *reinterpret_cast<std::vector<TextEvent> *>(queue); }

// Bytes in the UTF-8 sequence starting with `lead` (1 for ASCII and for invalid bytes).
size_t sequence_length(unsigned char lead) {
  if (lead >= 0xF0 && lead <= 0xF4) return 4;
  if (lead >= 0xE0) return lead <= 0xEF ? 3 : 1;
  if (lead >= 0xC2) return 2;
  return 1;
}

}  // namespace

void push_text(uintptr_t queue, const std::string &utf8) {
  for (size_t i = 0; i < utf8.size();) {
    size_t n = sequence_length(static_cast<unsigned char>(utf8[i]));
    if (i + n > utf8.size()) n = 1;
    for (size_t k = 1; k < n; k++)  // continuation bytes must be 10xxxxxx
      if ((static_cast<unsigned char>(utf8[i + k]) & 0xC0) != 0x80) { n = 1; break; }
    TextEvent e;
    e.text = utf8.substr(i, n);
    e.newline = false;
    queue_at(queue).push_back(e);
    i += n;
  }
}

void push_backspace(uintptr_t queue) {
  TextEvent e;
  e.text = "\b";
  e.newline = false;
  queue_at(queue).push_back(e);
}

void push_return(uintptr_t queue) {
  TextEvent e;
  e.text = "\n";
  e.newline = true;
  queue_at(queue).push_back(e);
}

}  // namespace launcher
}  // namespace mcfm
