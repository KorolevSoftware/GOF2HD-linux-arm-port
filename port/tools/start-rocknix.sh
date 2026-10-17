#!/bin/bash
# ROCKNIX: use the running Wayland compositor and system Mesa ARMv7 libraries.
set -euo pipefail
GOF_ROOT="${GOF_ROOT:-/storage/gof2hd}"
RUN_DIR="$GOF_ROOT/port/run-native"
LOG_DIR="$GOF_ROOT/logs"
mkdir -p "$LOG_DIR"
APK="$GOF_ROOT/base.apk"
DATA="$GOF_ROOT/data"
OBB="${GOF_OBB:-$(find "$GOF_ROOT/obb" -name 'main.*.obb' -print -quit 2>/dev/null || true)}"
test -f "$APK" && test -f "$OBB" && test -x "$RUN_DIR/gof2hd" || {
    echo "Missing APK, OBB or executable under $GOF_ROOT" >&2
    exit 1
}
game_pids() {
    pgrep -f "^(\./|$RUN_DIR/)gof2hd( |$)" || true
}
if [ -n "$(game_pids)" ]; then
    echo "gof2hd is already running; stop it before starting another copy" >&2
    exit 1
fi
mkdir -p "$DATA"
for bank in "$DATA"/audio/*.fsb; do
    [ -f "$bank" ] || continue
    name="${bank##*/}"
    [ -e "$DATA/$name" ] || [ -L "$DATA/$name" ] || ln -s "audio/$name" "$DATA/$name"
done
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run/0-runtime-dir}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
export SDL_VIDEODRIVER=wayland
export SDL_VIDEO_EGL_DRIVER=/usr/lib32/libEGL.so.1
export SDL_VIDEO_GL_DRIVER=/usr/lib32/libGLESv2.so.2
export GOF_GLES_LIBRARY=/usr/lib32/libGLESv2.so.2
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-pulseaudio}"
export GOF_SHOW_CURSOR="${GOF_SHOW_CURSOR:-1}"
LOG="${GOF_LOG:-$LOG_DIR/run-$(date +%Y%m%d-%H%M%S).log}"
cd "$RUN_DIR"
# Keep preloads confined to the ARMv7 process; setsid itself is AArch64.
PRELOAD="$RUN_DIR/libm.so:$RUN_DIR/libfmod_filesystem.so:$RUN_DIR/cpuinfo_fake.so:$RUN_DIR/pthread_bionic.so"
if [ "${GOF_DEBUG:-0}" = 1 ]; then
    setsid gdb -nx -q -batch \
        -ex 'set pagination off' -ex 'set startup-with-shell off' \
        -ex 'set environment GOF_GDB 1' \
        -ex "set environment LD_LIBRARY_PATH $RUN_DIR:/usr/lib32" \
        -ex "set environment LD_PRELOAD $PRELOAD" \
        -ex "set substitute-path /work $GOF_ROOT" \
        -ex run -ex 'thread apply all bt full' -ex 'info registers' \
        -ex 'info sharedlibrary' -ex 'info proc mappings' \
        --args ./gof2hd "$APK" "$OBB" "$DATA/" >"$LOG" 2>&1 </dev/null &
else
    setsid env LD_LIBRARY_PATH="$RUN_DIR:/usr/lib32" LD_PRELOAD="$PRELOAD" \
        ./gof2hd "$APK" "$OBB" "$DATA/" >"$LOG" 2>&1 </dev/null &
fi
PID=$!
echo "$PID" > "$LOG_DIR/launcher.pid"
ln -sfn "${LOG##*/}" "$LOG_DIR/latest.log"
sleep 3
GAME_PID=""
for attempt in {1..10}; do
    kill -0 "$PID" 2>/dev/null || break
    GAME_PID=$(game_pids)
    [ -z "$GAME_PID" ] || break
    sleep 1
done
if [ -n "$GAME_PID" ] && kill -0 "$PID" 2>/dev/null; then
    echo "$GAME_PID" > "$LOG_DIR/game.pid"
    echo "GOF2HD PID=$GAME_PID launcher=$PID log=$LOG"
else
    tail -60 "$LOG"
    exit 1
fi
