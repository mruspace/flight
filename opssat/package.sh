#!/bin/sh
# package.sh: build the experiment package for OPS-SAT PRETTY [exp4051].
#
# Output: exp4051-mru-flight-VERSION.tar.gz in the current folder, with all
# paths relative and all files owned by user and group "exp", as the OPS-SAT
# experiment environment README requires. The runner unpacks it into $HOME of
# user exp and executes ./run.
#
# Contents: run (entry point), quorum (static 32-bit ARM hard-float binary
# from the GitHub release, checksum verified), faults.txt (the five-minute
# schedule), README.txt (for the operators) and LICENSE.
#
#   ./package.sh            uses the release in VERSION (default v0.3.1)
#   VERSION=v0.2.0 ./package.sh
#   QUORUM_BIN=path ./package.sh   uses a binary you built yourself
set -eu

here=$(cd "$(dirname "$0")" && pwd)
VERSION=${VERSION:-v0.3.1}
TARGET=armv7-unknown-linux-musleabihf
REPO=mruspace/flight
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ -n "${QUORUM_BIN:-}" ]; then
    cp "$QUORUM_BIN" "$work/quorum"
    source="local build $(sha256sum "$QUORUM_BIN" 2>/dev/null || shasum -a 256 "$QUORUM_BIN")"
else
    asset="quorum-$VERSION-$TARGET.tar.gz"
    base="https://github.com/$REPO/releases/download/$VERSION"
    curl -fsSL -o "$work/$asset" "$base/$asset"
    curl -fsSL -o "$work/SHA256SUMS" "$base/SHA256SUMS"
    (cd "$work" && grep " $asset\$" SHA256SUMS | (sha256sum -c - 2>/dev/null || shasum -a 256 -c -))
    tar -xzf "$work/$asset" -C "$work" "quorum-$VERSION-$TARGET/quorum"
    mv "$work/quorum-$VERSION-$TARGET/quorum" "$work/quorum"
    source="release $VERSION, $asset"
fi

stage="$work/stage"
mkdir -p "$stage"
cp "$work/quorum" "$stage/quorum"
cp "$here/run" "$stage/run"
cp "$here/faults.5min.txt" "$stage/faults.txt"
cp "$here/../LICENSE" "$stage/LICENSE"
chmod 755 "$stage/run" "$stage/quorum"
chmod 644 "$stage/faults.txt" "$stage/LICENSE"
sum=$(cd "$stage" && (sha256sum quorum 2>/dev/null || shasum -a 256 quorum) | cut -d' ' -f1)
cat >"$stage/README.txt" <<TXT
Mru Flight on OPS-SAT PRETTY, experiment exp4051
Binary: $source
quorum sha256: $sum

What it is: three software replicas run the same deterministic task under a
voter. Faults are injected by the voter itself from faults.txt (no hardware is
touched, nothing is commanded during the run). Two runs, one after the other,
with the same seed and schedule: the shrinking quorum, then fixed triple
redundancy. Then it exits.

How to run: ./run (foreground, as user exp, in this folder). It stops by
itself: two runs of 110 seconds plus start-up, under five minutes in total.
Stop early with SIGTERM: the voter finishes its step, writes its summary and
exits.

What it touches: user space only, no network, no device files, no reboots.
Four processes. Memory limit 128 MB per process (RLIMIT_AS), set by the
processes themselves. CPU share of the voter capped at 5% of one core.
Storage: about 1 MB for the package, a few kilobytes of output.

Output, all in toGround/: run-info.txt (start, end, exit codes), and for each
policy summary-POLICY.txt (six lines), quorum-POLICY.csv (event log) and
stdout-POLICY.txt (console output).

Source and documentation: https://github.com/mruspace/flight (docs/opssat.md)
Contact: contact@mru.space
TXT

out="$PWD/exp4051-mru-flight-$VERSION.tar.gz"
if tar --version 2>/dev/null | grep -q bsdtar; then
    tar -C "$stage" --uname exp --gname exp --uid 1024 --gid 1024 -czf "$out" run quorum faults.txt README.txt LICENSE
else
    tar -C "$stage" --owner=exp:1024 --group=exp:1024 -czf "$out" run quorum faults.txt README.txt LICENSE
fi
echo "wrote $out"
tar -tzvf "$out"
