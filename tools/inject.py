#!/usr/bin/env python3
"""Add an LC_LOAD_DYLIB command to a thin 64-bit Mach-O, using header padding.

usage: inject.py <macho> <dylib install path>
Idempotent. Exit 2 when the file is unsuitable. Re-sign the file afterwards.
"""
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
LC_SEGMENT_64 = 0x19
LC_LOAD_DYLIB = 0xC
LC_LOAD_WEAK_DYLIB = 0x80000018
HEADER_SIZE = 32


def fail(msg):
    print("inject.py: " + msg, file=sys.stderr)
    sys.exit(2)


def main(path, dylib):
    data = bytearray(open(path, "rb").read())
    magic, _cpu, _sub, _ftype, ncmds, sizeofcmds, _flags, _res = struct.unpack_from("<IiiIIIII", data, 0)
    if magic != MH_MAGIC_64:
        fail("not a thin 64-bit Mach-O (lipo -thin first)")

    first_data = len(data)
    off = HEADER_SIZE
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmd in (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB):
            name_off = struct.unpack_from("<I", data, off + 8)[0]
            name = bytes(data[off + name_off: off + cmdsize]).split(b"\0", 1)[0].decode()
            if name == dylib:
                print("inject.py: already present")
                return
        if cmd == LC_SEGMENT_64:
            nsects = struct.unpack_from("<I", data, off + 64)[0]
            for i in range(nsects):
                s = off + 72 + i * 80
                size = struct.unpack_from("<Q", data, s + 40)[0]
                sect_off = struct.unpack_from("<I", data, s + 48)[0]
                if size and sect_off:
                    first_data = min(first_data, sect_off)
        off += cmdsize

    name = dylib.encode() + b"\0"
    cmdsize = (24 + len(name) + 7) & ~7
    end = HEADER_SIZE + sizeofcmds
    if end + cmdsize > first_data:
        fail("not enough header padding (%d free, need %d)" % (first_data - end, cmdsize))
    if any(data[end:end + cmdsize]):
        fail("header padding is not zero")

    lc = struct.pack("<IIIIII", LC_LOAD_DYLIB, cmdsize, 24, 2, 0x10000, 0x10000) + name
    lc += b"\0" * (cmdsize - len(lc))
    data[end:end + cmdsize] = lc
    struct.pack_into("<II", data, 16, ncmds + 1, sizeofcmds + cmdsize)
    open(path, "wb").write(data)
    print("inject.py: added %s" % dylib)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        fail("usage: inject.py <macho> <dylib install path>")
    main(sys.argv[1], sys.argv[2])
