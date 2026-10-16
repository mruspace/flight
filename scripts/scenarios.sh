#!/bin/sh
# Scenario tests for the quorum demo. Each check runs the real program and
# asserts on its output. Exit status is non-zero if any check fails.
set -eu
cd "$(dirname "$0")/.."

# QUORUM_BIN tests a prebuilt binary (for example a static ARM build under
# emulation); otherwise the local release build is used.
if [ -z "${QUORUM_BIN:-}" ]; then
    cargo build --release --quiet
fi

Q=${QUORUM_BIN:-target/release/quorum}
fails=0
pass() { printf 'ok    %s\n' "$1"; }
fail() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }

# field <name> <output>: print the value of name=value from a result line
field() { printf '%s\n' "$2" | tr ' ' '\n' | sed -n "s/^$1=//p" | head -n 1; }

run() { "$Q" "$@" | sed -n 2p; }

# 1. With only random upsets, voting masks every one of them.
for p in tmr shrink; do
    out=$(run --policy "$p" --ticks 20000 --seed 1 --upset-rate 0.001)
    if [ "$(field useful "$out")" = 20000 ] && [ "$(field wrong "$out")" = 0 ]; then
        pass "$p: upsets only, all 20000 results correct"
    else
        fail "$p: upsets only ($out)"
    fi
done

# 2. Two replicas die: fixed TMR halts at the second loss, the shrinking quorum
#    keeps going on self-check and delivers more.
t=$(run --policy tmr --ticks 20000 --seed 1 --upset-rate 0.001 --fault kill:0@5000 --fault kill:1@10000)
s=$(run --policy shrink --ticks 20000 --seed 1 --upset-rate 0.001 --fault kill:0@5000 --fault kill:1@10000)
[ "$(field halted_at "$t")" = 10000 ] && pass "tmr: halts at the second loss" || fail "tmr: halt ($t)"
[ "$(field halted_at "$s")" = never ] && pass "shrink: keeps running after the second loss" || fail "shrink: halt ($s)"
[ "$(field useful "$s")" -gt "$(field useful "$t")" ] && pass "shrink: more correct results than tmr" || fail "shrink vs tmr ($s / $t)"

# 3. A replica gets stuck while three are alive: health scoring retires it.
log=$(mktemp)
run --policy shrink --ticks 6000 --seed 1 --fault stuck:1@4000 --log "$log" >/dev/null
retired=$(sed -n 's/^\([0-9]*\),retired,1$/\1/p' "$log" | head -n 1)
if [ -n "$retired" ] && [ "$retired" -le 4005 ]; then
    pass "shrink: stuck replica retired at tick $retired by health scoring"
else
    fail "shrink: stuck replica not retired by health scoring"
fi

# 4. A replica gets stuck with two alive: the known-answer test finds it.
run --policy shrink --ticks 12000 --seed 1 --fault kill:0@5000 --fault stuck:1@10000 --log "$log" >/dev/null
retired=$(sed -n 's/^\([0-9]*\),retired,1$/\1/p' "$log" | head -n 1)
if [ -n "$retired" ] && [ "$retired" -le 10001 ]; then
    pass "shrink: stuck replica found by the known-answer test at tick $retired"
else
    fail "shrink: known-answer test did not find the stuck replica"
fi
rm -f "$log"

# 5. Across seeds and fault times, the shrinking quorum never delivers fewer
#    correct results than fixed TMR on the same faults.
worse=0
for seed in 1 2 3 4 5 6 7 8 9 10; do
    k1=$((2000 + seed * 300))
    k2=$((8000 + seed * 500))
    a=$(run --policy tmr --ticks 20000 --seed "$seed" --upset-rate 0.001 --fault kill:2@$k1 --fault kill:0@$k2)
    b=$(run --policy shrink --ticks 20000 --seed "$seed" --upset-rate 0.001 --fault kill:2@$k1 --fault kill:0@$k2)
    [ "$(field useful "$b")" -ge "$(field useful "$a")" ] || worse=$((worse + 1))
done
[ "$worse" = 0 ] && pass "shrink: never fewer correct results than tmr over 10 seeds" || fail "shrink: fewer than tmr on $worse seeds"

# 6. A replica that stops answering is treated as lost after the reply timeout,
#    and the shrinking quorum carries on.
log=$(mktemp)
out=$(run --policy shrink --ticks 3000 --seed 1 --reply-timeout-ms 200 --fault hang:1@1000 --log "$log")
lost=$(sed -n 's/^\([0-9]*\),lost,1$/\1/p' "$log" | head -n 1)
if [ "$lost" = 1000 ] && [ "$(field halted_at "$out")" = never ] && [ "$(field useful "$out")" -ge 2990 ]; then
    pass "shrink: hung replica dropped after the reply timeout, run continues"
else
    fail "shrink: hung replica ($out, lost at '$lost')"
fi
rm -f "$log"

# 7. SIGTERM stops the run cleanly: summary printed, exit status 0, no replica left.
out_file=$(mktemp)
"$Q" --policy shrink --ticks 100000000 --seed 1 --cpu-percent 50 >"$out_file" &
pid=$!
sleep 1
kill -TERM "$pid"
if wait "$pid" && grep -q "stopped=signal" "$out_file" && ! pgrep -f "quorum --replica" >/dev/null; then
    pass "stop: SIGTERM ends the run cleanly, replicas exit"
else
    fail "stop: SIGTERM ($(cat "$out_file"))"
fi
rm -f "$out_file"

# 8. A maximum run time is enforced.
out=$(run --policy shrink --ticks 100000000 --seed 1 --max-seconds 1)
[ "$(field stopped "$out")" = time-limit ] && pass "stop: maximum run time enforced" || fail "stop: max run time ($out)"

# 9. The CPU share is throttled: at 5%, total CPU time stays near 5% of wall time.
fp=$("$Q" --policy shrink --ticks 100000000 --seed 1 --cpu-percent 5 --max-seconds 3 | sed -n 3p)
cpu=$(field cpu "$fp" | tr -d s)
wall=$(field wall "$fp" | tr -d s)
if awk -v c="$cpu" -v w="$wall" 'BEGIN { exit !(c / w < 0.08) }'; then
    pass "cpu share: ${cpu}s CPU over ${wall}s wall at --cpu-percent 5"
else
    fail "cpu share too high (${cpu}s over ${wall}s)"
fi

# 10. On Linux, the memory limit is applied by the kernel to the voter and to
#     every replica (read back from /proc).
# (Skipped under user-mode emulation, which does not pass this limit through.)
if [ "$(uname)" = Linux ] && [ -z "${QUORUM_EMULATED:-}" ]; then
    "$Q" --policy shrink --ticks 100000000 --seed 1 --cpu-percent 50 --max-memory-mb 128 >/dev/null &
    pid=$!
    sleep 1
    want=134217728
    ok=1
    for p in "$pid" $(pgrep -P "$pid"); do
        got=$(awk '/Max address space/ { print $4 }' "/proc/$p/limits")
        [ "$got" = "$want" ] || ok=0
    done
    kill -TERM "$pid"; wait "$pid" || true
    [ "$ok" = 1 ] && pass "limits: 128 MB address-space limit on voter and replicas" || fail "limits: memory limit not applied"
fi

# 11. The radiation sensor finds and reports a bit flip in its memory.
log=$(mktemp)
out=$("$Q" --policy shrink --ticks 500 --seed 1 --sensor-mb 8 --sensor-every 100 --fault sensor:0@150 --log "$log")
if grep -q '^200,sensor_upset,' "$log" && printf '%s\n' "$out" | grep -q 'flips=1'; then
    pass "sensor: injected bit flip found at the next scan and reported"
else
    fail "sensor: injected flip not found ($out)"
fi
rm -f "$log"

# 12. A 64 MB sensor fits within the 128 MB memory limit; an oversized one is refused.
out=$("$Q" --policy shrink --ticks 2000 --seed 1 --sensor-mb 64 --sensor-every 1000 --max-memory-mb 128 | sed -n 5p)
if [ "$(field scans "$out")" = 2 ] && ! "$Q" --sensor-mb 120 --max-memory-mb 128 >/dev/null 2>&1; then
    pass "sensor: 64 MB runs under the 128 MB limit, oversize refused"
else
    fail "sensor: memory limit interplay ($out)"
fi

# 13. A fault schedule from a file gives the same run as the same faults on the command line.
faults=$(mktemp)
printf '# test schedule\nstuck:1@4000\n\nkill:0@9000\n' >"$faults"
a=$(run --policy shrink --ticks 20000 --seed 1 --upset-rate 0.001 --faults-file "$faults")
b=$(run --policy shrink --ticks 20000 --seed 1 --upset-rate 0.001 --fault stuck:1@4000 --fault kill:0@9000)
[ "$a" = "$b" ] && pass "faults file: same result as command-line faults" || fail "faults file ($a / $b)"
rm -f "$faults"

# 14. The launcher starts a run, reports it, and stops it cleanly with a summary.
dir=$(mktemp -d)
export QUORUM="$PWD/$Q" OUT_DIR="$dir" SENSOR_MB=8 CPU_PERCENT=20 FAULTS_FILE=/nonexistent
case "$Q" in /*) QUORUM="$Q" ;; esac
./opssat/run.sh start >/dev/null
sleep 1
st=$(./opssat/run.sh status)
./opssat/run.sh stop >/dev/null
if [ "${st#running}" != "$st" ] && grep -q 'stopped=signal' "$dir/summary-shrink.txt" && ! pgrep -f "quorum --replica" >/dev/null; then
    pass "launcher: start, status and stop work, summary written"
else
    fail "launcher ($st)"
fi
unset QUORUM OUT_DIR SENSOR_MB CPU_PERCENT FAULTS_FILE
rm -rf "$dir"

# 15. Footprint stays well inside the planned OPS-SAT targets (128 MB memory).
fp=$("$Q" --policy shrink --ticks 20000 --seed 1 --upset-rate 0.001 | sed -n 3p)
voter=$(field voter_max_rss "$fp" | tr -dc 0-9)
replica=$(field replica_max_rss "$fp" | tr -dc 0-9)
if [ "$voter" -lt 131072 ] && [ "$replica" -lt 131072 ]; then
    pass "footprint: voter ${voter} KiB, replica ${replica} KiB"
else
    fail "footprint too large ($fp)"
fi

# 16. Progress lines give the running totals, ending on the summary's count.
log=$(mktemp)
out=$(run --policy tmr --ticks 20000 --seed 1 --upset-rate 0.001 --fault kill:0@5000 --fault kill:1@10000 --progress-every 1000 --log "$log")
n=$(grep -c ',progress,' "$log")
last=$(grep ',progress,' "$log" | tail -n 1)
if [ "$n" = 10 ] && [ "$(printf '%s' "$last" | sed 's/^\([0-9]*\),.*useful=\([0-9]*\).*/\1 \2/')" = "9999 $(field useful "$out")" ]; then
    pass "progress: one line per 1000 ticks, last one matches the summary"
else
    fail "progress lines ($n, $last)"
fi
rm -f "$log"

# 17. Detection latency: a stuck fault on the last replica is caught by the
#     periodic known-answer test within one test period (64 ticks).
out=$("$Q" --policy shrink --ticks 20000 --seed 1 --fault kill:0@5000 --fault kill:1@8000 --fault stuck:2@10000)
det=$(printf '%s\n' "$out" | grep '^detection:')
if [ "$(field caught "$det")" = 1 ] && [ "$(field latency_max "$det")" -le 64 ]; then
    pass "detection: stuck last replica caught $(field latency_max "$det") ticks after the fault"
else
    fail "detection latency ($det)"
fi

# 18. A reset loses nothing: kill -9 the voter during the run, start it again
#     with the same options, and it resumes from its checkpoint to the same
#     counts as a run that was never interrupted. The log keeps every event up
#     to the checkpoint, once: a reset can land after the log was flushed and
#     before the checkpoint was renamed into place (seen in CI under emulation,
#     16 Oct 2026), so one event past the checkpoint is planted in the log and
#     must be gone after the resume. A checkpoint from another run is refused.
dir=$(mktemp -d)
set -- --policy shrink --ticks 12000 --seed 2 --upset-rate 0.01 --cpu-percent 60 \
    --fault kill:0@3000 --fault kill:1@5000 --fault stuck:2@9000
want=$("$Q" "$@" | sed -n '2p;5p')
"$Q" "$@" --checkpoint "$dir/cp" --log "$dir/log.csv" >/dev/null &
pid=$!
sleep 1
kill -KILL "$pid"
wait "$pid" 2>/dev/null || true
sleep 0.3
cp_tick=$(sed -n 's/^tick=//p' "$dir/cp")
printf '%s,killed,0\n' "$((cp_tick + 50))" >>"$dir/log.csv"
got=$("$Q" "$@" --checkpoint "$dir/cp" --log "$dir/log.csv" | sed -n '2p;5p')
at=$(sed -n 's/^\([0-9]*\),resumed,1$/\1/p' "$dir/log.csv")
printf 'run=other\ntick=5\n' >"$dir/other"
kills=$(grep -c ',killed,0$' "$dir/log.csv")
stale=$(grep -c "^$((cp_tick + 50)),killed,0$" "$dir/log.csv" || true)
if [ -n "$at" ] && [ "$at" = "$cp_tick" ] && [ "$got" = "$want" ] && [ ! -e "$dir/cp" ] &&
    [ "$(head -n 1 "$dir/log.csv")" = tick,event,detail ] && [ "$kills" = 1 ] && [ "$stale" = 0 ] &&
    ! "$Q" "$@" --checkpoint "$dir/other" >/dev/null 2>&1; then
    pass "reset: killed at tick $at, resumed to the same counts, stale log line removed"
else
    fail "reset: resume ($want / $got, resumed at '$at' from checkpoint tick '$cp_tick', $kills kill events, $stale stale)"
fi
set --
rm -rf "$dir"

# Checks 19 to 25 run replica 2 alone in self-check mode (replicas 0 and 1
# killed at ticks 10 and 20), with no random upsets, and put a fault on it.
# one <options>: the counts and the detection line, on one line
one() {
    "$Q" --policy shrink --upset-rate 0 --fault kill:0@10 --fault kill:1@20 "$@" |
        sed -n '2p;/^detection:/p' | tr '\n' ' '
}
caught_halted() { [ "$(field caught "$1")" = 1 ] && [ "$(field halted_at "$1")" != never ]; }
missed_wrong() {
    [ "$(field caught "$1")" = 0 ] && [ "$(field wrong "$1")" -gt 0 ] &&
        [ "$(field halted_at "$1")" = never ]
}

# 19. A result bit stuck at 0 or 1: the known-answer test catches it only when
#     the known result has the other value in that bit. Bit 0 of the known
#     result is 1, so stuck at 0 is caught and stuck at 1 is missed, and the
#     missed one delivers wrong results.
a=$(one --ticks 1524 --fault stuckbit:2@500/out.0.0)
b=$(one --ticks 1524 --fault stuckbit:2@500/out.0.1)
if caught_halted "$a" && [ "$(field latency_max "$a")" -le 64 ] && missed_wrong "$b"; then
    pass "stuck-at bit: out.0.0 caught after $(field latency_max "$a") ticks, out.0.1 missed with $(field wrong "$b") wrong results"
else
    fail "stuck-at bit in the result ($a / $b)"
fi

# 20. An input bit stuck at 0 is invisible to the known-answer test, whose
#     input is 0, but it gives wrong results; stuck at 1, it is caught.
a=$(one --ticks 1524 --fault stuckbit:2@500/in.3.0)
b=$(one --ticks 1524 --fault stuckbit:2@500/in.3.1)
if missed_wrong "$a" && caught_halted "$b"; then
    pass "stuck-at bit: in.3.0 missed with $(field wrong "$a") wrong results, in.3.1 caught"
else
    fail "stuck-at bit in the input ($a / $b)"
fi

# 21. An accumulator bit stuck at 1, with a test every 8 ticks: caught within 8.
a=$(one --ticks 1524 --kat-period 8 --fault stuckbit:2@500/acc.5.1)
if caught_halted "$a" && [ "$(field latency_max "$a")" -le 8 ]; then
    pass "stuck-at bit: acc.5.1 caught $(field latency_max "$a") ticks after the fault (test every 8)"
else
    fail "stuck-at bit in the accumulator ($a)"
fi

# 22. The last replica hangs: the reply timeout catches it at the fault tick.
a=$(one --ticks 600 --reply-timeout-ms 100 --fault hang:2@500)
if [ "$(field caught "$a")" = 1 ] && [ "$(field latency_max "$a")" = 0 ]; then
    pass "hang: last replica lost at the fault tick (latency 0)"
else
    fail "hang on the last replica ($a)"
fi

# 23. One bit flip in self-check: caught when only one run is hit, delivered
#     wrong when both runs are hit alike. A flip on an odd tick, which has no
#     computation in self-check mode, waits for the next one and is caught.
a=$(one --ticks 600 --common-mode 0 --fault corrupt:2@500)
b=$(one --ticks 600 --common-mode 1.0 --fault corrupt:2@500)
c=$(one --ticks 600 --common-mode 0 --fault corrupt:2@501)
if [ "$(field detected "$a")" = 1 ] && [ "$(field wrong "$a")" = 0 ] &&
    [ "$(field wrong "$b")" = 1 ] && [ "$(field caught "$b")" = 0 ] &&
    [ "$(field detected "$c")" = 1 ]; then
    pass "corrupt: caught alone, missed when common mode, odd tick not lost"
else
    fail "corrupt in self-check ($a / $b / $c)"
fi

# 24. A reset keeps a stuck-at bit: as in check 18, kill -9 the voter during a
#     run with a stuck-at bit the test cannot see (out.0.1, from check 19),
#     resume it, and get the same counts as a run never interrupted.
dir=$(mktemp -d)
set -- --policy shrink --ticks 30000 --seed 2 --upset-rate 0 \
    --fault kill:0@10 --fault kill:1@20 --fault stuckbit:2@500/out.0.1
want=$("$Q" "$@" | sed -n '2p;5p')
"$Q" "$@" --cpu-percent 20 --checkpoint "$dir/cp" --log "$dir/log.csv" >/dev/null &
pid=$!
# Kill only once a checkpoint holds the fault (tick 500 or later): a slow
# runner may not reach it in a fixed time. Give up after about 30 s.
n=0
until grep -q '^stuckbit=-,-,out' "$dir/cp" 2>/dev/null || [ "$n" -ge 150 ]; do
    sleep 0.2
    n=$((n + 1))
done
kill -KILL "$pid"
wait "$pid" 2>/dev/null || true
sleep 0.3
saved=$(grep '^stuckbit=' "$dir/cp" || true)
got=$("$Q" "$@" --cpu-percent 20 --checkpoint "$dir/cp" --log "$dir/log.csv" | sed -n '2p;5p')
at=$(sed -n 's/^\([0-9]*\),resumed,1$/\1/p' "$dir/log.csv")
if [ -n "$at" ] && [ "$got" = "$want" ] && [ "$saved" = "stuckbit=-,-,out.0.1" ] && [ ! -e "$dir/cp" ]; then
    pass "reset: stuck-at bit kept over a kill at tick $at, same counts"
else
    fail "reset with a stuck-at bit ($want / $got, resumed at '$at', saved '$saved')"
fi
set --
rm -rf "$dir"

# 25. Malformed stuck-at faults are refused with exit status 2.
bad=0
for f in stuckbit:2@500 stuckbit:2@500/out.64.1 stuckbit:2@500/block.32768.0 stuck:2@500/x; do
    rc=0
    "$Q" --ticks 10 --fault "$f" >/dev/null 2>&1 || rc=$?
    [ "$rc" = 2 ] || bad=$((bad + 1))
done
if [ "$bad" = 0 ]; then
    pass "faults: malformed stuck-at faults refused"
else
    fail "faults: $bad malformed fault(s) not refused with status 2"
fi

echo
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
