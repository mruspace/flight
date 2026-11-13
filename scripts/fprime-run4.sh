#!/bin/sh
# fprime-run4.sh: the OPS-SAT run 4 package (opssat/fprime3), end to end with
# short caps, against a native build: a replica that hangs is reset and
# taken back, a replica whose process is killed is kept out after three
# resets, and no segment delivers a wrong result (MRU-RM-024, MRU-FI-004).
# The cFS segment: the same decision core as a cFS application, the faults
# of run 1, no socket, nothing left behind. The sensor segment: a spike masked, a drifting sensor excluded, a stuck one
# that leaves two readings in disagreement, and no wrong value (MRU-SV-001 to
# 007, MRU-SS-001 to 004).
# Cycles are not checked exactly: on a loaded runner a withheld cycle
# (MRU-RM-023) can move an event. Exit status is non-zero if any check fails.
#
#   MRU_DEPLOYMENT    the manager binary   [fprime/build-artifacts/Linux/MruQuorum/bin/MruQuorum]
#   MRU_REPLICA       the replica binary   [fprime/build-artifacts/Linux/MruReplica/bin/MruReplica]
#   MRU_DICTIONARY    its dictionary       [fprime/build-artifacts/Linux/MruQuorum/dict/MruQuorumTopologyDictionary.json]
# Run it as a normal user, as CI and the OPS-SAT runner do: as root in a
# container, OSAL does not cap queue depths and cFE cannot create its pipes.
#
#   MRU_CFS           the cFS executable   [cfs/dist-native/core-cpu1]
#   MRU_CFS_STARTUP   its startup script   [cfs/dist-native/cf/cfe_es_startup.scr]
set -eu
cd "$(dirname "$0")/.."
art=fprime/build-artifacts/Linux
deployment=${MRU_DEPLOYMENT:-$art/MruQuorum/bin/MruQuorum}
replica=${MRU_REPLICA:-$art/MruReplica/bin/MruReplica}
dictionary=${MRU_DICTIONARY:-$art/MruQuorum/dict/MruQuorumTopologyDictionary.json}
cfs=${MRU_CFS:-cfs/dist-native/core-cpu1}
cfs_startup=${MRU_CFS_STARTUP:-cfs/dist-native/cf/cfe_es_startup.scr}

fails=0
pass() { printf 'ok    %s\n' "$1"; }
fail() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cp opssat/fprime3/run "$dir/run"
cp "$deployment" "$dir/MruQuorum"
cp "$replica" "$dir/MruReplica"
mkdir -p "$dir/cfs/cf"
cp "$cfs" "$dir/cfs/core-cpu1"
cp "$cfs_startup" "$dir/cfs/cf/cfe_es_startup.scr"
for s in reset shrink tmr sensor; do
    fprime-seqgen --dictionary "$dictionary" "opssat/fprime3/$s.seq" "$dir/$s.bin" >/dev/null
done

# 20 ms cycles: 9 s reach about cycle 450, past the second hang at 300
if (cd "$dir" && CYCLE_MS=20 RESET_SECONDS=9 SHRINK_SECONDS=9 TMR_SECONDS=9 SENSOR_SECONDS=9 CFS_SECONDS=8 ./run); then
    pass "run: five segments, exit 0"
else
    fail "run: exit status $? ($(grep -E 'exit=' "$dir/toGround/run-info.txt" | tr '\n' ' '))"
fi
out=$dir/toGround
count() { grep -c "$1" "$2" || true; }
field() { tr ' ' '\n' <"$1" | sed -n "s/^$2=//p" | head -n 1; }

# segment 1: resets on
e=$out/events-reset.txt
s=$out/summary-reset.txt
[ "$(count 'ReplicaAdmitted : Replica 1 admitted' "$e")" -eq 2 ] &&
    [ "$(count 'ResetApplied : Replica 1 reset at cycle [0-9]*; hang ended: 1' "$e")" -eq 2 ] &&
    pass "reset: replica 1 hung twice, was reset and admitted twice" ||
    fail "reset: replica 1 not reset and admitted twice"
[ "$(count 'ReplicaReset : Replica 2 reset' "$e")" -eq 3 ] &&
    [ "$(count 'ResetsExhausted : Replica 2 kept out' "$e")" -eq 1 ] &&
    [ "$(count 'ReplicaAdmitted : Replica 2' "$e")" -eq 0 ] &&
    pass "reset: replica 2, killed, reset three times and kept out" ||
    fail "reset: replica 2 not kept out after three resets"
[ "$(field "$s" wrong)" = 0 ] && [ "$(field "$s" slots)" = active,active,retired ] &&
    pass "reset: $(field "$s" useful) correct, 0 wrong, slots active,active,retired at the end" ||
    fail "reset: wrong=$(field "$s" wrong) slots=$(field "$s" slots)"
grep -q '^replicas: 0=exit 0 1=exit 0 2=signal 9' "$s" &&
    pass "reset: replica 2's process ended by SIGKILL, the others cleanly" ||
    fail "reset: replica processes ended unexpectedly"

# segment 2: resets off
e=$out/events-shrink.txt
s=$out/summary-shrink.txt
[ "$(count 'ReplicaReset' "$e")" -eq 0 ] && [ "$(field "$s" wrong)" = 0 ] &&
    [ "$(field "$s" slots)" = active,retired,retired ] && [ "$(field "$s" mode)" = self-check ] &&
    pass "shrink: no resets, $(field "$s" useful) correct, 0 wrong, alone in self-check at the end" ||
    fail "shrink: resets=$(count 'ReplicaReset' "$e") wrong=$(field "$s" wrong) slots=$(field "$s" slots)"

# segment 3: fixed TMR
e=$out/events-tmr.txt
s=$out/summary-tmr.txt
[ "$(count 'Halted : Halted at cycle' "$e")" -eq 1 ] && [ "$(field "$s" wrong)" = 0 ] &&
    [ "$(field "$s" mode)" = halted ] &&
    pass "tmr: $(field "$s" useful) correct, 0 wrong, halted when the killed replica was lost" ||
    fail "tmr: mode=$(field "$s" mode) wrong=$(field "$s" wrong)"

# segment 4: three sensors
e=$out/events-sensor.txt
s=$out/summary-sensor.txt
sensors=$(grep '^sensors:' "$s" || true)
sf() { printf '%s\n' "$sensors" | tr ' ' '\n' | sed -n "s/^$1=//p"; }
[ "$(count 'SensorOutlier : Sensor 1 outlier' "$e")" -ge 1 ] &&
    [ "$(count 'SensorExcluded : Sensor 1' "$e")" -eq 0 ] &&
    pass "sensor: the spike on sensor 1 outvoted and masked, sensor 1 not excluded" ||
    fail "sensor: the spike on sensor 1 not masked, or sensor 1 excluded"
[ "$(count 'SensorExcluded : Sensor 2 excluded' "$e")" -eq 1 ] &&
    pass "sensor: the drifting sensor 2 excluded after five outlier cycles in a row" ||
    fail "sensor: sensor 2 not excluded once"
[ "$(count 'SensorsDisagree : Sensors disagree' "$e")" -ge 1 ] &&
    pass "sensor: after sensor 0 froze, the two readings left disagreed and nothing was delivered" ||
    fail "sensor: no disagreement after sensor 0 froze"
[ -n "$sensors" ] && [ "$(sf wrong)" = 0 ] && [ "$(sf unchecked)" = 0 ] && [ "$(sf average_wrong)" -gt 0 ] &&
    pass "sensor: $(sf correct) correct, 0 wrong, $(sf withheld) withheld; the plain average wrong in $(sf average_wrong) cycles" ||
    fail "sensor: summary line '$sensors'"
[ "$(field "$s" wrong)" = 0 ] && grep -q '^replicas: 0=exit 0 1=exit 0 2=exit 0' "$s" &&
    pass "sensor: the replicas ran alongside, 0 wrong, no process killed" ||
    fail "sensor: replicas wrong=$(field "$s" wrong)"

# segment 5: the decision core as a cFS application
e=$out/events-cfs.txt
s=$out/summary-cfs.txt
grep -q 'Replica 1 retired (KNOWN_ANSWER_FAILED)' "$e" && grep -q 'Replica 0 retired (LOST)' "$e" &&
    grep -q 'Mode compare -> self-check' "$e" &&
    pass "cfs: stuck replica 1 retired on its known answer, hung replica 0 lost, self-check on replica 2" ||
    fail "cfs: events not as expected"
[ "$(field "$s" wrong)" = 0 ] && [ "$(field "$s" slots)" = retired,retired,active ] && grep -q 'exit=0' "$s" &&
    pass "cfs: $(field "$s" useful) correct, 0 wrong, exit 0" ||
    fail "cfs: wrong=$(field "$s" wrong) slots=$(field "$s" slots) ($(grep '^process' "$s"))"
grep -q 'sockets=0 ' "$s" && pass "cfs: no socket open during the run" || fail "cfs: $(grep '^process' "$s")"
grep -q 'cfs: cleanup: working folder removed, /dev/shm/osal:RAM absent, 0 shared memory segments removed' \
    "$out/run-info.txt" && [ ! -e "$dir/cfs-work" ] &&
    pass "cfs: the power-on reset deleted cFE's shared memory; working files and RAM disk folder removed" ||
    fail "cfs: cleanup ($(grep 'cfs: cleanup' "$out/run-info.txt"))"

[ "$fails" -eq 0 ] || { echo "$fails check(s) failed"; exit 1; }
