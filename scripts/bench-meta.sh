# shellcheck shell=sh disable=SC2034 # sourced: the caller uses these variables
# Metadata for a bench results file, sourced by bench.sh and kat-coverage.sh.
# Sets host, cpu, commit, rustc, date and out, and makes the output folder.
#
# BENCH_HOST names the host (default: os-arch).
# BENCH_COMMIT records the commit when there is no git checkout (on a board).
# BENCH_OUT sets the output folder (default: bench/results).
host=${BENCH_HOST:-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)}
case "$(uname -s)" in
    Darwin) cpu=$(sysctl -n machdep.cpu.brand_string) ;;
    *) cpu=$( (lscpu 2>/dev/null || true) | sed -n 's/^Model name: *//p' | head -n 1) ;;
esac
commit=${BENCH_COMMIT:-$(git rev-parse --short HEAD 2>/dev/null || echo unknown)}
[ -z "${BENCH_COMMIT:-}" ] && [ -n "$(git status --porcelain 2>/dev/null)" ] && commit="$commit-dirty"
rustc=$(rustc -V 2>/dev/null || echo unknown)
date=$(date -u +%Y-%m-%d)
out=${BENCH_OUT:-bench/results}
mkdir -p "$out"
