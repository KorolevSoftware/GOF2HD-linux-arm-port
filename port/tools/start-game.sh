#!/bin/sh
# GOF2HD: запуск одной командой.
# 1) закрывает лаунчер консоли (он держит GPU/fb0)
# 2) убивает старую копию игры, если висит
# 3) запускает игру в фоне и показывает лог

set -e

GOF_ROOT="${GOF_ROOT:-/root/gof2hd}"
RUN_DIR="$GOF_ROOT/port/run-native"
LOG_FILE="${GOF_LOG:-$GOF_ROOT/run.txt}"

APK="$GOF_ROOT/base.apk"
OBB=$(ls "$GOF_ROOT"/obb/*/main.*.obb "$GOF_ROOT"/obb/main.*.obb 2>/dev/null | head -1)
DATA="$GOF_ROOT/data"
DATA_DIR="${DATA%/}/"

echo "== GOF2HD launcher =="

# 1) лаунчер прошьвки (dmenu/retroarch) держит fb0 и GPU
if pgrep -f 'dmenu.bin|muos.bin|loadapp' >/dev/null 2>&1; then
    echo "[1] останавливаю лаунчер консоли..."
    /etc/init.d/launcher.sh stop >/dev/null 2>&1 || true
    sleep 1
fi

# 2) убить старый инстанс игры
OLD=$(pgrep -f '/root/gof2hd/base.apk' || true)
if [ -n "$OLD" ]; then
    echo "[2] убиваю старую игру: $OLD"
    kill -9 $OLD 2>/dev/null || true   # SIGTERM движок глотает — только SIGKILL
    for i in 1 2 3; do
        pgrep -f '/root/gof2hd/base.apk' >/dev/null 2>&1 || break
        sleep 1
    done
fi

if [ -z "$OBB" ]; then
    echo "не найден main.*.obb в $GOF_ROOT/obb" >&2
    exit 1
fi

# 3) запуск
echo "[3] запускаю игру"
cd "$RUN_DIR"
# Движок склеивает appRootDir и имя FEV напрямую. Завершающий слэш нужен для
# загрузки data/FMOD_GOF2.fev. FMOD ищет FSB рядом с FEV, поэтому даём ему
# ссылки на банки из data/audio без дублирования сотен мегабайт аудио-данных.
if ls "$DATA_DIR"audio/*.fsb >/dev/null 2>&1; then
    for f in "$DATA_DIR"audio/*.fsb; do
        b=$(basename "$f")
        [ -e "$DATA_DIR$b" ] || [ -L "$DATA_DIR$b" ] || ln -s "audio/$b" "$DATA_DIR$b"
    done
fi
export GOF_SHOW_CURSOR=1
export SDL_AUDIODRIVER=alsa
# FMOD streams FEV/FSB through its thread-safe POSIX filesystem callback.
export LD_PRELOAD="$RUN_DIR/libfmod_filesystem.so:$RUN_DIR/cpuinfo_fake.so:$RUN_DIR/pthread_bionic.so"
# Android FMOD is softfp but glibc libm is hardfp.  Preload the shared bionic
# libm bridge first so FMOD's unversioned math imports use the safe ABI.
export LD_PRELOAD="$RUN_DIR/libm.so:$LD_PRELOAD"
export LD_LIBRARY_PATH=.:/usr/lib32
: > "$LOG_FILE"
setsid ./gof2hd "$APK" "$OBB" "$DATA_DIR" >"$LOG_FILE" 2>&1 </dev/null &

sleep 3
PID=$(pgrep -f '/root/gof2hd/base.apk' || true)
if [ -n "$PID" ]; then
    echo "== игра запущена: PID $PID =="
    echo "лог: $LOG_FILE  (следить: tail -f $LOG_FILE)"
    echo "управление: встроенный геймпад консоли (стик/крестовина - курсор, A - тап, B - назад)"
else
    echo "== игра не поднялась, лог: ==" 1>&2
    tail -20 "$LOG_FILE" 1>&2
    exit 1
fi
