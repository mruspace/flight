#!/bin/sh
# fprime-deployment.sh: the OPS-SAT run 3 package, end to end with short caps,
# against a native build of the MruOpsSat deployment. Runs opssat/fprime/run
# with its three sequences, then checks each segment against the quorum demo
# on the same faults (MRU-SYS-001: the same wrong count, and a kill may cost
# up to three correct results; each cycle the manager withheld because it
# closed late, MRU-RM-023, may cost one more), the events on their cycles, the restore from
# the state file, and a second run in the same folder. Exit status is
# non-zero if any check fails.
#
#   MRU_DEPLOYMENT    the MruOpsSat binary   [fprime/build-artifacts/Linux/MruOpsSat/bin/MruOpsSat]
#   MRU_DICTIONARY    its dictionary         [fprime/build-artifacts/Linux/MruOpsSat/dict/MruOpsSatTopologyDictionary.json]
#   QUORUM_BIN        the demo binary        [target/release/quorum, built if missing]
set -eu
cd "$(dirname "$0")/.."
art=fprime/build-artifacts/Linux/MruOpsSat
deployment=${MRU_DEPLOYMENT:-$art/bin/MruOpsSat}
dictionary=${MRU_DICTIONARY:-$art/dict/MruOpsSatTopologyDictionary.json}
if [ -z "${QUORUM_BIN:-}" ]; then
    cargo build --release --quiet -p demo
fi
Q=${QUORUM_BIN:-target/release/quorum}
case "$deployment" in /*) ;; *) deployment="$PWD/$deployment" ;; esac

fails=0
pass() { printf 'ok    %s\n' "$1"; }
fail() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
field() { printf '%s\n' "$2" | tr ' ' '\n' | sed -n "s/^$1=//p" | head -n 1; }
abs() { [ "$1" -lt 0 ] && echo $((-$1)) || echo "$1"; }
# at TEXT CYCLE LATE FILE: an event line "TEXT at cycle N" with CYCLE <= N <=
# CYCLE + LATE. Each cycle the manager withheld (MRU-RM-023, a loaded CI
# runner) may move a later event by one cycle; with none withheld, exact.
# A retirement on a known answer may also log one cycle later: the answer can
# be dispatched just after the next cycle's tick.
at() {
    n=$(grep -F "$1 at cycle " "$4" | sed -n 's/.* at cycle \([0-9]*\).*/\1/p' | head -n 1)
    [ -n "$n" ] && [ "$n" -ge "$2" ] && [ "$n" -le $(($2 + $3)) ]
}

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cp opssat/fprime/run "$dir/run"
for s in shrink tmr restart; do
    fprime-seqgen --dictionary "$dictionary" "opssat/fprime/$s.seq" "$dir/$s.bin" >/dev/null
done

# 20 ms cycles: segment 1 and 2 reach cycle 400 in 8 s, past the kill at 240;
# segment 3 is killed near cycle 400, after the periodic saves at 300 and 400
env DEPLOYMENT="$deployment" CYCLE_MS=20 SHRINK_SECONDS=8 TMR_SECONDS=8 RESTART_AFTER=8 RESTART_SECONDS=3 \
    "$dir/run" >"$dir/run.out" 2>&1 || fail "run: exit status $? ($(tail -n 5 "$dir/toGround/run-info.txt" | tr '\n' ' '))"
out=$dir/toGround

# segments 1 and 2 against the demo on the cycles each one closed
for policy in shrink tmr; do
    s=$(sed -n 2p "$out/summary-$policy.txt")
    cycles=$(field cycles "$(sed -n 1p "$out/summary-$policy.txt")")
    ticks=$((cycles - 1))
    d=$("$Q" --policy "$policy" --ticks "$ticks" --seed 1 --fault corrupt:2@60 --fault stuck:1@120 --fault kill:0@240 | sed -n 2p)
    du=$(abs $(($(field useful "$s") - $(field useful "$d"))))
    dd=$(abs $(($(field detected "$s") - $(field detected "$d"))))
    slack=$((3 + $(field late "$s")))
    if [ "$cycles" -gt 300 ] && [ "$(field wrong "$s")" = 0 ] && [ "$(field wrong "$d")" = 0 ] &&
        [ "$du" -le "$slack" ] && [ "$dd" -le "$slack" ] && grep -q "sequence=ok" "$out/summary-$policy.txt" &&
        grep -q '^cpu: startup_user=.* run_share=.* end_cpu=' "$out/summary-$policy.txt"; then
        pass "$policy: $(field useful "$s") correct, $(field detected "$s") caught, 0 wrong, $(field late "$s") withheld over $ticks cycles (demo $(field useful "$d") / $(field detected "$d") / 0)"
    else
        fail "$policy: F' $s / demo $d over $ticks cycles"
    fi
done
s1=$out/events-shrink.txt
l1=$(field late "$(sed -n 2p "$out/summary-shrink.txt")")
if at 'Replica 1 retired (KNOWN_ANSWER_FAILED (0))' 122 $((l1 + 1)) "$s1" &&
    at 'Replica 0 retired (LOST (2))' 242 "$l1" "$s1" &&
    at 'Mode COMPARE (1) -> SELF_CHECK (2)' 243 "$l1" "$s1" &&
    grep -q 'CORRUPT (3) applied to replica 2 at cycle 60' "$s1"; then
    pass "shrink: events on their cycles (flip 60, retired 122, lost 242, self-check 243)"
else
    fail "shrink: events ($(grep -E 'Retired|ModeChanged|FaultApplied' "$s1" | tr '\n' ' '))"
fi
lt=$(field late "$(sed -n 2p "$out/summary-tmr.txt")")
if [ "$(field useful "$(sed -n 2p "$out/summary-tmr.txt")")" = 239 ] &&
    at 'Replica 0 retired (LOST (2))' 242 "$lt" "$out/events-tmr.txt" &&
    ! grep -q 'KNOWN_ANSWER' "$out/events-tmr.txt"; then
    pass "tmr: 239 correct, then nothing; the stuck replica is never diagnosed"
else
    fail "tmr: $(sed -n 2p "$out/summary-tmr.txt")"
fi

# segment 3: killed, restored from the state file
r=$(sed -n 2p "$out/summary-restart.txt" 2>/dev/null || true)
at=$(sed -n 's/.*State restored: policy SHRINK (0), slots \[ ACTIVE (0), RETIRED (2), ACTIVE (0) \], cycle \([0-9]*\).*/\1/p' "$out/events-restart-2.txt" | head -n 1)
if grep -q 'restart-1: exit=137' "$out/run-info.txt" && [ -n "$at" ] && [ "$at" -ge 300 ] &&
    [ "$(field wrong "$r")" = 0 ] && [ "$(field slots "$r")" = active,retired,active ] &&
    [ -s "$out/state-restart-at-kill.dat" ] && grep -q 'status=0' "$out/run-info.txt"; then
    pass "restart: killed, restored at cycle $at with replica 1 retired, 0 wrong after"
else
    fail "restart: restored at '$at' ($r / $(tail -n 6 "$out/run-info.txt" | tr '\n' ' '))"
fi

# the same package again in the same folder: the old state file is removed
env DEPLOYMENT="$deployment" CYCLE_MS=20 SEGMENTS=restart RESTART_AFTER=6 RESTART_SECONDS=2 \
    "$dir/run" >"$dir/run2.out" 2>&1 || true
if grep -q 'removed a state file left by an earlier run' "$out/run-info.txt" &&
    grep -q 'No valid state file' "$out/events-restart-1.txt" &&
    at 'Replica 1 retired (KNOWN_ANSWER_FAILED (0))' 202 $(($(grep -c CycleWithheld "$out/events-restart-1.txt") + 1)) "$out/events-restart-1.txt"; then
    pass "restart: a second run in the same folder starts from cycle 1"
else
    fail "restart: second run in the same folder ($(tail -n 8 "$out/run-info.txt" | tr '\n' ' '))"
fi

echo
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
