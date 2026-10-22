#!/bin/sh
# package.sh: build the run 3 experiment package for OPS-SAT PRETTY [exp4051],
# the F´ deployment.
#
# Output: exp4051-mru-flight-fprime-VERSION.tar.gz in the current folder, with
# all paths relative and all files owned by user and group "exp", as the
# OPS-SAT experiment environment README requires. The runner unpacks it into
# $HOME of user exp and executes ./run.
#
# Contents: run (entry point), MruOpsSat (the F´ deployment, static 32-bit
# ARM hard-float binary from the GitHub release, checksum verified),
# shrink.bin, tmr.bin and restart.bin (the F´ command sequences, compiled
# against that build's dictionary, from the same release asset), README.txt
# (for the operators) and LICENSE.
#
#   ./package.sh                    uses the release in VERSION (default v0.5.0)
#   DIST=opssat/fprime/dist ./package.sh   uses a local build (build.sh)
set -eu

# macOS tar stores extended attributes as extra "._NAME" entries; both
# settings below stop them (ESA found them in the run 1 package).
export COPYFILE_DISABLE=1

here=$(cd "$(dirname "$0")" && pwd)
VERSION=${VERSION:-v0.5.0}
PLATFORM=armv7-linux-static
REPO=mruspace/flight
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
files="MruOpsSat shrink.bin tmr.bin restart.bin"

if [ -n "${DIST:-}" ]; then
    for f in $files; do cp "$DIST/$f" "$work/$f"; done
    source="local build in $DIST"
else
    name="mru-fprime-$VERSION-$PLATFORM"
    asset="$name.tar.gz"
    base="https://github.com/$REPO/releases/download/$VERSION"
    curl -fsSL -o "$work/$asset" "$base/$asset"
    curl -fsSL -o "$work/SHA256SUMS" "$base/SHA256SUMS"
    (cd "$work" && grep " $asset\$" SHA256SUMS | (sha256sum -c - 2>/dev/null || shasum -a 256 -c -))
    tar -xzf "$work/$asset" -C "$work"
    for f in $files; do mv "$work/$name/$f" "$work/$f"; done
    source="release $VERSION, $asset"
fi

stage="$work/stage"
mkdir -p "$stage"
for f in $files; do cp "$work/$f" "$stage/$f"; done
cp "$here/run" "$stage/run"
cp "$here/../../LICENSE" "$stage/LICENSE"
chmod 755 "$stage/run" "$stage/MruOpsSat"
chmod 644 "$stage/shrink.bin" "$stage/tmr.bin" "$stage/restart.bin" "$stage/LICENSE"
sha() { (sha256sum "$1" 2>/dev/null || shasum -a 256 "$1") | cut -d' ' -f1; }
sum=$(sha "$stage/MruOpsSat")
cat >"$stage/README.txt" <<TXT
Mru Flight on OPS-SAT PRETTY, experiment exp4051, run 3: the F´ deployment
Binary: $source
MruOpsSat sha256: $sum

What it is: one F´ (NASA JPL's flight software framework) deployment. Mru's
four F´ components, RedundancyManager, HealthRegistry, three ReplicaHost
threads and FaultInjector, run with F´'s own command dispatcher, event
manager, telemetry database, health pings and command sequencer, driven by a
Linux timer every 100 ms. Three software replicas compute the same task; the
manager votes, compares or self-checks, using Mru's Rust decision core. The
faults are commanded by F´ command sequences (no hardware is touched, nothing
is commanded from outside during the run). Three segments, one after the
other, then it exits:

  1  shrink   shrink.bin: the shrinking quorum, and the faults that flew in
              run 1: one bit flip in replica 2 at cycle 60, replica 1 stuck
              at 120, replica 0 stops answering at 240. Cap 45 s.
  2  tmr      tmr.bin: the same faults under fixed triple redundancy. Cap 35 s.
  3  restart  restart.bin: the shrinking quorum with the manager's state
              file, replica 1 stuck at cycle 200. After 35 s the script kills
              the process without warning (SIGKILL, our own process, in user
              space), copies the state file aside and starts the deployment
              again with no sequence; the manager restores its state and runs
              30 s more. No platform reboot, nothing commanded. The segment
              starts from cycle 1: a state file left in this folder by an
              earlier run of the package is removed first, and run-info.txt
              says so. The shell may print one "Killed" line for this; it is
              the planned kill, recorded in run-info.txt.

How to run: ./run (foreground, as user exp, in this folder). It stops by
itself: the caps add up to 145 s, under five minutes in total. Stop early
with SIGTERM: the running deployment stops its timer, writes its summary and
exits.

What it touches: user space only, no network, no device files, no reboots.
One process at a time, about 15 threads. Memory limit 128 MB of address space
(RLIMIT_AS), set by the process itself; about 14 MB resident. Storage: about
1.4 MB for the package, a few tens of kilobytes of output.

Output, all in toGround/: run-info.txt (start, end, kill time, exit codes,
checksums); for each segment summary-NAME.txt (counts, footprint) and
events-NAME.txt (F´ events as text lines); for segment 3 the events before
and after the kill (events-restart-1.txt, events-restart-2.txt), the state
file as it was at the kill (state-restart-at-kill.dat) and the final one
(state-restart.dat).

Source and documentation: https://github.com/mruspace/flight (docs/opssat.md)
Contact: contact@mru.space
TXT
chmod 644 "$stage/README.txt"

out="$PWD/exp4051-mru-flight-fprime-$VERSION.tar.gz"
entries="run MruOpsSat shrink.bin tmr.bin restart.bin README.txt LICENSE"
if tar --version 2>/dev/null | grep -q bsdtar; then
    # shellcheck disable=SC2086
    tar -C "$stage" --no-mac-metadata --uname exp --gname exp --uid 1024 --gid 1024 -czf "$out" $entries
else
    # shellcheck disable=SC2086
    tar -C "$stage" --owner=exp:1024 --group=exp:1024 -czf "$out" $entries
fi
echo "wrote $out"

# List and check the archive with Python's tarfile, which hides nothing:
# exactly the seven files, no "._" entries, every entry owned by exp.
python3 -I - "$out" <<'PY'
import sys, tarfile
want = ["run", "MruOpsSat", "shrink.bin", "tmr.bin", "restart.bin", "README.txt", "LICENSE"]
with tarfile.open(sys.argv[1]) as t:
    members = t.getmembers()
for m in members:
    print(f"{m.mode:o} {m.uname}/{m.gname} {m.size:>8} {m.name}")
names = [m.name for m in members]
bad = [n for n in names if n.startswith("._") or "/._" in n]
if bad:
    sys.exit(f"error: hidden metadata entries in the package: {bad}")
if sorted(names) != sorted(want):
    sys.exit(f"error: package entries {names} differ from {want}")
if any(m.uname != "exp" or m.gname != "exp" for m in members):
    sys.exit("error: an entry is not owned by exp:exp")
print(f"checked: {len(members)} entries, no hidden files, all owned by exp")
PY
