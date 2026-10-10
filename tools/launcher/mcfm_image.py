#!/usr/bin/env python3
"""Mach-O tools for the launcher (docs/LAUNCHER.md, Stage 1).

usage: mcfm_image.py imports <macho>              imports as TSV: lib, symbol, fn|data
       mcfm_image.py stubs <imports.tsv> <outdir> one C stub source per stubbed library
       mcfm_image.py dylib <executable> <out>     executable -> dylib loadable on macOS
Exit 2 when the input is unsuitable.
"""
import os
import re
import subprocess
import sys

# Libraries the image binds to the host system; everything else gets a generated stub.
HOST_LIBS = ("libSystem", "libc++", "libz")

# Non-lazy imports that are data, not functions (constants, ObjC classes, globals).
DATA_NAME = re.compile(
    r"^_(k[A-Z]\w*|OBJC_\w+|_objc_empty_cache|__CFConstantStringClassReference"
    r"|NS\w*(Key|Domain|Notification|Mode)|UI\w*(Notification|Key|Invalid|Image)"
    r"|GC\w*Notification|AVAudioSessionCategory\w*|CGRectZero|_NSConcrete\w+)$")


def fail(msg):
    print("mcfm_image.py: " + msg, file=sys.stderr)
    sys.exit(2)


def short_name(install_path):
    """'/usr/lib/libobjc.A.dylib' -> 'libobjc', '.../UIKit.framework/UIKit' -> 'UIKit'."""
    return install_path.rsplit("/", 1)[-1].split(".")[0]


def list_imports(path):
    """{(lib, symbol): kind} from `dyld_info -fixups`."""
    if not os.path.isfile(path):
        fail("no such file: " + path)
    try:
        text = subprocess.run(["dyld_info", "-fixups", path], check=True,
                              capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        fail("dyld_info failed on %s: %s" % (path, e))
    kinds = {}
    for line in text.splitlines():
        f = line.split()
        if len(f) < 5 or f[3] not in ("bind", "lazy-bind", "weak-bind"):
            continue
        lib, _, sym = f[4].partition("/")
        if not sym:
            continue
        key = (lib, sym)
        lazy = f[3] == "lazy-bind"
        data = not lazy and (f[1] != "__got" or DATA_NAME.match(sym) is not None)
        if lazy or kinds.get(key) == "fn":
            kinds[key] = "fn"
        else:
            kinds[key] = "data" if data else "fn"
    return kinds


def linked_libs(path):
    """Short names of every LC_LOAD_DYLIB / LC_LOAD_WEAK_DYLIB, from `otool -L`."""
    text = subprocess.run(["otool", "-L", path], check=True, capture_output=True, text=True).stdout
    return {short_name(line.split()[0]) for line in text.splitlines()[1:] if line.strip()}


def cmd_imports(path):
    rows = {(lib, sym, kind) for (lib, sym), kind in list_imports(path).items()}
    rows |= {(lib, "-", "lib") for lib in linked_libs(path)}
    for row in sorted(rows):
        print("\t".join(row))


def main(argv):
    if len(argv) == 3 and argv[1] == "imports":
        return cmd_imports(argv[2])
    fail(__doc__.strip())


if __name__ == "__main__":
    main(sys.argv)
