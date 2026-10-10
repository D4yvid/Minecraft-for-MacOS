// convert_tool <executable> <hooks.tsv> <out>: shared/loader/convert.cpp from the command line
// (tools/tests/convert_test.sh compares it with mcfm_image.py dylib --hooks). Hooks: lines
// "<name>\t0x<address>" (an empty file: no hooks). Nothing is written on failure.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "convert.h"

using namespace mcfm::loader;

int main(int argc, char **argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: convert_tool <executable> <hooks.tsv> <out>\n");
    return 2;
  }
  std::ifstream f(argv[1], std::ios::binary);
  if (!f) { std::fprintf(stderr, "convert_tool: cannot read %s\n", argv[1]); return 2; }
  std::vector<uint8_t> in((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::ifstream h(argv[2]);
  if (!h) { std::fprintf(stderr, "convert_tool: cannot read %s\n", argv[2]); return 2; }
  std::vector<ConvertHook> hooks;
  for (std::string line; std::getline(h, line);) {
    if (line.empty()) continue;
    size_t tab = line.find('\t');
    if (tab == std::string::npos) { std::fprintf(stderr, "convert_tool: bad hooks line: %s\n", line.c_str()); return 2; }
    hooks.push_back(ConvertHook{line.substr(0, tab), std::strtoull(line.c_str() + tab + 1, nullptr, 16)});
  }
  std::vector<uint8_t> thin, out;
  std::string error;
  if (!thin_arm64(in, &thin, &error) || !convert_executable(thin, hooks, &out, &error)) {
    std::fprintf(stderr, "convert_tool: %s: %s\n", argv[1], error.c_str());
    return 2;
  }
  std::ofstream o(argv[3], std::ios::binary);
  o.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
  return o ? 0 : 2;
}
