#!/bin/sh
# watch-hang.sh — диагностика зависания GOF2HD
# (симптомы: ~4 мин — весь девайс не реагирует + одно ядро на 100%;
#  ~10 мин — игра виснет намертво).
#
# Сэмплирует каждый TID процесса каждые 2 с (state/utime/stime/wchan/comm),
# RSS и loadavg; по истечении DUR шлёт SIGQUIT -> core со стеками ВСЕХ потоков,
# потом SIGKILL и снимает dmesg/meminfo.
#
# Запуск (игра должна быть стартована с ulimit -c unlimited):
#   setsid sh watch-hang.sh <pid> [duration_seconds] [out_file] >/dev/null 2>&1 &
#
# Core упадёт в CWD процесса игры (обычно run-native/).

PID=$1
DUR=${2:-570}
OUT=${3:-/root/gof2hd/trace.txt}

if [ -z "$PID" ] || [ ! -d "/proc/$PID" ]; then
    echo "usage: watch-hang.sh <pid> [seconds] [out]" >&2
    exit 1
fi

: > "$OUT"
END=$(( $(date +%s) + DUR ))

while [ "$(date +%s)" -lt "$END" ]; do
    NOW=$(date +%s)
    {
        echo "=== t=$NOW ==="
        [ -f "/proc/$PID/status" ] && grep -E 'VmPeak|VmSize|VmRSS|VmData|Threads' "/proc/$PID/status"
        for t in /proc/$PID/task/*; do
            [ -d "$t" ] || continue
            tid=${t##*/}
            comm=$(cat "$t/comm" 2>/dev/null)
            wchan=$(cat "$t/wchan" 2>/dev/null)
            # stat: $3 state, $14 utime, $15 stime
            st=$(awk '{print $3}' "$t/stat" 2>/dev/null)
            utime=$(awk '{print $14}' "$t/stat" 2>/dev/null)
            stime=$(awk '{print $15}' "$t/stat" 2>/dev/null)
            echo "tid=$tid state=$st utime=$utime stime=$stime wchan=$wchan comm=$comm"
        done
        cat /proc/loadavg
        free -m | head -2
    } >> "$OUT"
    sleep 2
done

{
    echo "=== dumping core (SIGQUIT pid=$PID) ==="
    kill -QUIT "$PID" 2>&1
    sleep 8
    if [ -d "/proc/$PID" ]; then
        echo "still alive, SIGKILL"
        kill -9 "$PID" 2>&1
    fi
    echo "=== dmesg tail ==="
    dmesg 2>/dev/null | tail -80
    echo "=== meminfo ==="
    grep -E 'MemFree|MemAvailable|Shmem|SwapTotal|SwapFree|Active|Inactive|Dirty' /proc/meminfo 2>/dev/null
    echo "=== core files ==="
    ls -la /root/gof2hd/port/run-native/core* 2>/dev/null
} >> "$OUT"
echo "watch done" >> "$OUT"
