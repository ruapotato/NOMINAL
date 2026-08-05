#!/bin/sh
# bench.sh — run the support bench, and keep running it.
#
# Playtests kept dying mid-session because the server went away: sometimes a
# rebuild replaced the binary underneath it, sometimes it simply exited. A
# playtester with half a diagnosis and no socket is a wasted run, so this
# restarts it and records why it stopped.
#
# THE HOT LOOP. The first version restarted unconditionally with a one second
# sleep. Start a second copy while the first still holds the port and it can
# never bind, so it exits immediately, sleeps a second, and tries again --
# forever. Five of these accumulated across one working session and between
# them burned 1341 restarts on a single log. Back when the language model was
# linked in, every one of those attempts loaded 1.8 GB of weights before
# discovering it could not have the port, which is how a supervisor for a text
# server ended up pinned at 100% of a core and holding 2.8 GB. The weights are
# gone and a restart is cheap now; the loop is still a bug, so the guards stay.
#
# Two guards, because either alone is not enough:
#   1. Refuse to start at all if something already holds the port.
#   2. Give up if the child keeps dying immediately. A server that lives less
#      than a few seconds has not failed at serving, it has failed at
#      starting, and restarting it will not change that.
#
# AND IT STOPS ON ITS OWN. The hot-loop guards below fixed a supervisor that
# would not stop restarting; they did nothing about a supervisor nobody
# stopped. A bench left up after a playtest sat idle for an hour and a half
# holding 3.3 GB, because the model was loaded whether anyone was connected or
# not, and David spotted it in the process table rather than me. The weights
# are gone, so an idle bench is cheap -- but a stray process on his desktop is
# what he actually objected to, and that has not changed. A playtest is hours,
# not days; MAXMIN is the outer bound and can be raised deliberately.
PORT=${1:-7777}
SEED=${2:-12000}
LOG=${3:-/tmp/nominal-bench.log}
MAXMIN=${4:-180}
BIN=${BIN:-./build/bf}
BEGAN=$(date +%s)

if command -v ss >/dev/null 2>&1 && ss -ltn 2>/dev/null | grep -q ":$PORT "; then
    echo "bench: port $PORT is already in use -- refusing to start" >&2
    echo "bench: port $PORT already in use, refused to start" >> "$LOG"
    exit 1
fi

FAST=0
while true; do
    echo "=== bench starting on $PORT (seed $SEED)" >> "$LOG"
    START=$(date +%s)
    "$BIN" --serve "$PORT" "$SEED" >> "$LOG" 2>&1
    RC=$?
    END=$(date +%s)
    RAN=$((END - START))
    echo "=== bench exited with $RC after ${RAN}s" >> "$LOG"

    if [ "$RAN" -lt 5 ]; then
        FAST=$((FAST + 1))
        if [ "$FAST" -ge 3 ]; then
            echo "bench: died in under 5s three times running -- giving up" >&2
            echo "bench: gave up after 3 immediate failures (last rc=$RC)" >> "$LOG"
            exit 1
        fi
    else
        FAST=0
    fi

    if [ "$MAXMIN" -gt 0 ]; then
        RUNMIN=$(( ($(date +%s) - BEGAN) / 60 ))
        if [ "$RUNMIN" -ge "$MAXMIN" ]; then
            echo "bench: ${RUNMIN}m old, past the ${MAXMIN}m limit -- stopping" >&2
            echo "bench: stopped after ${RUNMIN}m (limit ${MAXMIN}m)" >> "$LOG"
            exit 0
        fi
    fi

    SEED=$((SEED + 100))
    sleep 1
done
