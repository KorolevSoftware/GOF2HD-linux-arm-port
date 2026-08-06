#!/usr/bin/env bash
# Build the GOF2HD port stack for the device (hardfp, aapcs-vfp).
# Everything links against the device's hardfp glibc; the engine (softfp)
# is served through pcs("aapcs") entry points in shim and gles-bridge.
set -euo pipefail
P="$(cd "$(dirname "$0")/.." && pwd)"

TC="$HOME/tools/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-linux-gnueabihf"
CC="$TC/bin/arm-none-linux-gnueabihf-gcc"
CXX="$TC/bin/arm-none-linux-gnueabihf-g++"
SYSROOT="$TC/arm-none-linux-gnueabihf/libc"

OUT="$P/run-hf"
CFLAGS="-mfloat-abi=hard -O2 -fno-stack-protector -fno-builtin"

echo "== building shim (hardfp, pcs aapcs entries) =="
"$CC" $CFLAGS -shared -fPIC -std=gnu89 \
    -Wl,--version-script="$P/shim/version.map" -o "$P/shim/libc.so" \
    "$P/shim/shim.c" "$P/shim/sscanf.c" "$P/shim/stdio.c" "$P/shim/abi.c" -ldl
"$CC" $CFLAGS -shared -fPIC -o "$P/shim/liblog.so" "$P/shim/liblog.c"
for l in libandroid libm libdl; do
    printf '' | "$CC" $CFLAGS -shared -fPIC -Wl,--version-script="$P/shim/emptyver.map" -x c - -o "$P/shim/$l.so"
done

echo "== building gles bridge (hardfp -> libmali) =="
"$CC" $CFLAGS -shared -fPIC -o "$P/gles-stub/libGLESv2.so" "$P/gles-stub/gles-bridge.c" -ldl

echo "== building fmod stubs =="
"$CC" $CFLAGS -shared -fPIC -o "$P/fmodex-stub/libfmodex.so" "$P/fmodex-stub/fmodex_stub.c"
"$CXX" $CFLAGS -shared -fPIC -o "$P/fmodex-stub/libfmodevent.so" "$P/fmodex-stub/fmodevent_stub.cpp"

echo "== building host (hardfp) =="
"$CC" $CFLAGS -rdynamic -o "$P/host/gof2hd" "$P/host/gof2hd.c" "$P/host/jni.c" -ldl -lgcc_s

echo "== assembling run-hf =="
rm -rf "$OUT"
mkdir -p "$OUT"
cp "$P/shim"/libc.so "$P/shim"/liblog.so "$P/shim"/libandroid.so "$P/shim"/libm.so "$P/shim"/libdl.so "$OUT"/
cp "$P/gles-stub"/libGLESv2.so "$OUT"/libGLESv2.so
cp "$P/gles-stub"/libGLESv2.so "$OUT"/libGLESv1_CM.so
cp "$P/gles-stub"/libGLESv2.so "$OUT"/libEGL.so
cp "$P/host/gof2hd" "$OUT"/
cp /tmp/opencode/gof2hd/lib/armeabi-v7a/libgof2hdaa.so "$OUT"/
cp "$P/fmodex-stub"/libfmodex.so "$P/fmodex-stub"/libfmodevent.so "$OUT"/
cp "$SYSROOT/lib/ld-linux-armhf.so.3" "$SYSROOT/lib/libc.so.6" "$SYSROOT/lib/libm.so.6" \
   "$SYSROOT/lib/libdl.so.2" "$SYSROOT/lib/libpthread.so.0" "$OUT"/ 2>/dev/null || true
cp "$SYSROOT/usr/lib/libstdc++.so.6" "$SYSROOT/lib/libgcc_s.so.1" "$OUT"/ 2>/dev/null || true

echo "== done =="
ls -la "$OUT"
