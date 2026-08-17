#!/bin/bash
# play-loop.sh — запускает GOF2HD в цикле: если игра умирает (OOM-киллер) или
# зависает (watchdog сам завершает процесс после дампа), скрипт перезапускает её.
# Это обход ресурсного потолка устройства: свежий процесс грузит сектора с малой
# памятью (накопление GPU-памяти драйвера mali сбрасывается при рестарте).
#
# Запуск:  bash /root/gof2hd/port/tools/play-loop.sh
set -u

GOF_ROOT="${GOF_ROOT:-/root/gof2hd}"
START="$GOF_ROOT/port/tools/start-game.sh"

echo "== play-loop: GOF2HD авто-перезапуск =="
n=0
while true; do
    n=$((n+1))
    echo "--- запуск #$n: $(date '+%H:%M:%S') ---"
    bash "$START"
    # ждём, пока процесс живёт, и следим
    while pgrep -f "^\./gof2hd" >/dev/null 2>&1; do
        sleep 2
    done
    echo "--- игра завершилась (#$n): $(date '+%H:%M:%S'), перезапуск через 3 c ---"
    sleep 3
done
