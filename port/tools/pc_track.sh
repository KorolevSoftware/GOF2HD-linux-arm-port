#!/bin/bash
# pc_track.sh — на ПК: следит за gof2hd на девайсе (только по /proc, без ptrace).
# Каждые 5 с пишет: время, VmRSS/VmSize, mali0MB (из maps), gltex.
# Если VmRSS вырос >50MB за сэмпл — снимает maps+smaps+gltex+status (быстро).
export SSH_ASKPASS=/tmp/opencode/askpass.sh SSH_ASKPASS_REQUIRE=force DISPLAY=dummy:0
DEV=root@192.168.0.128
OUT=/tmp/opencode/mem_curve.txt
SNAP=/tmp/opencode/snaps
mkdir -p "$SNAP"
: > "$OUT"
prev=0
for i in $(seq 1 800); do
    data=$(timeout 12 ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no "$DEV" '
        PID=$(pgrep -f "^\./gof2hd" | head -1)
        [ -z "$PID" ] && { echo "NOPROC"; exit 0; }
        echo -n "PID=$PID "
        awk "/VmRSS|VmSize/{printf \"%s=%s \", \$1, \$2}" /proc/$PID/status
        m0=$(awk "/\/dev\/mali0/{split(\$1,a,\"-\"); s+=strtonum(\"0x\"a[2])-strtonum(\"0x\"a[1])} END{print int(s/1048576)}" /proc/$PID/maps 2>/dev/null)
        echo -n "mali0MB=$m0 "
        grep gltex /root/gof2hd/run.txt 2>/dev/null | tail -1
    ' 2>/dev/null)
    if [ -n "$data" ] && ! echo "$data" | grep -q "NOPROC"; then
        ts=$(date +%s)
        echo "$ts $data" >> "$OUT"
        vmr=$(echo "$data" | grep -oE "VmRSS:?=[0-9]+" | head -1 | grep -oE "[0-9]+")
        if [ -n "$vmr" ] && [ "$prev" -gt 0 ]; then
            d=$((vmr - prev))
            if [ "$d" -gt 50000 ]; then
                echo "=== VMRSS SPIKE +$d KB ts=$ts pid=$(echo "$data"|grep -oE 'PID=[0-9]+'|cut -d= -f2) ===" >> "$OUT"
                timeout 25 ssh -o ConnectTimeout=8 "$DEV" "PID=\$(pgrep -f '^\\./gof2hd' | head -1); if [ -d /proc/\$PID ]; then cp /proc/\$PID/maps $SNAP/maps_$ts.txt 2>/dev/null; cp /proc/\$PID/smaps $SNAP/smaps_$ts.txt 2>/dev/null; grep -E 'VmPeak|VmSize|VmRSS|VmData' /proc/\$PID/status > $SNAP/status_$ts.txt 2>/dev/null; grep gltex /root/gof2hd/run.txt | tail -3 > $SNAP/gltex_$ts.txt 2>/dev/null; dmesg | tail -30 > $SNAP/dmesg_$ts.txt 2>/dev/null; echo captured-\$PID; else echo pid-gone; fi" 2>/dev/null >> "$OUT"
                sleep 12
            fi
        fi
        prev=$vmr
    fi
    sleep 5
done
