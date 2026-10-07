#!/bin/sh
# run.sh: start, stop or check a Mru Flight experiment run (OPS-SAT or any Linux).
#
#   ./run.sh start     start a run in the background
#   ./run.sh status    show whether it is running, and the summary when done
#   ./run.sh stop      stop it cleanly (SIGTERM, then SIGKILL after 30 s)
#
# Settings come from environment variables (defaults in brackets). Use the
# same settings for start, status and stop:
#   QUORUM       the quorum binary                 [./quorum next to this script]
#   OUT_DIR      where the log and summary go      [./out]
#   POLICY       shrink or tmr                     [shrink]
#   SEED         random seed                       [1]
#   CPU_PERCENT  share of one CPU                  [5]
#   MEMORY_MB    memory limit per process          [128]
#   MAX_SECONDS  maximum run time                  [86400]
#   SENSOR_MB    radiation sensor size, 0 is off   [64]
#   FAULTS_FILE  fault schedule                    [./faults.txt if present]
#
# Outputs in OUT_DIR: quorum-POLICY.csv (event log), summary-POLICY.txt
# (summary, written at the end), stdout-POLICY.txt, quorum.pid while running.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
QUORUM=${QUORUM:-$here/quorum}
OUT_DIR=${OUT_DIR:-$here/out}
POLICY=${POLICY:-shrink}
SEED=${SEED:-1}
CPU_PERCENT=${CPU_PERCENT:-5}
MEMORY_MB=${MEMORY_MB:-128}
MAX_SECONDS=${MAX_SECONDS:-86400}
SENSOR_MB=${SENSOR_MB:-64}
FAULTS_FILE=${FAULTS_FILE:-$here/faults.txt}
pid_file=$OUT_DIR/quorum.pid

running() { [ -f "$pid_file" ] && kill -0 "$(cat "$pid_file")" 2>/dev/null; }

case "${1:-}" in
start)
    if running; then
        echo "already running (pid $(cat "$pid_file"))"
        exit 1
    fi
    mkdir -p "$OUT_DIR"
    set -- --policy "$POLICY" --seed "$SEED" --ticks 1000000000000 \
        --cpu-percent "$CPU_PERCENT" --max-memory-mb "$MEMORY_MB" \
        --max-seconds "$MAX_SECONDS" --sensor-mb "$SENSOR_MB" \
        --log "$OUT_DIR/quorum-$POLICY.csv" --summary "$OUT_DIR/summary-$POLICY.txt"
    if [ -f "$FAULTS_FILE" ]; then
        set -- "$@" --faults-file "$FAULTS_FILE"
    fi
    "$QUORUM" "$@" >"$OUT_DIR/stdout-$POLICY.txt" 2>&1 &
    echo $! >"$pid_file"
    echo "started (pid $!), output in $OUT_DIR"
    ;;
stop)
    if ! running; then
        echo "not running"
        exit 0
    fi
    pid=$(cat "$pid_file")
    kill -TERM "$pid"
    i=0
    while kill -0 "$pid" 2>/dev/null && [ "$i" -lt 30 ]; do
        sleep 1
        i=$((i + 1))
    done
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" # replicas exit by themselves when the voter is gone
        echo "stopped with SIGKILL"
    else
        echo "stopped"
    fi
    rm -f "$pid_file"
    ;;
status)
    if running; then
        echo "running (pid $(cat "$pid_file"))"
    else
        echo "not running"
        cat "$OUT_DIR/summary-$POLICY.txt" 2>/dev/null || true
    fi
    ;;
*)
    echo "usage: $0 start|stop|status"
    exit 2
    ;;
esac
