#!/bin/sh
# Board bench: run the bench, and a reset test, on a Linux board over SSH.
#
#   scripts/board.sh HOST QUORUM [--quick]      scenario checks, then the bench
#   scripts/board.sh HOST QUORUM --resets N     N resets during one long run
#
# HOST is an ssh destination. QUORUM is a static quorum binary for the board
# (from a tagged release or the CI artifacts). CORE_BIN, a static bench binary
# for the board, adds the timing of the core. Results come back to
# bench/results/.
#
# A reset is, by default, an immediate reboot without sync (sysrq "b"): the
# nearest a board can come to a power cut by itself. It needs passwordless
# sudo on the board. RESET_CMD replaces it with a command run here, for
# example one that switches a relay or smart plug off and on: a real power
# cut. RESET=process kills the voter instead and reboots nothing, to test this
# script on any Linux host.
#
# BOARD_DIR    folder on the board        [mru-bench, in the home folder]
# BENCH_HOST   name in the results file   [board-<arch>]
# RESET_TICKS  length of the reset run    [100000]
set -eu
cd "$(dirname "$0")/.."

[ $# -ge 2 ] || { sed -n '2,23p' "$0"; exit 2; }
host=$1
bin=$2
shift 2
mode=bench
quick=""
resets=0
case "${1:-}" in
    --quick) quick=--quick ;;
    --resets) mode=resets; resets=${2:?--resets needs a count} ;;
    "") ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
esac

dir=${BOARD_DIR:-mru-bench}
on() { ssh -o ConnectTimeout=10 -o BatchMode=yes "$host" "$@"; }
arch=$(on uname -m)
name=${BENCH_HOST:-board-$arch}
commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
date=$(date -u +%Y-%m-%d)
mkdir -p bench/results

on "mkdir -p $dir/scripts $dir/opssat $dir/state"
scp -q "$bin" "$host:$dir/quorum"
scp -q scripts/bench.sh scripts/scenarios.sh "$host:$dir/scripts/"
scp -q opssat/run.sh opssat/faults.example.txt "$host:$dir/opssat/"
[ -n "${CORE_BIN:-}" ] && scp -q "$CORE_BIN" "$host:$dir/bench-core"
on "chmod +x $dir/quorum $dir/scripts/*.sh $dir/opssat/run.sh; [ -e $dir/bench-core ] && chmod +x $dir/bench-core; true"

if [ "$mode" = bench ]; then
    core=""
    [ -n "${CORE_BIN:-}" ] && core="CORE_BIN=./bench-core"
    on "cd $dir && QUORUM_BIN=./quorum sh scripts/scenarios.sh" | tee "bench/results/$date-$name-scenarios.txt"
    on "cd $dir && QUORUM_BIN=./quorum $core BENCH_HOST=$name BENCH_COMMIT=$commit sh scripts/bench.sh $quick"
    scp -q "$host:$dir/bench/results/$date-$name*" bench/results/
    echo "results in bench/results/$date-$name*"
    exit 0
fi

# ---- reset test

T=${RESET_TICKS:-100000}
args="--policy shrink --ticks $T --seed 1 --upset-rate 0.001 --cpu-percent 50 \
--fault kill:0@$((T / 4)) --fault stuck:1@$((T / 2))"
state="$dir/state"

echo "reference run, $T ticks, no resets"
want=$(on "cd $dir && ./quorum $args" | sed -n '2p;5p')

on "rm -f $state/cp $state/cp.tmp $state/log.csv $state/summary.txt"
start() {
    on "cd $dir && nohup ./quorum $args --checkpoint state/cp --log state/log.csv \
        --summary state/summary.txt </dev/null >state/out.txt 2>&1 & echo \$! >state/pid"
}
finished() { on "test -s $state/summary.txt" 2>/dev/null; }
up() {
    i=0
    until on true 2>/dev/null; do
        i=$((i + 1))
        [ "$i" -lt 120 ] || { echo "board did not come back" >&2; exit 1; }
        sleep 5
    done
}
reset() {
    if [ -n "${RESET_CMD:-}" ]; then
        sh -c "$RESET_CMD"
    elif [ "${RESET:-}" = process ]; then
        on "kill -KILL \$(cat $state/pid) 2>/dev/null" || true
    else
        # no sync: what was not on disk is lost, as in a power cut
        on "sudo sh -c 'echo 1 >/proc/sys/kernel/sysrq; echo b >/proc/sysrq-trigger'" >/dev/null 2>&1 &
        sleep 5
        kill "$!" 2>/dev/null || true
    fi
}

start
log=bench/results/$date-$name-resets.txt
: >"$log"
done_resets=0
while [ "$done_resets" -lt "$resets" ] && ! finished; do
    wait_s=$(awk -v s="$done_resets" 'BEGIN { srand(s + 1); printf "%d", 3 + rand() * 12 }')
    sleep "$wait_s"
    finished && break
    t0=$(date +%s)
    reset
    [ "${RESET:-}" = process ] || { sleep 10; up; }
    # the run may have ended just before the reset: then it needs no restart
    finished && break
    done_resets=$((done_resets + 1))
    start
    t1=$(date +%s)
    echo "reset $done_resets after ${wait_s}s, back after $((t1 - t0))s" | tee -a "$log"
done
until finished; do sleep 5; done

got=$(on "sed -n '2p;5p' $state/summary.txt")
resumed=$(on "grep -c ',resumed,' $state/log.csv" || true)
{
    echo "host: $name ($arch), commit $commit, $T ticks"
    echo "reset: ${RESET_CMD:+command: $RESET_CMD}${RESET_CMD:-${RESET:-sysrq reboot}}"
    echo "resets: $done_resets, resumes logged: $resumed"
    echo "reference: $want"
    echo "with resets: $got"
} | tee -a "$log"
if [ "$got" = "$want" ]; then
    echo "same counts with $done_resets resets" | tee -a "$log"
else
    echo "COUNTS DIFFER" | tee -a "$log"
    exit 1
fi
