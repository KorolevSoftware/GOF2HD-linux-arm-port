#!/usr/bin/env python3
"""Selectively strip version requirements from a bionic libgof2hdaa.so.

The engine links against Android's bionic libc and imports many symbols as
`foo@LIBC`.  glibc has no "LIBC" node, so we re-export those symbols from our
shim libc.so/libm.so under a LIBC version node; the dynamic linker then has to
bind them to our shim (the only provider of version "LIBC").  This is what
keeps our ABI-translation wrappers (FILE*, stat, pthread, softfp math) in the
hot path.

But a fully-versioned engine would also force ALL plain functions (malloc,
memcpy, open, ...) through the shim, which is exactly the wrapper boilerplate
we want to avoid.  So this tool *selectively* zeroes the version entry
(VERSYM) of every undefined symbol that our shim does NOT provide: those
imports become unversioned and bind straight to glibc (the first provider in
scope).  Imports that our shim DOES provide keep their @LIBC requirement.

Version-needed tables (DT_VERNEED / .gnu.version_r) are left untouched so the
"LIBC" node stays resolvable.

Usage: patch-versions.py <libgof2hdaa.so> <shim_libc.so> <shim_libm.so>
"""
import struct
import sys

def section_hdrs(data):
    if data[:4] != b"\x7fELF" or data[4] != 1:
        raise SystemExit("not a 32-bit little-endian ELF")
    if data[5] != 1:
        raise SystemExit("not little-endian")
    e_shoff = struct.unpack_from("<I", data, 0x20)[0]
    e_shentsize = struct.unpack_from("<H", data, 0x2E)[0]
    e_shnum = struct.unpack_from("<H", data, 0x30)[0]
    e_shstrndx = struct.unpack_from("<H", data, 0x32)[0]
    if e_shentsize != 40:
        raise SystemExit(f"unexpected section header entry size {e_shentsize}")
    def sec(i):
        b = e_shoff + i * e_shentsize
        return dict(name=struct.unpack_from("<I", data, b + 0)[0],
                    type=struct.unpack_from("<I", data, b + 4)[0],
                    off=struct.unpack_from("<I", data, b + 16)[0],
                    size=struct.unpack_from("<I", data, b + 20)[0],
                    link=struct.unpack_from("<I", data, b + 24)[0],
                    entsize=struct.unpack_from("<I", data, b + 36)[0])
    strh = sec(e_shstrndx)
    shstr = bytes(data[strh["off"]: strh["off"] + strh["size"]])
    def secname(s):
        e = shstr.find(b"\x00", s["name"])
        return shstr[s["name"]:e if e >= 0 else len(shstr)].decode("latin1")
    return [sec(i) for i in range(e_shnum)], secname

def named_sections(data):
    sections, secname = section_hdrs(data)
    return {secname(s): s for s in sections}, secname

def load_symbols_and_versions(data):
    """Return (dynsyms, versym) where dynsyms is list of dicts."""
    sections, secname = section_hdrs(data)
    byname = {secname(s): s for s in sections}
    dynsym = byname.get(".dynsym")
    dynstr = byname.get(".dynstr")
    ver = byname.get(".gnu.version")
    if not (dynsym and dynstr and ver):
        raise SystemExit("engine lacks .dynsym/.dynstr/.gnu.version")
    strtab = data[dynstr["off"]: dynstr["off"] + dynstr["size"]]
    de = dynsym["entsize"] or 16
    n = dynsym["size"] // de
    def dname(o):
        e = strtab.find(b"\x00", o)
        return strtab[o:e if e >= 0 else len(strtab)].decode("latin1")
    syms = []
    for i in range(n):
        b = dynsym["off"] + i * de
        name = dname(struct.unpack_from("<I", data, b)[0])
        info = struct.unpack_from("<B", data, b + 12)[0]
        shndx = struct.unpack_from("<H", data, b + 14)[0]
        vi = struct.unpack_from("<H", data, ver["off"] + i * 2)[0]
        syms.append(dict(name=name, bind=info >> 4, type=info & 0xf,
                         shndx=shndx, versym=vi))
    return syms

def shim_exports(path):
    data = open(path, "rb").read()
    syms = load_symbols_and_versions(data)
    return {s["name"] for s in syms
            if s["shndx"] != 0 and s["bind"] in (1, 2) and s["name"]}

def patch(engine_path, libc_path, libm_path):
    keep = shim_exports(libc_path) | shim_exports(libm_path)

    f = open(engine_path, "r+b")
    data = bytearray(f.read())
    syms = load_symbols_and_versions(data)
    sections, secname = section_hdrs(data)
    ver = next(s for s in sections if secname(s) == ".gnu.version")

    zeroed = 0
    kept = 0
    for i, s in enumerate(syms):
        if s["shndx"] != 0 or s["bind"] not in (1, 2):
            continue
        if s["versym"] == 0:
            continue
        if s["name"] in keep:
            kept += 1
        else:
            struct.pack_into("<H", data, ver["off"] + i * 2, 0)
            zeroed += 1

    f.seek(0)
    f.write(data)
    f.close()
    print(f"  kept @LIBC for {kept} shim-provided imports")
    print(f"  unversioned {zeroed} glibc-bound imports")
    print(f"  patched {engine_path}")
    return 0

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("usage: patch-versions.py <engine.so> <shim_libc.so> <shim_libm.so>",
              file=sys.stderr)
        raise SystemExit(1)
    raise SystemExit(patch(sys.argv[1], sys.argv[2], sys.argv[3]))