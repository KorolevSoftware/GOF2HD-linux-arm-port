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

echo "== locating GLES provider =="
GLES_LIB=""
for c in libmali.so libGLESv2.so libGLESv2.so.2; do
    if [ -e "/usr/lib/$c" ] || [ -e "/usr/lib32/$c" ] || [ -e "/usr/lib/arm-linux-gnueabihf/$c" ]; then
        GLES_LIB="$c"; break
    fi
done
if [ -z "$GLES_LIB" ]; then
    echo "!! no GLES library found; will build gles-bridge anyway"
fi
echo "   using: ${GLES_LIB:-none}"

echo "== building shim (hardfp, pcs aapcs entries) =="
"$CC" $CFLAGS -shared -fPIC -std=gnu89 \
    -Wl,--version-script="$P/shim/version.map" -o "$P/shim/libc.so" \
    "$P/shim/shim.c" "$P/shim/sscanf.c" "$P/shim/stdio.c" "$P/shim/abi.c" -ldl
"$CC" $CFLAGS -shared -fPIC -o "$P/shim/liblog.so" "$P/shim/liblog.c"
"$CC" $CFLAGS -shared -fPIC -Wl,--version-script="$P/shim/libm.map" -o "$P/shim/libm.so" "$P/shim/libm.c" -ldl
for l in libandroid libdl; do
    printf '' | "$CC" $CFLAGS -shared -fPIC -Wl,--version-script="$P/shim/emptyver.map" -x c - -o "$P/shim/$l.so"
done

echo "== building gles bridge =="
"$CC" $CFLAGS -shared -fPIC -o "$P/gles-stub/libGLESv2.so" "$P/gles-stub/gles-bridge.c" -ldl

echo "== building fmod stubs =="
"$CC" $CFLAGS -shared -fPIC -o "$P/fmodex-stub/libfmodex.so" "$P/fmodex-stub/fmodex_stub.c"
"$CXX" $CFLAGS -shared -fPIC -o "$P/fmodex-stub/libfmodevent.so" "$P/fmodex-stub/fmodevent_stub.cpp"

echo "== building host =="
"$CC" $CFLAGS -rdynamic -o "$P/host/gof2hd" "$P/host/gof2hd.c" "$P/host/jni.c" -ldl -lgcc_s

echo "== assembling run-native =="
rm -rf "$OUT"
mkdir -p "$OUT"
cp "$P/shim"/libc.so "$P/shim"/liblog.so "$P/shim"/libandroid.so "$P/shim"/libm.so "$P/shim"/libdl.so "$OUT"/
cp "$P/gles-stub"/libGLESv2.so "$OUT"/libGLESv2.so
cp "$P/gles-stub"/libGLESv2.so "$OUT"/libGLESv1_CM.so
cp "$P/gles-stub"/libGLESv2.so "$OUT"/libEGL.so
cp "$P/host/gof2hd" "$OUT"/
[ -f /root/gof2hd/libgof2hdaa.so ] && \
    cp /root/gof2hd/libgof2hdaa.so "$OUT"/ || \
    echo "!! libgof2hdaa.so not found at /root/gof2hd/"
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
cp "$P/fmodex-stub"/libfmodex.so "$P/fmodex-stub"/libfmodevent.so "$OUT"/

echo "== done =="
ls -la "$OUT"
