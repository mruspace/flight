#!/bin/sh
# package.sh: build the experiment package for OPS-SAT PRETTY [exp4051].
#
# Output: exp4051-mru-flight-VERSION.tar.gz in the current folder, with all
# paths relative and all files owned by user and group "exp", as the OPS-SAT
# experiment environment README requires. The runner unpacks it into $HOME of
# user exp and executes ./run.
#
# Contents: run (entry point), quorum (static 32-bit ARM hard-float binary
# from the GitHub release, checksum verified), faults.txt (the lifecycle
# schedule), faults-restart.txt (the restart schedule), README.txt (for the
# operators) and LICENSE.
#
#   ./package.sh            uses the release in VERSION (default v0.4.0)
#   VERSION=v0.2.0 ./package.sh
#   QUORUM_BIN=path ./package.sh   uses a binary you built yourself
set -eu

# macOS tar stores extended attributes as extra "._NAME" entries, which the
# macOS tar listing then hides. ESA's first test (16 Oct 2026) found them in
# the package. Both lines below stop them, on bsdtar and on GNU tar.
export COPYFILE_DISABLE=1

here=$(cd "$(dirname "$0")" && pwd)
VERSION=${VERSION:-v0.4.0}
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
cp "$here/faults.lifecycle.txt" "$stage/faults.txt"
cp "$here/faults.restart.txt" "$stage/faults-restart.txt"
cp "$here/../LICENSE" "$stage/LICENSE"
chmod 755 "$stage/run" "$stage/quorum"
chmod 644 "$stage/faults.txt" "$stage/faults-restart.txt" "$stage/LICENSE"
sum=$(cd "$stage" && (sha256sum quorum 2>/dev/null || shasum -a 256 quorum) | cut -d' ' -f1)
cat >"$stage/README.txt" <<TXT
Mru Flight on OPS-SAT PRETTY, experiment exp4051, run 2
Binary: $source
quorum sha256: $sum

What it is: three software replicas run the same deterministic task under a
voter. Faults are injected by the voter itself from a schedule file (no
hardware is touched, nothing is commanded during the run). Three segments,
one after the other, then it exits:

  1  shrink   the lifecycle schedule (faults.txt): one replica hangs, one gets
              stuck, the last one gets a stuck bit; the shrinking quorum goes
              from three to two to one and halts by itself with no wrong
              result. Cap 80 s; it ends earlier when it halts.
  2  tmr      the same schedule under fixed triple redundancy. Cap 30 s.
  3  restart  shrink with a checkpoint every 100 ticks (faults-restart.txt).
              After 30 s the script kills the voter without warning (SIGKILL,
              our own process, in user space), copies its checkpoint aside and
              starts it again; it resumes from the checkpoint and runs 30 s
              more. No platform reboot, nothing commanded. The shell that
              runs the script may print one "Killed" line for this; it is
              the planned kill, recorded in run-info.txt.

How to run: ./run (foreground, as user exp, in this folder). It stops by
itself: the caps add up to 170 s plus a few seconds of start-up per voter,
under five minutes in total. Stop early with SIGTERM: the running voter
finishes its step, writes its summary and exits.

What it touches: user space only, no network, no device files, no reboots.
At most four processes at a time. Memory limit 128 MB per process
(RLIMIT_AS), set by the processes themselves. CPU share of the voter capped
at 5% of one core, start-up included. Storage: about 1 MB for the package, a
few kilobytes of output.

Output, all in toGround/: run-info.txt (start, end, kill time, exit codes);
for segments 1 and 2 summary-NAME.txt (six lines), quorum-NAME.csv (event
log) and stdout-NAME.txt (console output); for segment 3 one summary and one
event log for the whole run (summary-restart.txt, quorum-restart.csv), the
console output before and after the kill (stdout-restart-1.txt,
stdout-restart-2.txt), the checkpoint as it was at the kill
(checkpoint-restart-at-kill.txt) and the final checkpoint
(checkpoint-restart.txt).

Source and documentation: https://github.com/mruspace/flight (docs/opssat.md)
Contact: contact@mru.space
TXT

out="$PWD/exp4051-mru-flight-$VERSION.tar.gz"
if tar --version 2>/dev/null | grep -q bsdtar; then
    tar -C "$stage" --no-mac-metadata --uname exp --gname exp --uid 1024 --gid 1024 -czf "$out" run quorum faults.txt faults-restart.txt README.txt LICENSE
else
    tar -C "$stage" --owner=exp:1024 --group=exp:1024 -czf "$out" run quorum faults.txt faults-restart.txt README.txt LICENSE
fi
echo "wrote $out"

# List and check the archive with Python's tarfile, which hides nothing:
# exactly the six files, no "._" entries, every entry owned by exp.
if command -v python3 >/dev/null 2>&1; then
    python3 -I - "$out" <<'PY'
import sys, tarfile
want = ["run", "quorum", "faults.txt", "faults-restart.txt", "README.txt", "LICENSE"]
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
else
    echo "python3 not found: the tar listing below can hide ._ entries on macOS" >&2
    tar -tzvf "$out"
fi
