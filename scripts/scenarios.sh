#!/bin/sh
# Scenario tests for the quorum demo. Each check runs the real program and
# asserts on its output. Exit status is non-zero if any check fails.
set -eu
cd "$(dirname "$0")/.."
cargo build --release --quiet

Q=target/release/quorum
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

# 6. Footprint stays well inside the planned OPS-SAT targets (128 MB memory).
fp=$("$Q" --policy shrink --ticks 20000 --seed 1 --upset-rate 0.001 | sed -n 3p)
voter=$(field voter_max_rss "$fp" | tr -dc 0-9)
replica=$(field replica_max_rss "$fp" | tr -dc 0-9)
if [ "$voter" -lt 131072 ] && [ "$replica" -lt 131072 ]; then
    pass "footprint: voter ${voter} KiB, replica ${replica} KiB"
else
    fail "footprint too large ($fp)"
fi

echo
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
