#!/bin/sh
# catch-hang.sh — поймать момент зависания GOF2HD и снять диагностику.
#
# Детектирует:
#   1) спин: поток с CPU >= ~90% дольше ~6 с (симптом "одно ядро на 100%"),
#   2) замирание главного потока (render loop не тикает) при живом процессе.
# При срабатывании делает gcore (core со стеками всех потоков) и
# gdb "thread apply all bt" (текстовый дамп) в /root/gof2hd/hang_capture/.
#
# Запуск:
#   setsid sh catch-hang.sh <pid> >/dev/null 2>&1 </dev/null &

PID=$1
OUTDIR=/root/gof2hd/hang_capture
mkdir -p "$OUTDIR"
LOG="$OUTDIR/detect.log"
PREV="$OUTDIR/.prev"
: > "$LOG"
: > "$PREV"

spins=0
stalled=0
spin_tid=

while [ -d "/proc/$PID" ]; do
    NOW="$OUTDIR/.now"
    : > "$NOW"
    for t in /proc/$PID/task/*; do
        [ -d "$t" ] || continue
        tid=${t##*/}
        ut=$(awk '{print $14}' "$t/stat" 2>/dev/null)
        echo "$tid $ut" >> "$NOW"
    done

    main_delta=0
    while read tid ut; do
        prev=$(awk -v k="$tid" '$1==k {print $2}' "$PREV" 2>/dev/null)
        [ -n "$prev" ] || continue
        d=$((ut - prev))
        if [ "$tid" = "$PID" ]; then main_delta=$d; fi
        if [ "$d" -ge 180 ]; then
            if [ "$tid" = "$spin_tid" ]; then spins=$((spins+1)); else spins=1; spin_tid=$tid; fi
            echo "spin tid=$tid delta=$d hits=$spins" >> "$LOG"
        fi
    done < "$NOW"

    if [ "$spins" -ge 3 ]; then
        ts=$(date +%s)
        echo "=== CAPTURE spin tid=$spin_tid ts=$ts ===" >> "$LOG"
        gcore -o "$OUTDIR/core_spin_${spin_tid}_${ts}" "$PID" >> "$LOG" 2>&1
        gdb -p "$PID" -batch -ex 'set pagination off' \
            -ex 'thread apply all bt' \
            > "$OUTDIR/bt_spin_${spin_tid}_${ts}.txt" 2>&1 &
        spins=0; spin_tid=
        sleep 20
    fi

    if [ "$main_delta" -lt 5 ] && [ -d "/proc/$PID" ]; then
        stalled=$((stalled+1))
        if [ "$stalled" -ge 15 ]; then
            ts=$(date +%s)
            echo "=== CAPTURE main-thread stall ts=$ts main_delta=$main_delta ===" >> "$LOG"
            gcore -o "$OUTDIR/core_stall_${ts}" "$PID" >> "$LOG" 2>&1
            gdb -p "$PID" -batch -ex 'set pagination off' \
                -ex 'thread apply all bt' \
                > "$OUTDIR/bt_stall_${ts}.txt" 2>&1 &
            stalled=0
            sleep 20
        fi
    else
        stalled=0
    fi

    mv "$NOW" "$PREV"
    sleep 2
done
echo "process $PID gone" >> "$LOG"
