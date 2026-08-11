#!/usr/bin/env bash
# Native build of the GOF2HD port stack directly on the device.
# Requires: gcc, g++, make, dlfcn (glibc dev), libmali (or Mesa GLES), SDL2 dev,
#           unzip, python3.
# Everything is built hardfp against the device's own glibc, so no
# cross-compiler or version mismatches.
set -euo pipefail
P="$(cd "$(dirname "$0")/.." && pwd)"

CC="${CC:-arm-linux-gnueabihf-gcc}"
CXX="${CXX:-arm-linux-gnueabihf-g++}"
CFLAGS="${CFLAGS:--O2 -g -fno-stack-protector -fno-builtin}"
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

echo "== building audio/bionic-compat helpers =="
# bionic pthread/sem translation (LD_PRELOAD) + fake /proc/cpuinfo (FMOD CPU detect)
"$CC" $CFLAGS -shared -fPIC -o "$OUT/pthread_bionic.so" "$P/fmodex-stub/pthread_bionic.c" -ldl -lpthread
"$CC" $CFLAGS -shared -fPIC -o "$OUT/cpuinfo_fake.so" "$P/fmodex-stub/cpuinfo_fake.c" -ldl
# libstdc++ shim for bionic C++ runtime symbols
"$CXX" $CFLAGS -shared -fPIC -o "$OUT/libstdc++.so" "$P/fmodex-stub/libstdcxx_stub.c"
# fake OpenSL ES -> SDL2 audio (the Android FMOD opensl output)
"$CC" $CFLAGS -shared -fPIC -o "$OUT/libOpenSLES.so" "$P/fmodex-stub/libOpenSLES.c" -lSDL2 -lSDL2main -L/usr/lib32
# FMOD streams FEV/FSB through a thread-safe POSIX filesystem callback.
"$CC" $CFLAGS -shared -fPIC -o "$OUT/libfmod_filesystem.so" "$P/fmodex-stub/fmod_filesystem.c" -ldl

echo "== real FMOD from base.apk =="
APK="${GOF_APK:-$P/../base.apk}"
[ -f "$APK" ] || { echo "!! base.apk not found at $APK"; exit 1; }
unzip -o -j "$APK" lib/armeabi-v7a/libfmodex.so lib/armeabi-v7a/libfmodevent.so -d "$OUT" >/dev/null

echo "== patching FMOD e_flags soft-float -> hard-float (0x5000200 -> 0x5000400) =="
python3 - "$OUT/libfmodex.so" "$OUT/libfmodevent.so" <<'PYEOF'
import struct, sys
for path in sys.argv[1:]:
    f = open(path, "r+b")
    f.seek(0x24)
    v = struct.unpack("<I", f.read(4))[0]
    new = (v & ~0x200) | 0x400
    f.seek(0x24)
    f.write(struct.pack("<I", new))
    f.close()
    print(f"  {path} e_flags 0x{v:08x} -> 0x{new:08x}")
PYEOF

echo "== building host (SDL2) =="
"$CC" $CFLAGS -rdynamic -o "$OUT/gof2hd" "$P/host/gof2hd.c" "$P/host/config.c" \
    "$P/host/engine_bridge.c" "$P/host/touch_fifo.c" "$P/host/jni.c" \
    "$P/host/wrap_overlay.c" \
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
