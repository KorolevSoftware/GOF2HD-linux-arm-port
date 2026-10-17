#!/usr/bin/env bash
# ARMv7 hardfp build of the GOF2HD port stack, on-device or in Docker.
# Requires: ARMv7 gcc/g++, make, cmake, glibc dev, SDL2 dev.
# Everything is built hardfp against the build environment's glibc.
# The target must provide a compatible glibc and ARMv7 SDL2/GLES libraries.
set -euo pipefail
P="$(cd "$(dirname "$0")/.." && pwd)"

CC="${CC:-arm-linux-gnueabihf-gcc}"
CXX="${CXX:-arm-linux-gnueabihf-g++}"
CFLAGS="${CFLAGS:--O2 -g -fno-stack-protector -fno-builtin}"
OUT="${GOF_OUT:-$P/run-native}"
SYS_LIB="${GOF_SYS_LIB:-/usr/lib32}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

echo "== building host and compatibility libraries (CMake/GCC, hardfp) =="
cmake -S "$P/.." -B "$OUT/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DGOF_OUT="$OUT" -DGOF_SYS_LIB="$SYS_LIB" -DGOF_CFLAGS="$CFLAGS"
cmake --build "$OUT/build" --parallel "${GOF_JOBS:-2}"


echo "== port files built (game data is prepared separately) =="
ls -la "$OUT"
