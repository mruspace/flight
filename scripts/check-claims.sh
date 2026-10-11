#!/bin/sh
# Fails when a headline number in README.md differs from the results files the
# code wrote: the bench (bench/results/<date>-<host>.json, three hosts that must
# agree) and the known-answer test coverage campaign (bench/results/*-kat.json).
# Each check builds the exact string the README must contain, from the JSON.
#   ./scripts/check-claims.sh
# Needs jq.
set -eu
cd "$(dirname "$0")/.."
R=bench/results
README=README.md
fail=0
# README.md with line breaks folded, so a wrapped sentence still matches.
FLAT=$(tr '\n' ' ' <"$README" | tr -s ' ')
want() { # label, expected substring of README.md
	if printf '%s' "$FLAT" | grep -qF -- "$2"; then echo "ok   $1"; else echo "FAIL $1: README.md lacks \"$2\""; fail=1; fi
}

# The bench: latest date, not quick, not the coverage campaign.
is_bench() { case $1 in *-kat.json | *quick*) return 1 ;; *) return 0 ;; esac; }
latest=
for f in "$R"/*.json; do
	is_bench "$f" || continue
	latest=$(basename "$f" | cut -c1-10) # globs sort by name, so the last is the latest date
done
set --
for f in "$R"/"$latest"-*.json; do is_bench "$f" && set -- "$@" "$f"; done
[ $# -eq 3 ] || { echo "FAIL expected 3 bench hosts for $latest, found $#"; exit 1; }
for f in "$2" "$3"; do
	if [ "$(jq -cS .summary "$1")" != "$(jq -cS .summary "$f")" ]; then
		echo "FAIL bench hosts differ: $1 and $f"; exit 1
	fi
done
B=$1
s() { jq -r --arg sch "$1" --argjson r "$2" ".summary[] | select(.schedule == \$sch and .upset_rate == \$r) | $3" "$B"; }
f2() { printf '%.2f' "$1"; }
f1() { printf '%.1f' "$1"; }
f0() { printf '%.0f' "$1"; }

want "ratio, two lost one at a time" "$(f2 "$(s staggered-kills 0.001 .useful_ratio_shrink_tmr.mean)")× with two replicas lost"
want "ratio, two lost early" "$(f2 "$(s common-mode 0.001 .useful_ratio_shrink_tmr.mean)")× with two lost early"
want "ratio, stuck" "$(f2 "$(s stuck 0.001 .useful_ratio_shrink_tmr.mean)")× with a stuck replica"
pairs=$(jq '[.summary[].seeds] | add' "$B")
below=$(jq '[.summary[].seeds_shrink_below_tmr] | add' "$B")
if [ "$below" = 0 ]; then
	want "run pairs" "In all $pairs pairs of runs"
	want "run pairs, proof section" "($pairs run pairs on five fault schedules, none"
else
	echo "FAIL $below of $pairs run pairs below fixed TMR: the README says none"; fail=1
fi
want "common-mode wrong results" "at most $(f1 "$(s common-mode 0.01 .wrong_mean.shrink)") per run on average"
want "last-stuck wrong results" "and $(f0 "$(s last-stuck 0.0001 .wrong_mean.shrink)") when"
want "last-stuck latency" "caught it $(f0 "$(s last-stuck 0.0001 .detection_latency_mean_ticks)") ticks later"
for f in "$R"/"$latest"-*.json; do
	is_bench "$f" || continue
	h=$(jq -r .host "$f")
	mb=$(f1 "$(jq '[.runs[] | .voter_rss_kib, .replica_rss_kib] | max / 1024' "$f")")
	want "footprint $h" "| $mb MB |"
done

# The coverage campaign.
K=
for f in "$R"/*-kat.json; do
	[ -e "$f" ] || continue
	case $f in *quick*) continue ;; esac
	K=$f # globs sort, so the last is the latest date
done
if [ -n "$K" ]; then
	want "coverage file" "$K"
	k() { jq -r --arg fl "$1" --arg loc "$2" --argjson p "$3" --argjson cm "$4" \
		".summary[] | select(.fault == \$fl and (.location // \"\") == \$loc and .kat_period == \$p and (.common_mode // 0) == \$cm) | $5" "$K"; }
	pct() { printf '%.0f%%' "$(echo "$1 * 100" | bc -l)"; }
	row() { # label, fault, location, share, README row prefix
		d=$(pct "$(k "$2" "$3" 64 "$4" .fraction_detected)")
		lat="$(f1 "$(k "$2" "$3" 64 "$4" .latency_mean)") ($(k "$2" "$3" 64 "$4" .latency_max))"
		wr="$(f1 "$(k "$2" "$3" 64 "$4" .wrong_before_detection_mean)") ($(k "$2" "$3" 64 "$4" .wrong_before_detection_max))"
		miss=$(k "$2" "$3" 64 "$4" .missed_with_wrong)
		n=$(k "$2" "$3" 64 "$4" .runs)
		want "coverage, $1" "$5 | $n | $d | $(echo "$lat" | sed 's/^0\.0 /0 /') | $(echo "$wr" | sed 's/^0\.0 /0 /') | $miss |"
	}
	row "stuck result" stuck "" 0 "| Stuck result (same wrong value every time)"
	row "hang" hang "" 0 "| Hang"
	row "stuckbit out" stuckbit out 0 "| Stuck-at bit in the result"
	row "stuckbit acc" stuckbit acc 0 "| Stuck-at bit in the hash accumulator"
	row "stuckbit block" stuckbit block 0 "| Stuck-at bit in the working block (128 of 65,536)"
	row "stuckbit in" stuckbit in 0 "| Stuck-at bit in the input"
	row "corrupt 0" corrupt "" 0 "| Bit flip in working memory, common-mode share 0"
	row "corrupt 0.05" corrupt "" 0.05 "| Bit flip, common-mode share 0.05"
	row "corrupt 0.2" corrupt "" 0.2 "| Bit flip, common-mode share 0.2"
	for p in 8 64 256; do
		want "stuck result, period $p" "$(f1 "$(k stuck "" "$p" 0 .wrong_before_detection_mean)") at $p ticks"
	done
fi

if [ "$fail" = 0 ]; then
	echo "README.md matches the results files."
else
	echo "README.md differs from the results files."
	exit 1
fi
