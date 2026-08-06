#!/usr/bin/env bash
# Build the GOF2HD Linux port stack for ARM soft-float (armel), matching the
# engine's bionic armeabi-v7a softfp ABI.
set -euo pipefail

P="$(cd "$(dirname "$0")/.." && pwd)"
TC_SOFT="$HOME/tools/armv5-eabi--glibc--stable-2024.02-1"
CC="$TC_SOFT/bin/arm-buildroot-linux-gnueabi-gcc"
CXX="$TC_SOFT/bin/arm-buildroot-linux-gnueabi-g++"
SYSROOT="$TC_SOFT/arm-buildroot-linux-gnueabi/sysroot"
RUNDIR="$P/run"
SRC_APK_LIB="/tmp/opencode/gof2hd/lib/armeabi-v7a"

echo "== building shim =="
"$CC" -shared -fPIC -O2 -std=gnu89 -fno-stack-protector -fno-builtin \
    -Wl,--version-script="$P/shim/version.map" -o "$P/shim/libc.so" \
    "$P/shim/shim.c" "$P/shim/sscanf.c" "$P/shim/abi.c" -ldl
"$CC" -shared -fPIC -O2 -o "$P/shim/liblog.so" "$P/shim/liblog.c"
for l in libandroid libm libdl; do
    printf '' | "$CC" -shared -fPIC -Wl,--version-script="$P/shim/emptyver.map" -x c - -o "$P/shim/$l.so"
done

echo "== building gles stub =="
"$CC" -shared -fPIC -O2 -o "$P/gles-stub/libGLESv2.so" "$P/gles-stub/gles-stub.c"

echo "== building fmod stubs =="
"$CC" -shared -fPIC -O2 -o "$P/fmodex-stub/libfmodex.so" "$P/fmodex-stub/fmodex_stub.c"
"$CXX" -shared -fPIC -O2 -o "$P/fmodex-stub/libfmodevent.so" "$P/fmodex-stub/fmodevent_stub.cpp"

echo "== building host =="
"$CC" -O2 -fno-stack-protector -rdynamic -o "$P/host/gof2hd" "$P/host/gof2hd.c" "$P/host/jni.c" -ldl -lgcc_s

echo "== assembling run dir =="
rm -rf "$RUNDIR"
mkdir -p "$RUNDIR/lib"
cp "$P/shim"/libc.so "$P/shim"/liblog.so "$P/shim"/libandroid.so "$P/shim"/libm.so "$P/shim"/libdl.so "$RUNDIR"/
cp "$P/gles-stub"/libGLESv2.so "$RUNDIR"/libGLESv2.so
cp "$P/gles-stub"/libGLESv2.so "$RUNDIR"/libGLESv1_CM.so
cp "$P/gles-stub"/libGLESv2.so "$RUNDIR"/libEGL.so
cp "$P/host/gof2hd" "$RUNDIR"/
cp "$SRC_APK_LIB"/libgof2hdaa.so "$RUNDIR"/
cp "$P/fmodex-stub"/libfmodex.so "$P/fmodex-stub"/libfmodevent.so "$RUNDIR"/

# softfp glibc stack from Bootlin sysroot
cp "$SYSROOT/lib/ld-linux.so.3" "$SYSROOT/lib/libc.so.6" "$SYSROOT/lib/libm.so.6" \
   "$SYSROOT/lib/libdl.so.2" "$SYSROOT/lib/libpthread.so.0" "$RUNDIR/lib/"
cp "$SYSROOT/usr/lib/libstdc++.so.6" "$SYSROOT/lib/libgcc_s.so.1" "$RUNDIR/lib/" 2>/dev/null || \
cp "$SYSROOT/lib/libstdc++.so.6" "$SYSROOT/lib/libgcc_s.so.1" "$RUNDIR/lib/"
ln -sf libstdc++.so.6 "$RUNDIR/lib/libstdc++.so"

echo "== done =="
ls -la "$RUNDIR"
