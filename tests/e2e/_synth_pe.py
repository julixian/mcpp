"""A minimal PE32+ image for the runtime placement tests (819, 820).

    python3 _synth_pe.py <out> program             a program importing vcruntime140.dll
    python3 _synth_pe.py <out> <a.b.c.d>           a DLL with that VERSIONINFO file version

The images are never loaded. They carry exactly what mcpp reads from a PE
file: the import directory (the closure of `place-dlls` and `mcpp pack`), the
optional header's linker version, and an RT_VERSION resource whose fixed file
information holds the version the runtime placement resolver compares.
"""
import struct
import sys


def pe(imports=(), version=None, linker=(14, 44)):
    b = bytearray(0x400)
    b[0:2] = b"MZ"
    struct.pack_into("<I", b, 0x3C, 0x80)
    nt = 0x80
    b[nt:nt + 4] = b"PE\0\0"
    struct.pack_into("<HH", b, nt + 4, 0x8664, 1)
    struct.pack_into("<H", b, nt + 20, 240)
    opt = nt + 24
    struct.pack_into("<H", b, opt, 0x20B)
    b[opt + 2], b[opt + 3] = linker
    struct.pack_into("<I", b, opt + 108, 16)
    dirs = opt + 112
    sec = opt + 240
    b[sec:sec + 8] = b".data\0\0\0"
    va, raw, size = 0x1000, 0x400, 0x400
    struct.pack_into("<IIII", b, sec + 8, size, va, size, raw)
    data = bytearray(size)
    if imports:
        names = 0x100
        for i, name in enumerate(imports):
            data[names:names + len(name) + 1] = name.encode() + b"\0"
            struct.pack_into("<IIIII", data, i * 20, va + 0x80, 0, 0, va + names, va + 0x80)
            names += len(name) + 1
        struct.pack_into("<II", b, dirs + 8, va, (len(imports) + 1) * 20)
    if version:
        r = 0x200
        struct.pack_into("<II", b, dirs + 16, va + r, 0x100)
        struct.pack_into("<H", data, r + 14, 1)
        struct.pack_into("<II", data, r + 16, 16, 0x80000000 | 0x18)
        struct.pack_into("<H", data, r + 0x18 + 14, 1)
        struct.pack_into("<II", data, r + 0x18 + 16, 1, 0x80000000 | 0x30)
        struct.pack_into("<H", data, r + 0x30 + 14, 1)
        struct.pack_into("<II", data, r + 0x30 + 16, 1033, 0x48)
        struct.pack_into("<II", data, r + 0x48, va + r + 0x60, 92)
        v = r + 0x60
        struct.pack_into("<HHH", data, v, 92, 52, 0)
        key = "VS_VERSION_INFO".encode("utf-16-le")
        data[v + 6:v + 6 + len(key)] = key
        ma, mi, bu, rv = version
        struct.pack_into("<IIII", data, v + 40, 0xFEEF04BD, 0x00010000,
                         (ma << 16) | mi, (bu << 16) | rv)
    return bytes(b) + bytes(data)


def main(argv):
    out, kind = argv[1], argv[2]
    if kind == "program":
        # Lower case, as the files are named: the closure matches a name
        # exactly on a case-sensitive host file system, as a Windows one does not.
        image = pe(imports=("vcruntime140.dll", "KERNEL32.dll"))
    else:
        image = pe(version=tuple(int(x) for x in kind.split(".")))
    with open(out, "wb") as f:
        f.write(image)


if __name__ == "__main__":
    main(sys.argv)
