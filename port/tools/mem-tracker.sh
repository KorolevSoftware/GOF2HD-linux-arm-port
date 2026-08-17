#!/bin/sh
# mem-tracker.sh PID — пофайловый RSS процесса каждые 10 с.
# Помогает найти, какой файл/регион растёт (OBB? FSB? anon? GPU-пин).
# Запуск: setsid sh mem-tracker.sh <pid> >/dev/null 2>&1 </dev/null &
PID=$1
OUT=/root/gof2hd/mem_track.txt
: > "$OUT"
while [ -d "/proc/$PID" ]; do
    echo "=== t=$(date +%s) ===" >> "$OUT"
    grep -E 'VmPeak|VmSize|VmRSS|VmData' "/proc/$PID/status" >> "$OUT" 2>/dev/null
    echo "fds=$(ls "/proc/$PID/fd" 2>/dev/null | wc -l)" >> "$OUT"
    awk 'BEGIN{name="[anon]"}
         /^[0-9a-f]+-[0-9a-f]+/{name=($6!=""?$6:"[anon]")}
         /^Rss:/{rss[name]+=$2}
         END{for(n in rss) if(rss[n]>2048) printf "%9d kB  %s\n", rss[n], n}' \
        "/proc/$PID/smaps" 2>/dev/null | sort -rn >> "$OUT"
    sleep 10
done
echo "=== proc gone ===" >> "$OUT"
