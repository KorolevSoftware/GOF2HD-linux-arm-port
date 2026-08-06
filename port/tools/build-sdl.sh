#!/usr/bin/env bash
# Build GOF2HD ARM host with SDL2 + Mali EGL, linking against libs from the
# target ARM machine (/tmp/opencode/armlibs).
set -euo pipefail
P="$(cd "$(dirname "$0")/.." && pwd)"

TC="$HOME/tools/armv5-eabi--glibc--stable-2024.02-1"
CC="$TC/bin/arm-buildroot-linux-gnueabi-gcc"
SYSROOT="$TC/arm-buildroot-linux-gnueabi/sysroot"

ARM_LIBS="/tmp/opencode/armlibs"
INC="/tmp/opencode/inc"

echo "== building host (SDL2 + EGL linked) =="
"$CC" -O2 -fno-stack-protector -rdynamic \
    -I"$P/host" -I"$INC" -I"$INC/SDL2" -I"$INC/EGL" -I"$INC/GLES2" \
    -o "$P/host/gof2hd-sdl" \
    "$P/host/gof2hd-sdl.c" "$P/host/jni.c" \
    -L"$ARM_LIBS" -lSDL2 -lmali -ldl -lgcc_s \
    -Wl,-rpath-link,"$ARM_LIBS" 2>&1 | grep -vE 'warning: |note: ' | head -20
echo "rc=$?"
ls -la "$P/host/gof2hd-sdl" 2>/dev/null && echo BUILD_OK || echo BUILD_FAIL
