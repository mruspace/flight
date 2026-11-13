#!/bin/sh
# package.sh: build the run 4 experiment package for OPS-SAT PRETTY [exp4051]:
# a device that stops answering, automatic resets, sensors that disagree,
# and the same decision core as a NASA cFS application.
#
# Output: exp4051-mru-flight-fprime-resets-VERSION.tar.gz in the current folder, with
# all paths relative and all files owned by user and group "exp", as the
# OPS-SAT experiment environment README requires. The runner unpacks it into
# $HOME of user exp and executes ./run.
#
# Contents: run (entry point), MruQuorum (the manager process) and MruReplica
# (one replica process), static 32-bit ARM hard-float binaries from the
# GitHub release, checksum verified; reset.bin, shrink.bin, tmr.bin and sensor.bin (the
# F´ command sequences, compiled against the manager's dictionary, from the
# same release asset); README.txt (for the operators) and LICENSE.
#
#   VERSION=v0.8.0 ./package.sh     uses that release (there is no default yet)
#   DIST=opssat/run4/dist CFS_DIST=cfs/dist ./package.sh
#                                   uses local builds (build.sh, cfs/build.sh)
set -eu

# macOS tar stores extended attributes as extra "._NAME" entries; both
# settings below stop them (ESA found them in the run 1 package).
export COPYFILE_DISABLE=1

here=$(cd "$(dirname "$0")" && pwd)
VERSION=${VERSION:-}
if [ -z "${DIST:-}" ] && [ -z "$VERSION" ]; then
    echo "set VERSION (a release with run 4) or DIST (a local build)" >&2
    exit 2
fi
VERSION=${VERSION:-local}
PLATFORM=armv7-linux-static
REPO=mruspace/flight
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
files="MruQuorum MruReplica reset.bin shrink.bin tmr.bin sensor.bin"

if [ -n "${DIST:-}" ]; then
    for f in $files; do cp "$DIST/$f" "$work/$f"; done
    mkdir -p "$work/cfs/cf"
    cp "${CFS_DIST:?set CFS_DIST with DIST}/core-cpu1" "$work/cfs/core-cpu1"
    cp "$CFS_DIST/cf/cfe_es_startup.scr" "$work/cfs/cf/cfe_es_startup.scr"
    source="local builds in $DIST and $CFS_DIST"
else
    name="mru-fprime-resets-$VERSION-$PLATFORM"
    asset="$name.tar.gz"
    base="https://github.com/$REPO/releases/download/$VERSION"
    curl -fsSL -o "$work/$asset" "$base/$asset"
    curl -fsSL -o "$work/SHA256SUMS" "$base/SHA256SUMS"
    (cd "$work" && grep " $asset\$" SHA256SUMS | (sha256sum -c - 2>/dev/null || shasum -a 256 -c -))
    tar -xzf "$work/$asset" -C "$work"
    for f in $files; do mv "$work/$name/$f" "$work/$f"; done
    mv "$work/$name/cfs" "$work/cfs"
    source="release $VERSION, $asset"
fi

stage="$work/stage"
mkdir -p "$stage"
for f in $files; do cp "$work/$f" "$stage/$f"; done
mkdir -p "$stage/cfs/cf"
cp "$work/cfs/core-cpu1" "$stage/cfs/core-cpu1"
cp "$work/cfs/cf/cfe_es_startup.scr" "$stage/cfs/cf/cfe_es_startup.scr"
chmod 755 "$stage/cfs" "$stage/cfs/cf" "$stage/cfs/core-cpu1"
chmod 644 "$stage/cfs/cf/cfe_es_startup.scr"
cp "$here/run" "$stage/run"
cp "$here/../../LICENSE" "$stage/LICENSE"
chmod 755 "$stage/run" "$stage/MruQuorum" "$stage/MruReplica"
chmod 644 "$stage/reset.bin" "$stage/shrink.bin" "$stage/tmr.bin" "$stage/sensor.bin" "$stage/LICENSE"
sha() { (sha256sum "$1" 2>/dev/null || shasum -a 256 "$1") | cut -d' ' -f1; }
sum=$(sha "$stage/MruQuorum")
sumReplica=$(sha "$stage/MruReplica")
sumCfs=$(sha "$stage/cfs/core-cpu1")
cat >"$stage/README.txt" <<TXT
Mru Flight on OPS-SAT PRETTY, experiment exp4051, run 4: a device that stops
answering, automatic resets, and sensors that disagree
Binaries: $source
MruQuorum sha256: $sum
MruReplica sha256: $sumReplica
cfs/core-cpu1 sha256: $sumCfs

What it is: the F´ deployment of run 3 (NASA JPL's flight software framework),
in four processes, with two new behaviors. When a replica stops answering,
Mru resets it, tests it with a known answer, and takes it back only if the
test passes; it resets each replica at most three times in a run, then keeps
it out. And SensorVoter chooses one value from three sensors of the same
quantity within a tolerance, and excludes a sensor that keeps disagreeing.
Segment 5 runs the same decision core as an application of NASA's core
Flight System (cFS v7.0.1: cFE, OSAL and PSP), cfs/core-cpu1. MruQuorum, the manager process, runs RedundancyManager,
HealthRegistry and FaultInjector with F´'s own command dispatcher, event
manager, telemetry database, health pings and command sequencer, driven by a
Linux timer every 100 ms. It starts three MruReplica processes, one
ReplicaHost each, linked through F´'s GenericHub over Unix socket pairs (no
network), and pins replica 0 to core 0 and replicas 1 and 2 to core 1.

The faults of segments 1 to 3:
  cycle  60  replica 1 stops answering (a hang, commanded by the sequence and
             applied by FaultInjector). A reset from the manager ends the
             hang in FaultInjector, as a platform reset ends it in a device.
  cycle 150  replica 2's process is killed: the manager sends it SIGKILL.
             A real dead unit: no reset brings it back.
  cycle 300  replica 1 hangs again.
Nothing outside these processes is touched. Four segments, one after the
other, then it exits:

  1  reset    reset.bin: the shrinking quorum with automatic resets. Cap 40 s.
  2  shrink   shrink.bin: the shrinking quorum, resets off. Cap 40 s.
  3  tmr      tmr.bin: fixed triple redundancy, no resets. Cap 40 s.
  4  sensor   sensor.bin: three simulated sensors of one quantity, the true
              value known, each reading with noise. SensorVoter chooses one
              value within a tolerance; a spike on sensor 1 at cycle 60, a
              drift on sensor 2 from cycle 120, sensor 0 frozen from cycle
              260. The replicas run with no faults and no kill. Cap 40 s.
  5  cfs      cfs/core-cpu1: Mru as a cFS application, one static binary, three
              replicas as cFE child tasks, the faults of run 1 (a flip at cycle
              60, a stuck replica at 120, a hung one at 240), 40 s of cycles.
              It ends itself with a power-on reset, which deletes cFE's shared
              memory. It writes a working folder (cfs-work/, with a 512 KB
              EEPROM.DAT and three empty key files) and a folder in /dev/shm
              (osal:RAM); run deletes both, and any shared memory segment
              that appeared during the segment and is still there. It opens
              no socket; run records the count.

How to run: ./run (foreground, as user exp, in this folder). It stops by
itself: the caps add up to 200 s, under five minutes in total. Stop early
with SIGTERM: the running manager stops its timer, writes its summary, and
its replica processes end.

What it touches: user space only, no network, no device files, no reboots.
At most four processes at a time. Segment 5 uses System V shared memory and
POSIX message queues (within the kernel's limits for a normal user) and
removes what it created. Memory limit 128 MB of address space per
process (RLIMIT_AS), set by each process itself. Each replica process pins
itself to one core (sched_setaffinity on itself). Storage: about 2.5 MB for
the package, a few tens of kilobytes of output.

Output, all in toGround/: run-info.txt (start, end, exit codes, checksums);
for each segment summary-NAME.txt (counts, footprint, the CPU time by phase,
how each replica process ended and the experiment's CPU share; for segment
4 also the sensors' counts) and events-NAME.txt; for segment 5
summary-cfs.txt, events-cfs.txt (cFE's console, with the app's events) and
cfs-mru.cfg (its fault schedule) (F´ events as text lines,
the core of each replica process, the real fault).

Source and documentation: https://github.com/mruspace/flight (docs/opssat.md,
docs/fault-model.md)
Contact: contact@mru.space
TXT
chmod 644 "$stage/README.txt"

out="$PWD/exp4051-mru-flight-fprime-resets-$VERSION.tar.gz"
entries="run MruQuorum MruReplica reset.bin shrink.bin tmr.bin sensor.bin cfs README.txt LICENSE"
if tar --version 2>/dev/null | grep -q bsdtar; then
    # shellcheck disable=SC2086
    tar -C "$stage" --no-mac-metadata --uname exp --gname exp --uid 1024 --gid 1024 -czf "$out" $entries
else
    # shellcheck disable=SC2086
    tar -C "$stage" --owner=exp:1024 --group=exp:1024 -czf "$out" $entries
fi
echo "wrote $out"

# List and check the archive with Python's tarfile, which hides nothing:
# exactly these entries, no "._" entries, every entry owned by exp.
python3 -I - "$out" <<'PY'
import sys, tarfile
want = ["run", "MruQuorum", "MruReplica", "reset.bin", "shrink.bin", "tmr.bin", "sensor.bin", "cfs", "cfs/cf",
        "cfs/cf/cfe_es_startup.scr", "cfs/core-cpu1", "README.txt", "LICENSE"]
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
