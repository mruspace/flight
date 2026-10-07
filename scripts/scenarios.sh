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

echo
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
