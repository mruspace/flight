#!/bin/sh
# Bench: run the quorum demo over a matrix of policies, seeds, upset rates and
# fault schedules, time the decision core, and write the results as JSON and
# CSV to bench/results/<date>-<host>.{json,csv}.
#
#   scripts/bench.sh            full matrix: 10 seeds, 20,000 ticks per run
#   scripts/bench.sh --quick    2 seeds, 5,000 ticks per run (for CI)
#
# BENCH_HOST names the host in the file name (default: os-arch).
# BENCH_OUT sets the output folder (default: bench/results).
# QUORUM_BIN tests a prebuilt binary, as in scenarios.sh.
set -eu
cd "$(dirname "$0")/.."

quick=0
[ "${1:-}" = --quick ] && quick=1
if [ "$quick" = 1 ]; then
    seeds="1 2"
    T=5000
else
    seeds="1 2 3 4 5 6 7 8 9 10"
    T=20000
fi
rates="0.0001 0.001 0.01"
schedules="none staggered-kills common-mode stuck last-stuck"

if [ -z "${QUORUM_BIN:-}" ]; then
    cargo build --release --quiet
fi
Q=${QUORUM_BIN:-target/release/quorum}

# field <name> <output>: print the value of name=value from the output, as in scenarios.sh
field() { printf '%s\n' "$2" | tr ' ' '\n' | sed -n "s/^$1=//p" | head -n 1; }

# faults <schedule>: the fault options and the tick at which each replica
# stops being healthy ("-" if never), as "OPTIONS|H0 H1 H2"
faults() {
    case "$1" in
        none) echo "|- - -" ;;
        # two replicas lost a quarter and half of the way through
        staggered-kills) echo "--fault kill:0@$((T / 4)) --fault kill:1@$((T / 2))|$((T / 4)) $((T / 2)) -" ;;
        # two replicas lost early: the last one self-checks for 80% of the run,
        # where an upset can hit both of its runs alike (--common-mode 0.05)
        common-mode) echo "--fault kill:0@$((T / 10)) --fault kill:1@$((T / 5))|$((T / 10)) $((T / 5)) -" ;;
        # one replica stuck a fifth of the way through, another lost later
        stuck) echo "--fault stuck:1@$((T / 5)) --fault kill:0@$((T * 9 / 20))|$((T * 9 / 20)) $((T / 5)) -" ;;
        # two replicas lost, then the last one stuck: on one replica only the
        # periodic known-answer test (every 64 ticks) can catch it
        last-stuck) echo "--fault kill:0@$((T / 4)) --fault kill:1@$((T / 2)) --fault stuck:2@$((T * 3 / 4))|$((T / 4)) $((T / 2)) $((T * 3 / 4))" ;;
    esac
}

# capacity <H0 H1 H2>: the most correct results a checked design could deliver
# on the replicas still healthy at each tick: one per tick with two or more,
# one every other tick with one (it must compute twice to check itself)
capacity() {
    awk -v T="$T" -v h="$1" 'BEGIN {
        n = split(h, at, " ")
        for (t = 1; t <= T; t++) {
            alive = 0
            for (i = 1; i <= n; i++) if (at[i] == "-" || t < at[i] + 0) alive++
            if (alive >= 2) c++
            else if (alive == 1 && t % 2 == 0) c++
        }
        print c + 0
    }'
}

host=${BENCH_HOST:-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)}
case "$(uname -s)" in
    Darwin) cpu=$(sysctl -n machdep.cpu.brand_string) ;;
    *) cpu=$( (lscpu 2>/dev/null || true) | sed -n 's/^Model name: *//p' | head -n 1) ;;
esac
commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
[ -n "$(git status --porcelain 2>/dev/null)" ] && commit="$commit-dirty"
rustc=$(rustc -V 2>/dev/null || echo unknown)
date=$(date -u +%Y-%m-%d)
out=${BENCH_OUT:-bench/results}
mkdir -p "$out"
base="$out/$date-$host"
[ "$quick" = 1 ] && base="$base-quick"

# ---- core cost

echo "core cost (ns per call)" >&2
if [ -z "${QUORUM_BIN:-}" ]; then
    micro=$(cargo run --release --quiet -p bench)
else
    micro=""
fi

# .text size of the core for a bare-metal ARM Cortex-M target, if the target
# and llvm-size are installed (rustup target add thumbv7em-none-eabihf;
# rustup component add llvm-tools); built without LTO so the code is in the rlib
text=null
size_tool=$(ls "$(rustc --print sysroot 2>/dev/null)"/lib/rustlib/*/bin/llvm-size 2>/dev/null | head -n 1 || true)
if [ -n "$size_tool" ] && rustup target list --installed 2>/dev/null | grep -q thumbv7em-none-eabihf; then
    CARGO_PROFILE_RELEASE_LTO=false cargo build -p quorum --release --quiet \
        --target thumbv7em-none-eabihf --target-dir target/size
    text=$("$size_tool" -A target/size/thumbv7em-none-eabihf/release/libquorum.rlib |
        awk '/^\.text/ { s += $2 } END { print s + 0 }')
fi

# ---- system runs

csv="$base.csv"
echo "policy,seed,upset_rate,schedule,ticks,useful,detected,wrong,halted_at,survival_ticks,alive_at_end,capacity,gdi,faults,caught,latency_mean,latency_max,cpu_s,wall_s,voter_rss_kib,replica_rss_kib" >"$csv"
n=0
for s in $schedules; do
    spec=$(faults "$s")
    opts=${spec%%|*}
    cap=$(capacity "${spec#*|}")
    for r in $rates; do
        for seed in $seeds; do
            for p in tmr shrink; do
                # shellcheck disable=SC2086 # opts is a list of options
                o=$("$Q" --policy "$p" --ticks "$T" --seed "$seed" --upset-rate "$r" $opts)
                useful=$(field useful "$o")
                halted=$(field halted_at "$o")
                surv=$(field last_useful "$o") # survival: ticks until the last correct result
                gdi=$(awk -v u="$useful" -v c="$cap" 'BEGIN { printf "%.4f", u / c }')
                f=$(field faults "$o")
                printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
                    "$p" "$seed" "$r" "$s" "$T" "$useful" "$(field detected "$o")" \
                    "$(field wrong "$o")" "$halted" "$surv" "$(field alive_at_end "$o")" \
                    "$cap" "$gdi" "${f:-0}" "$(field caught "$o")" \
                    "$(field latency_mean "$o")" "$(field latency_max "$o")" \
                    "$(field cpu "$o" | tr -d s)" "$(field wall "$o" | tr -d s)" \
                    "$(field voter_max_rss "$o" | tr -dc 0-9)" \
                    "$(field replica_max_rss "$o" | tr -dc 0-9)" >>"$csv"
                n=$((n + 1))
            done
        done
        printf '  %s, upset rate %s: done\n' "$s" "$r" >&2
    done
done

# ---- summary per schedule and upset rate, over all seeds

summary=$(awk -F, 'NR > 1 {
    k = $4 "," $3
    if (!(k in seen)) { seen[k] = 1; order[++m] = k }
    if ($1 == "tmr") { tu[k] += $6; tw[k] += $8; ts[k] += $10; tg[k] += $13; th[k] += ($9 != "never"); u[k "," $2] = $6 }
    else { su[k] += $6; sw[k] += $8; ss[k] += $10; sg[k] += $13; sh[k] += ($9 != "never"); n[k]++
        if ($15 != "" && $16 != "none") { lat[k] += $16; nl[k]++ }
        ratio = (u[k "," $2] > 0) ? $6 / u[k "," $2] : 0
        rs[k] += ratio; if (!(k in rmin) || ratio < rmin[k]) rmin[k] = ratio
        if ($6 < u[k "," $2]) below[k]++ }
} END {
    for (i = 1; i <= m; i++) {
        k = order[i]; split(k, a, ","); c = n[k]
        printf "%s,%s,%d,%.1f,%.1f,%.2f,%.2f,%d,%d,%.1f,%.1f,%.4f,%.4f,%.4f,%.4f,%d,%s\n", a[1], a[2], c,
            tu[k] / c, su[k] / c, tw[k] / c, sw[k] / c, th[k], sh[k], ts[k] / c, ss[k] / c,
            tg[k] / c, sg[k] / c, rs[k] / c, rmin[k], below[k] + 0,
            (nl[k] > 0) ? sprintf("%.1f", lat[k] / nl[k]) : "null"
    }
}' "$csv")

# ---- JSON

json="$base.json"
{
    printf '{\n'
    printf '  "host": "%s",\n  "cpu": "%s",\n  "os": "%s",\n  "arch": "%s",\n' \
        "$host" "$cpu" "$(uname -sr)" "$(uname -m)"
    printf '  "commit": "%s",\n  "date": "%s",\n  "rustc": "%s",\n' "$commit" "$date" "$rustc"
    printf '  "build": "release, lto = true, codegen-units = 1",\n'
    printf '  "binary": "%s",\n' "$Q"
    printf '  "matrix": { "ticks": %s, "seeds": [%s], "upset_rates": [%s], "schedules": ["%s"], "common_mode": 0.05, "kat_period": 64 },\n' \
        "$T" "$(echo $seeds | tr ' ' ',')" "$(echo $rates | tr ' ' ',')" "$(echo $schedules | sed 's/ /", "/g')"
    printf '  "core": {\n    "quorum_text_bytes_thumbv7em": %s,\n    "ns_per_call": [' "$text"
    printf '%s\n' "$micro" | awk -F, 'NR > 1 { printf "%s\n      { \"case\": \"%s\", \"policy\": \"%s\", \"mode\": \"%s\", \"ns\": %s }", (NR > 2 ? "," : ""), $1, $2, $3, $4 }'
    printf '\n    ]\n  },\n  "summary": ['
    printf '%s\n' "$summary" | awk -F, '{ printf "%s\n    { \"schedule\": \"%s\", \"upset_rate\": %s, \"seeds\": %s, \"useful_mean\": { \"tmr\": %s, \"shrink\": %s }, \"wrong_mean\": { \"tmr\": %s, \"shrink\": %s }, \"runs_halted\": { \"tmr\": %s, \"shrink\": %s }, \"survival_ticks_mean\": { \"tmr\": %s, \"shrink\": %s }, \"gdi_mean\": { \"tmr\": %s, \"shrink\": %s }, \"useful_ratio_shrink_tmr\": { \"mean\": %s, \"min\": %s }, \"seeds_shrink_below_tmr\": %s, \"detection_latency_mean_ticks\": %s }", (NR > 1 ? "," : ""), $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17 }'
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

echo
echo "host: $host ($cpu), commit $commit, $n runs of $T ticks"
[ -n "$micro" ] && printf '%s\n' "$micro" | column -t -s,
[ "$text" != null ] && echo "quorum .text for thumbv7em-none-eabihf: $text bytes"
echo
{
    echo "schedule,upset_rate,seeds,useful_tmr,useful_shrink,wrong_tmr,wrong_shrink,halted_tmr,halted_shrink,survival_tmr,survival_shrink,gdi_tmr,gdi_shrink,ratio_mean,ratio_min,below_tmr,latency"
    printf '%s\n' "$summary"
} | column -t -s,
echo
echo "wrote $json and $csv"
