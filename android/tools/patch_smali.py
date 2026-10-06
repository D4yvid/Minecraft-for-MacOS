#!/usr/bin/env python3
"""Make MainActivity load librunet.so: inserts System.loadLibrary("runet") right after
the game's own System.loadLibrary("gnustl_shared") (librunet depends on it).

usage: patch_smali.py <path/to/smali/com/mojang/minecraftpe/MainActivity.smali>
Idempotent; leaves an APK already patched for runet-client alone. Exit 2 if the anchor
is missing (unsupported APK).
"""
import re
import sys

LOAD = '    invoke-static {%s}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n'


def main(path):
    with open(path) as f:
        text = f.read()
    if re.search(r'const-string (\w+), "runet"\s*\n\s*invoke-static \{\1\}, Ljava/lang/System;->loadLibrary', text):
        print("patch_smali: already loads runet")
        return 0
    m = re.search(r'const-string (\w+), "gnustl_shared"\s*\n\s*invoke-static \{\1\}, '
                  r'Ljava/lang/System;->loadLibrary\(Ljava/lang/String;\)V\n', text)
    if not m:
        print("patch_smali: no System.loadLibrary(\"gnustl_shared\") in %s; unsupported APK" % path,
              file=sys.stderr)
        return 2
    reg = m.group(1)
    insert = '\n    const-string %s, "runet"\n\n' % reg + LOAD % reg
    text = text[:m.end()] + insert + text[m.end():]
    with open(path, 'w') as f:
        f.write(text)
    print("patch_smali: added System.loadLibrary(\"runet\")")
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
