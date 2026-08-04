#!/bin/sh
# bench.sh — run the support bench, and keep running it.
#
# Playtests kept dying mid-session because the server went away: sometimes a
# rebuild replaced the binary underneath it, sometimes it simply exited. A
# playtester with half a diagnosis and no socket is a wasted run, so this
# restarts it and records why it stopped.
PORT=${1:-7777}
SEED=${2:-12000}
LOG=${3:-/tmp/nominal-bench.log}
while true; do
    echo "=== bench starting on $PORT" >> "$LOG"
    ${BIN:-./build/bf} --serve "$PORT" "$SEED" >> "$LOG" 2>&1
    echo "=== bench exited with $? -- restarting" >> "$LOG"
    SEED=$((SEED + 100))
    sleep 1
done
