#!/usr/bin/env python3
"""patch_llvm.py <llvm runtimes copy>: adapts the LLVM 18.1.8 libc++ and libunwind sources for
the Android launcher runtime (docs/LAUNCHER.md, Stage 3a). Each edit must match exactly once,
so a different LLVM version fails here instead of building something subtly wrong.

- libc++ ctype on Darwin's tables (_LIBCPP_MCFM_DARWIN_CTYPE, see runtime/include/__config_site):
  the game inlines ctype<char>::is() against Darwin's _CTYPE_* masks.
- libunwind: compact unwind and the dynamic unwind-section finders (Apple-only upstream) on
  Android, for the Mach-O image our loader maps (_LIBUNWIND_MCFM_DYNAMIC_SECTIONS).
"""
import pathlib
import sys

EDITS = {
    "libcxx/include/__config": [(
        "#  if defined(__BIONIC__) || defined(__NuttX__) || defined(__Fuchsia__) || defined(__wasi__) ||",
        "#  if (defined(__BIONIC__) && !defined(_LIBCPP_MCFM_DARWIN_CTYPE)) || defined(__NuttX__) || defined(__Fuchsia__) || defined(__wasi__) ||",
    )],
    "libcxx/include/__locale": [(
        "class _LIBCPP_EXPORTED_FROM_ABI ctype_base {\npublic:\n#if defined(_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE)\n",
        "class _LIBCPP_EXPORTED_FROM_ABI ctype_base {\npublic:\n"
        "#if defined(_LIBCPP_MCFM_DARWIN_CTYPE)\n"
        "  // Darwin's _CTYPE_* bits (darwin_abi.h), as Apple's libc++ defines them.\n"
        "  typedef unsigned int mask;\n"
        "  static const mask space  = 0x4000;\n"
        "  static const mask print  = 0x40000;\n"
        "  static const mask cntrl  = 0x200;\n"
        "  static const mask upper  = 0x8000;\n"
        "  static const mask lower  = 0x1000;\n"
        "  static const mask alpha  = 0x100;\n"
        "  static const mask digit  = 0x400;\n"
        "  static const mask punct  = 0x2000;\n"
        "  static const mask xdigit = 0x10000;\n"
        "  static const mask blank  = 0x20000;\n"
        "  static const mask __regex_word = 0x80;\n"
        "#elif defined(_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE)\n",
    )],
    "libcxx/include/regex": [(
        "#if defined(__BIONIC__) || defined(_NEWLIB_VERSION)\n  // Originally bionic's ctype_base",
        "#if (defined(__BIONIC__) && !defined(_LIBCPP_MCFM_DARWIN_CTYPE)) || defined(_NEWLIB_VERSION)\n  // Originally bionic's ctype_base",
    )],
    "libcxx/src/locale.cpp": [(
        "const ctype<char>::mask* ctype<char>::classic_table() noexcept {\n#  if defined(__APPLE__) || defined(__FreeBSD__)\n",
        "const ctype<char>::mask* ctype<char>::classic_table() noexcept {\n#  if defined(__APPLE__) || defined(__FreeBSD__) || defined(_LIBCPP_MCFM_DARWIN_CTYPE)\n",
    )],
    "libunwind/src/config.h": [(
        "#if defined(_LIBUNWIND_HIDE_SYMBOLS)\n  // The CMake file passes -fvisibility=hidden",
        "// mcfm: Mach-O images mapped by our own loader (Android): their compact unwind info comes\n"
        "// through the dynamic unwind-section finders.\n"
        "#if defined(_LIBUNWIND_MCFM_DYNAMIC_SECTIONS) && !defined(__APPLE__)\n"
        "  #define _LIBUNWIND_SUPPORT_COMPACT_UNWIND 1\n"
        "#endif\n\n"
        "#if defined(_LIBUNWIND_HIDE_SYMBOLS)\n  // The CMake file passes -fvisibility=hidden",
    )],
    "libunwind/src/libunwind_ext.h": [(
        "#ifdef __APPLE__\n\n// Holds a description of the object-format-header",
        "#if defined(__APPLE__) || defined(_LIBUNWIND_MCFM_DYNAMIC_SECTIONS)\n\n// Holds a description of the object-format-header",
    )],
    "libunwind/src/libunwind.cpp": [(
        "#ifdef __APPLE__\n\nnamespace libunwind {\n\nstatic constexpr size_t MAX_DYNAMIC_UNWIND_SECTIONS_FINDERS",
        "#if defined(__APPLE__) || defined(_LIBUNWIND_MCFM_DYNAMIC_SECTIONS)\n\nnamespace libunwind {\n\nstatic constexpr size_t MAX_DYNAMIC_UNWIND_SECTIONS_FINDERS",
    )],
    "libunwind/src/AddressSpace.hpp": [(
        "inline bool LocalAddressSpace::findUnwindSections(pint_t targetAddr,\n"
        "                                                  UnwindInfoSections &info) {\n",
        "#if defined(_LIBUNWIND_MCFM_DYNAMIC_SECTIONS) && !defined(__APPLE__)\n"
        "bool findDynamicUnwindSections(void *, unw_dynamic_unwind_sections *);\n"
        "#endif\n\n"
        "inline bool LocalAddressSpace::findUnwindSections(pint_t targetAddr,\n"
        "                                                  UnwindInfoSections &info) {\n"
        "#if defined(_LIBUNWIND_MCFM_DYNAMIC_SECTIONS) && !defined(__APPLE__)\n"
        "  // mcfm: images registered by our loader first (their ranges are not ELF objects).\n"
        "  unw_dynamic_unwind_sections mcfmSections;\n"
        "  if (findDynamicUnwindSections((void *)targetAddr, &mcfmSections)) {\n"
        "    info.dso_base = mcfmSections.dso_base;\n"
        "    info.dwarf_section = (uintptr_t)mcfmSections.dwarf_section;\n"
        "    info.dwarf_section_length = mcfmSections.dwarf_section_length;\n"
        "#if defined(_LIBUNWIND_SUPPORT_DWARF_INDEX)\n"
        "    info.dwarf_index_section = 0;\n"
        "    info.dwarf_index_section_length = 0;\n"
        "#endif\n"
        "    info.compact_unwind_section = (uintptr_t)mcfmSections.compact_unwind_section;\n"
        "    info.compact_unwind_section_length = mcfmSections.compact_unwind_section_length;\n"
        "    return true;\n"
        "  }\n"
        "#endif\n",
    )],
}


def main():
    root = pathlib.Path(sys.argv[1])
    for rel, edits in EDITS.items():
        path = root / rel
        text = path.read_text()
        for old, new in edits:
            count = text.count(old)
            if count != 1:
                sys.exit(f"patch_llvm: {rel}: expected text found {count} times: {old[:70]!r}")
            text = text.replace(old, new)
        path.write_text(text)
    print("patch_llvm: patched", len(EDITS), "files")


if __name__ == "__main__":
    main()
