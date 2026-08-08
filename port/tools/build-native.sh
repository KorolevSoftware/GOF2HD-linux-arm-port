#!/usr/bin/env bash
# Native build of the GOF2HD port stack directly on the device.
# Requires: gcc, g++, make, dlfcn (glibc dev), libmali (or Mesa GLES).
# Everything is built hardfp against the device's own glibc, so no
# cross-compiler or version mismatches.
set -euo pipefail
P="$(cd "$(dirname "$0")/.." && pwd)"

CC="${CC:-arm-linux-gnueabihf-gcc}"
CXX="${CXX:-arm-linux-gnueabihf-g++}"
CFLAGS="${CFLAGS:--O2 -fno-stack-protector -fno-builtin}"
OUT="$P/run-native"

rm -rf "$OUT"
mkdir -p "$OUT"

echo "== building shim (hardfp, pcs aapcs entries) =="
"$CC" $CFLAGS -shared -fPIC -std=gnu89 \
    -Wl,--version-script="$P/shim/version.map" -o "$OUT/libc.so" \
    "$P/shim/shim.c" "$P/shim/sscanf.c" "$P/shim/stdio.c" "$P/shim/abi.c" -ldl
"$CC" $CFLAGS -shared -fPIC -o "$OUT/liblog.so" "$P/shim/liblog.c"
"$CC" $CFLAGS -shared -fPIC -Wl,--version-script="$P/shim/libm.map" -o "$OUT/libm.so" "$P/shim/libm.c" -ldl
for l in libandroid libdl; do
    printf '' | "$CC" $CFLAGS -shared -fPIC -Wl,--version-script="$P/shim/emptyver.map" -x c - -o "$OUT/$l.so"
done

echo "== building gles bridge =="
"$CC" $CFLAGS -shared -fPIC -o "$OUT/libGLESv2.so" "$P/gles-stub/gles-bridge.c" -ldl

echo "== building fmod stubs =="
"$CC" $CFLAGS -shared -fPIC -o "$OUT/libfmodex.so" "$P/fmodex-stub/fmodex_stub.c"
"$CXX" $CFLAGS -shared -fPIC -o "$OUT/libfmodevent.so" "$P/fmodex-stub/fmodevent_stub.cpp"

echo "== building host (SDL2) =="
"$CC" $CFLAGS -rdynamic -o "$OUT/gof2hd" "$P/host/gof2hd.c" "$P/host/jni.c" "$P/host/wrap_overlay.c" \
    "$OUT/libGLESv2.so" -L/usr/lib32 -lSDL2main -lSDL2 -ldl -lgcc_s -lm

echo "== adding game engine =="
[ -f /root/gof2hd/libgof2hdaa.so ] && \
    cp /root/gof2hd/libgof2hdaa.so "$OUT"/ || \
    { echo "!! libgof2hdaa.so not found at /root/gof2hd/"; exit 1; }

echo "== patching game e_flags soft-float -> hard-float (0x5000200 -> 0x5000400) =="
python3 - "$OUT/libgof2hdaa.so" <<'PYEOF'
import struct, sys
path = sys.argv[1]
f = open(path, "r+b")
f.seek(0x24)
v = struct.unpack("<I", f.read(4))[0]
new = (v & ~0x200) | 0x400
f.seek(0x24)
f.write(struct.pack("<I", new))
f.close()
print(f"  e_flags 0x{v:08x} -> 0x{new:08x}")
PYEOF

echo "== patching game version tables -> unversioned imports (patch-versions.py) =="
python3 "$P/tools/patch-versions.py" "$OUT/libgof2hdaa.so" "$OUT/libc.so" "$OUT/libm.so"

echo "== done =="
ls -la "$OUT"