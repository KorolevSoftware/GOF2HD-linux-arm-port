#!/usr/bin/env bash
# ARMv7 hardfp build of the GOF2HD port stack, on-device or in Docker.
# Requires: ARMv7 gcc/g++, make, cmake, glibc dev, SDL2 dev, unzip, python3.
# Everything is built hardfp against the build environment's glibc.
# The target must provide a compatible glibc and ARMv7 SDL2/GLES libraries.
set -euo pipefail
P="$(cd "$(dirname "$0")/.." && pwd)"

CC="${CC:-arm-linux-gnueabihf-gcc}"
CXX="${CXX:-arm-linux-gnueabihf-g++}"
CFLAGS="${CFLAGS:--O2 -g -fno-stack-protector -fno-builtin}"
OUT="${GOF_OUT:-$P/run-native}"
SYS_LIB="${GOF_SYS_LIB:-/usr/lib32}"
APK="${GOF_APK:-$P/../base.apk}"

[ -f "$APK" ] || { echo "!! base.apk not found at $APK"; exit 1; }
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

echo "== building host and compatibility libraries (CMake/GCC, hardfp) =="
cmake -S "$P/.." -B "$OUT/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DGOF_OUT="$OUT" -DGOF_SYS_LIB="$SYS_LIB" -DGOF_CFLAGS="$CFLAGS"
cmake --build "$OUT/build" --parallel "${GOF_JOBS:-2}"

echo "== real FMOD from base.apk =="
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

echo "== adding game engine =="
unzip -o -j "$APK" lib/armeabi-v7a/libgof2hdaa.so -d "$OUT" >/dev/null
test -f "$OUT/libgof2hdaa.so" || { echo "!! game engine missing from $APK"; exit 1; }

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
