#!/bin/sh
# KAT coverage: how often the periodic known-answer test (KAT) catches a
# persistent fault on the last replica, how fast, how many wrong results get
# out first, and what the test costs. Writes JSON and CSV to
# bench/results/<date>-<host>-kat.{json,csv}.
#
#   scripts/kat-coverage.sh                 full matrix (about 5,900 runs)
#   scripts/kat-coverage.sh --quick         a small sample of it (for CI)
#   scripts/kat-coverage.sh --only GROUP    one group of the matrix, written
#                                           to <date>-<host>-kat-GROUP.*
#
# Every run: shrinking quorum, no random upsets, replicas 0 and 1 killed at
# ticks 10 and 20, so replica 2 runs alone in self-check mode. The fault goes
# on replica 2 at tick 500 or later. The KAT computes a fixed input (tick 0)
# and compares it with the known result; the KAT request carries the
# replica's faults, so it runs on the same broken hardware.
#
# Groups:
#   stuck     the replica returns a fixed wrong value (every KAT phase)
#   hang      the replica stops answering (caught by the reply timeout)
#   stuckbit  one bit held at 0 or 1: in the input (in), the working block
#             (block), the hash accumulator (acc) or the result (out)
#   corrupt   one bit flip in one computation, over seeds and common-mode
#             shares (the share of flips that hit both self-check runs alike)
#   baseline  kills only, for the cost of the KAT at each period (100,000
#             ticks per run, 20,000 with --quick)
#
# BENCH_HOST, BENCH_OUT, BENCH_COMMIT and QUORUM_BIN work as in bench.sh.
set -eu
cd "$(dirname "$0")/.."

quick=0
only=""
while [ $# -gt 0 ]; do
    case "$1" in
        --quick) quick=1 ;;
        --only)
            only=${2:?--only needs a group}
            shift
            case "$only" in
                stuck | hang | stuckbit | corrupt | baseline) ;;
                *) echo "unknown group $only (stuck, hang, stuckbit, corrupt or baseline)" >&2; exit 2 ;;
            esac
            ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done
groups=${only:-stuck hang stuckbit corrupt baseline}
# wanted <group>: whether this run includes the group
wanted() { case " $groups " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

if [ -z "${QUORUM_BIN:-}" ]; then
    cargo build --release --quiet
fi
Q=${QUORUM_BIN:-target/release/quorum}

# field <name> <output>: print the value of name=value from the output, as in bench.sh
field() { printf '%s\n' "$2" | tr ' ' '\n' | sed -n "s/^$1=//p" | head -n 1; }

# nums <from> <to> [step]: the integers from..to
nums() { awk -v a="$1" -v b="$2" -v s="${3:-1}" 'BEGIN { for (i = a; i <= b; i += s) printf "%s ", i }'; }

if [ "$quick" = 1 ]; then
    periods="16 64"
    stuck_k=$(nums 0 15)
    hang_k=$(nums 0 7)
    bit_k=$(nums 0 120 8) # 16 of the 128 instances, 8 per value
    seeds=$(nums 1 32)
    shares="0 0.2"
    base_seeds="1 2"
    base_periods="16 64 1000000"
    BASE_TICKS=20000
else
    periods="8 16 32 64 128 256"
    stuck_k=$(nums 0 255)
    hang_k=$(nums 0 63)
    bit_k=$(nums 0 127)
    seeds=$(nums 1 400)
    shares="0 0.05 0.2"
    base_seeds=$(nums 1 10)
    base_periods="8 16 32 64 128 256 1000000"
    BASE_TICKS=100000
fi
# a healthy replica answers in microseconds: the timeout only matters for hang
TIMEOUT=1000
HANG_TIMEOUT=100
common="--policy shrink --upset-rate 0 --fault kill:0@10 --fault kill:1@20"

# host, cpu, commit, rustc, date and the output folder
. scripts/bench-meta.sh
base="$out/$date-$host-kat"
[ -n "$only" ] && base="$base-$only"
[ "$quick" = 1 ] && base="$base-quick"
csv="$base.csv"
json="$base.json"

echo "group,fault,location,bit,value,kat_period,common_mode,instance,fault_tick,ticks,useful,detected,wrong,halted_at,caught,latency,kat_tests,cpu_s,wall_s,fault_schedule" >"$csv"
n=0
started=$(date +%s)

# run <group> <fault> <location> <bit> <value> <period> <common_mode> <instance>
#     <fault_tick> <ticks> <timeout_ms> <seed> [fault option]
run() {
    cm=""
    [ -n "$7" ] && cm="--common-mode $7"
    f=""
    [ -n "${13:-}" ] && f="--fault ${13}"
    # shellcheck disable=SC2086 # common, cm and f are lists of options
    o=$("$Q" $common --kat-period "$6" --ticks "${10}" --seed "${12}" \
        --reply-timeout-ms "${11}" $cm $f)
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8" "$9" "${10}" \
        "$(field useful "$o")" "$(field detected "$o")" "$(field wrong "$o")" \
        "$(field halted_at "$o")" "$(field caught "$o")" "$(field latency_max "$o")" \
        "$(field kat_tests "$o")" "$(field cpu "$o" | tr -d s)" "$(field wall "$o" | tr -d s)" \
        "kill:0@10 kill:1@20${13:+ ${13}}" >>"$csv"
    n=$((n + 1))
}

# ---- stuck: a fixed wrong result, at every phase of the KAT period
wanted stuck && for P in $periods; do
    for k in $stuck_k; do
        t=$((500 + k))
        run stuck stuck "" "" "" "$P" "" "$k" "$t" $((t + 1024)) $TIMEOUT 1 "stuck:2@$t"
    done
    printf '  stuck, KAT period %s: done\n' "$P" >&2
done

# ---- hang: the replica stops answering; the reply timeout catches it
wanted hang && for k in $hang_k; do
    t=$((500 + k))
    run hang hang "" "" "" 64 "" "$k" "$t" $((t + 64)) $HANG_TIMEOUT 1 "hang:2@$t"
done
wanted hang && printf '  hang: done\n' >&2

# ---- stuckbit: instance k holds one bit at one value from tick 500 + k on.
# in, acc, out: bit k % 64, value k / 64 (all 128 of 64 bits x 2 values).
# block: bit (k * 257) % 32768, value k / 64 (128 of 32,768 bits x 2 values,
# spread over the block, every bit position in a word once per value).
wanted stuckbit && for loc in in block acc out; do
    for P in $periods; do
        for k in $bit_k; do
            if [ "$loc" = block ]; then b=$(((k * 257) % 32768)); else b=$((k % 64)); fi
            v=$((k / 64))
            t=$((500 + k))
            run stuckbit stuckbit "$loc" "$b" "$v" "$P" "" "$k" "$t" $((t + 1024)) $TIMEOUT 1 \
                "stuckbit:2@$t/$loc.$b.$v"
        done
        printf '  stuckbit %s, KAT period %s: done\n' "$loc" "$P" >&2
    done
done

# ---- corrupt: one bit flip in one computation; the seed picks the bit and
# whether the flip hits both self-check runs (the common-mode share)
wanted corrupt && for c in $shares; do
    for s in $seeds; do
        run corrupt corrupt "" "" "" 64 "$c" "$s" 500 600 $TIMEOUT "$s" "corrupt:2@500"
    done
    printf '  corrupt, common-mode share %s: done\n' "$c" >&2
done

# ---- baseline: no fault, for the cost of the KAT at each period
# (1000000 is in effect no test; --kat-period 0 would test every tick)
# Each seed runs every period in turn, so a change in machine load over the
# group hits all periods alike.
wanted baseline && for s in $base_seeds; do
    for P in $base_periods; do
        run baseline none "" "" "" "$P" "" "$s" "" $BASE_TICKS $TIMEOUT "$s"
    done
    printf '  baseline, seed %s: done\n' "$s" >&2
done
wall_total=$(($(date +%s) - started))

# ---- results delivered before each fault tick, from a fault-free run that
# stops just before it (the fault cannot change them)
pre=$(mktemp)
# shellcheck disable=SC2013 # one tick per line
for t in $(awk -F, 'NR > 1 && $9 != "" { print $9 }' "$csv" | sort -un); do
    # shellcheck disable=SC2086 # common is a list of options
    o=$("$Q" $common --ticks $((t - 1)))
    echo "$t,$(field useful "$o")" >>"$pre"
done

# ---- summary per fault, location, KAT period and common-mode share

summary=$(awk -F, -v prefile="$pre" 'FILENAME == prefile { pre[$1] = $2; next }
FNR > 1 && $1 != "baseline" {
    k = $2 "," $3 "," $6 "," $7
    if (!(k in runs)) { order[++m] = k }
    runs[k]++
    if ($15 == 1) {
        det[k]++; lat[k] += $16; if ($16 > latmax[k]) latmax[k] = $16
        w[k] += $13; if ($13 > wmax[k]) wmax[k] = $13
        if ($5 != "") dv[k "," $5]++
    } else {
        if ($13 > 0) mww[k]++; else mne[k]++
        mw[k] += $13; md[k] += $11 + $13 - pre[$9]
    }
    if ($5 != "") rv[k "," $5]++
}
END {
    for (i = 1; i <= m; i++) {
        k = order[i]; d = det[k] + 0; r = runs[k]
        split(k, a, ",")
        printf "%s,%s,%s,%s,%d,%d,%.4f,%d,%d,%s,%s,%s,%s,%s,%s,%s\n",
            a[1], (a[2] == "" ? "-" : a[2]), a[3], (a[4] == "" ? "-" : a[4]), r, d, d / r,
            mww[k] + 0, mne[k] + 0,
            (d > 0 ? sprintf("%.1f", lat[k] / d) : "null"), (d > 0 ? latmax[k] + 0 : "null"),
            (d > 0 ? sprintf("%.2f", w[k] / d) : "null"), (d > 0 ? wmax[k] + 0 : "null"),
            (md[k] > 0 ? sprintf("%.4f", mw[k] / md[k]) : "null"),
            (a[1] == "stuckbit" ? sprintf("%d/%d", dv[k ",0"], rv[k ",0"]) : "-"),
            (a[1] == "stuckbit" ? sprintf("%d/%d", dv[k ",1"], rv[k ",1"]) : "-")
    }
}' "$pre" "$csv")
rm -f "$pre"

# ---- throughput and CPU cost per KAT period, from the baseline runs

throughput=$(awk -F, 'NR > 1 && $1 == "baseline" {
    p = $6
    if (!(p in runs)) order[++m] = p
    runs[p]++; u[p] += $11; kt[p] += $17; dl[p] += $11 + $13; cpu[p, runs[p]] = $18
}
# q <quantile>: of v[1..c], sorted, by linear interpolation
function q(f,   x, i) { x = 1 + (c - 1) * f; i = int(x); return (i >= c) ? v[c] : v[i] + (x - i) * (v[i + 1] - v[i]) }
END {
    for (i = 1; i <= m; i++) {
        p = order[i]; c = runs[p]
        for (a = 1; a <= c; a++) v[a] = cpu[p, a]
        for (a = 2; a <= c; a++) { x = v[a]; for (b = a - 1; b >= 1 && v[b] > x; b--) v[b + 1] = v[b]; v[b + 1] = x }
        med[p] = q(0.5); q1[p] = q(0.25); q3[p] = q(0.75)
        if (p + 0 > ref + 0) ref = p
    }
    for (i = 1; i <= m; i++) {
        p = order[i]
        printf "%s,%d,%.1f,%.1f,%.6f,%.4f,%.4f,%.4f,%.4f,%s,%.6f\n", p, runs[p], u[p] / runs[p], kt[p] / runs[p],
            (dl[p] > 0 ? kt[p] / dl[p] : 0), med[p], q1[p], q3[p], q3[p] - q1[p],
            (med[ref] > 0 ? sprintf("%.4f", med[p] / med[ref] - 1) : "null"), 1 / p
    }
}' "$csv")

# ---- JSON

list() { echo "$1" | tr -s ' ' ',' | sed 's/^,//; s/,$//'; }
nk=$(echo "$bit_k" | wc -w | tr -d ' ')
{
    printf '{\n'
    printf '  "host": "%s",\n  "cpu": "%s",\n  "os": "%s",\n  "arch": "%s",\n' \
        "$host" "$cpu" "$(uname -sr)" "$(uname -m)"
    printf '  "commit": "%s",\n  "date": "%s",\n  "rustc": "%s",\n' "$commit" "$date" "$rustc"
    printf '  "build": "release, lto = true, codegen-units = 1",\n'
    printf '  "binary": "%s",\n' "$Q"
    printf '  "matrix": {\n'
    printf '    "groups": ["%s"],\n' "$(echo "$groups" | sed 's/ /", "/g')"
    printf '    "total_runs": %s,\n    "wall_s": %s,\n' "$n" "$wall_total"
    printf '    "common": "%s",\n' "$common"
    printf '    "fault_replica": 2,\n    "kat_input": "tick 0",\n'
    printf '    "stuck": { "fault_tick": "500 + k", "k": [%s], "ticks": "fault_tick + 1024", "kat_periods": [%s], "reply_timeout_ms": %s },\n' \
        "$(list "$stuck_k")" "$(list "$periods")" "$TIMEOUT"
    printf '    "hang": { "fault_tick": "500 + k", "k": [%s], "ticks": "fault_tick + 64", "kat_periods": [64], "reply_timeout_ms": %s },\n' \
        "$(list "$hang_k")" "$HANG_TIMEOUT"
    printf '    "stuckbit": { "fault_tick": "500 + k", "k": [%s], "ticks": "fault_tick + 1024", "kat_periods": [%s], "reply_timeout_ms": %s,\n' \
        "$(list "$bit_k")" "$(list "$periods")" "$TIMEOUT"
    printf '      "in": "bit = k %% 64, value = k / 64: %s of 128 instances (64 bits x 2 values)",\n' "$nk"
    printf '      "acc": "bit = k %% 64, value = k / 64: %s of 128 instances (64 bits x 2 values)",\n' "$nk"
    printf '      "out": "bit = k %% 64, value = k / 64: %s of 128 instances (64 bits x 2 values)",\n' "$nk"
    printf '      "block": "bit = (k * 257) %% 32768, value = k / 64: %s of 65,536 instances (32,768 bits x 2 values)" },\n' "$nk"
    printf '    "corrupt": { "fault_tick": 500, "seeds": [%s], "common_mode": [%s], "ticks": 600, "kat_periods": [64], "reply_timeout_ms": %s },\n' \
        "$(list "$seeds")" "$(list "$shares")" "$TIMEOUT"
    printf '    "baseline": { "faults": "kills only", "ticks": %s, "seeds": [%s], "kat_periods": [%s], "reply_timeout_ms": %s, "note": "1000000 is in effect no test" },\n' \
        "$BASE_TICKS" "$(list "$base_seeds")" "$(list "$base_periods")" "$TIMEOUT"
    printf '    "definitions": {\n'
    printf '      "detected": "caught == 1: the voter reacted to the faulty replica",\n'
    printf '      "wrong_before_detection": "wrong results in a detected run (no upsets; a persistent fault halts the run at detection, as the last replica is retired)",\n'
    printf '      "missed_with_wrong": "not caught, wrong > 0",\n'
    printf '      "missed_no_effect": "not caught, wrong == 0",\n'
    printf '      "wrong_rate_when_missed": "wrong results / results delivered from the fault tick on, over the missed runs",\n'
    printf '      "latency": "ticks from the fault to detection, over the detected runs",\n'
    printf '      "kat_tests_per_delivered_result": "known-answer tests / correct and wrong results delivered, over the runs (exact)",
      "cpu_s_q1_q3_iqr": "first and third quartiles of CPU time over the runs (linear interpolation), and their difference",
      "cpu_cost_vs_no_test": "median CPU time (voter and replicas) / the median at period 1000000, minus 1",\n'
    printf '      "model_extra_computes": "1 / P: one KAT computation per P ticks, against P computations for P / 2 self-checked results"\n'
    printf '    }\n  },\n  "summary": ['
    printf '%s\n' "$summary" | awk -F, 'NF > 1 {
        bv = ""
        if ($1 == "stuckbit") { split($15, z, "/"); split($16, o, "/")
            bv = sprintf(", \"by_value\": { \"0\": { \"runs\": %s, \"detected\": %s }, \"1\": { \"runs\": %s, \"detected\": %s } }", z[2], z[1], o[2], o[1]) }
        printf "%s\n    { \"fault\": \"%s\", \"location\": %s, \"kat_period\": %s, \"common_mode\": %s, \"runs\": %s, \"detected\": %s, \"fraction_detected\": %s, \"missed_with_wrong\": %s, \"missed_no_effect\": %s, \"latency_mean\": %s, \"latency_max\": %s, \"wrong_before_detection_mean\": %s, \"wrong_before_detection_max\": %s, \"wrong_rate_when_missed\": %s%s }",
            (n++ ? "," : ""), $1, ($2 == "-" ? "null" : "\"" $2 "\""), $3, ($4 == "-" ? "null" : $4),
            $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, bv }'
    printf '\n  ],\n  "throughput": ['
    printf '%s\n' "$throughput" | awk -F, 'NF > 1 { printf "%s\n    { \"kat_period\": %s, \"runs\": %s, \"useful_mean\": %s, \"kat_tests_mean\": %s, \"kat_tests_per_delivered_result\": %s, \"cpu_s_median\": %s, \"cpu_s_q1\": %s, \"cpu_s_q3\": %s, \"cpu_s_iqr\": %s, \"cpu_cost_vs_no_test\": %s, \"model_extra_computes\": %s }",
        (n++ ? "," : ""), $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11 }'
    printf '\n  ],\n  "runs": ['
    awk -F, 'NR == 1 { for (i = 1; i <= NF; i++) h[i] = $i; next }
        { printf "%s\n    {", (NR > 2 ? "," : "")
          for (i = 1; i <= NF; i++) {
              v = $i
              if (v !~ /^-?[0-9.]+$/) v = (v == "none" || v == "") ? "null" : "\"" v "\""
              printf "%s \"%s\": %s", (i > 1 ? "," : ""), h[i], v
          }
          printf " }" }' "$csv"
    printf '\n  ]\n}\n'
} >"$json"

# ---- report

# align columns where the system has column(1)
table() { if command -v column >/dev/null; then column -t -s,; else tr , ' '; fi; }
echo
echo "host: $host ($cpu), commit $commit, $n runs in ${wall_total}s"
if [ -n "$summary" ]; then
    echo
    {
        echo "fault,location,kat_period,common_mode,runs,detected,fraction,missed_wrong,missed_no_effect,latency_mean,latency_max,wrong_mean,wrong_max,wrong_rate_missed,detected_v0,detected_v1"
        printf '%s\n' "$summary"
    } | table
fi
if [ -n "$throughput" ]; then
    echo
    {
        echo "kat_period,runs,useful_mean,kat_tests_mean,kat_tests_per_result,cpu_s_median,cpu_s_q1,cpu_s_q3,cpu_s_iqr,cpu_cost_vs_no_test,model_extra_computes"
        printf '%s\n' "$throughput"
    } | table
fi
echo
echo "wrote $json and $csv"
