# Running Mru Flight on OPS-SAT

**Status: registered.** ESA's OPS-SAT Space Lab confirmed the experiment on
14 October 2026 as **exp4051** for OPS-SAT PRETTY. Nothing has run on the
satellite yet. The next steps are ESA's test in the software environment, TU
Graz's test on the PRETTY FlatSat, then the run in orbit.

A note for the OPS-SAT Space Lab team: what the experiment does on the
satellite, what it touches, its limits, how to start and stop it, what it sends
down, and what it can and cannot show. It describes the current `quorum`
program; the flight version will add F´ command and telemetry interfaces (see
[fprime-design.md](fprime-design.md)).

## What runs

One program, `quorum`, as a fully static Linux binary (no shared libraries,
about 0.6 MB), plus a small launcher script, `run.sh`. Release packages are
provided for 64-bit ARM, 32-bit ARM (hard and soft float) and x86_64 Linux.
On every change, CI runs the full scenario tests on each ARM binary under
emulation.

At start the program launches three copies of itself as replicas. The voter
sends each replica a small deterministic task (a hash over 4 KiB of working
memory), compares the results, and steps down from vote to compare to
self-check as replicas fail or are retired. Faults are injected by the voter on
a published schedule.

Optionally, the voter also holds a **radiation sensor**: a block of memory
(64 MB by default in `run.sh`) filled with a known pattern and checked at a
fixed interval. Every bit found flipped is logged with its location and
repaired.

## Scope and limits of the experiment

**What it shows**

- The shrinking quorum's decisions and the operations behavior on real flight
  hardware: stepping down as replicas are lost, retiring a stuck replica,
  diagnosing with known-answer tests, recovering from a hung replica.
- That the software stays inside its limits for the whole run, and stops
  cleanly on command.
- How often real radiation flips bits in unprotected memory, if the platform
  memory has no error correction, measured by the sensor.

**What it does not show**

- **Hardware independence.** All three replicas run on one processor, so a
  fault that affects the processor itself (a latch-up, a reset) affects all of
  them. The experiment tests the software policy, not hardware redundancy.
  That comes next, on separate boards of the ground bench and, later, on
  hardware with independent processors.
- **A reliability estimate.** With a few megabytes of working memory in low
  Earth orbit, real upsets in the replicas will be rare, so the main evidence
  comes from the injected faults. Real upsets are a bonus. For statistics over
  thousands of simulated missions, see dusk.
- **Upsets behind error correction.** If the platform memory has error
  correction, the sensor will report few or no flips. That is a valid result:
  it measures what the software actually sees.

## What it touches

| | |
|---|---|
| Platform control | **None.** User space only: no drivers, no device files, no reboots, no privileged calls. |
| Network | **None.** Replicas talk to the voter over pipes only. |
| Files | Reads an optional fault schedule. Writes an event log, a summary and the console output, all in one output folder. |
| Processes | The voter and three replicas. Replicas exit on their own when the voter ends, for any reason. |

## Limits, enforced by the program itself

| Limit | Setting | How it is enforced |
|---|---|---|
| Memory | `--max-memory-mb 128` (default) | Address-space limit set by each process on itself (Linux `RLIMIT_AS`). The kernel refuses allocations above it. A sensor too large for the limit is refused at start. |
| CPU time | `--max-cpu-seconds S` | CPU-time limit set by each process on itself (`RLIMIT_CPU`). |
| CPU share | `--cpu-percent 5` | The voter sleeps after each step so that work, including sensor scans, takes at most this share of wall time. Measured in tests at 4.7% for a 5% setting. |
| Run time | `--max-seconds S` | The voter stops cleanly when the time is reached. |
| Unresponsive replica | `--reply-timeout-ms 2000` (default) | A replica that does not answer in time is treated as lost and killed. |

Measured footprint in CI (Linux ARM64, 20,000 steps): about 1.5 MB resident
memory per process, plus the sensor's size in the voter when it is on.

## Start, check and stop

```sh
./run.sh start     # starts a run in the background
./run.sh status    # running, or the summary once it has finished
./run.sh stop      # SIGTERM, then SIGKILL after 30 s if needed
```

`run.sh` takes its settings from environment variables (policy, seed, CPU
share, memory limit, maximum run time, sensor size, fault file and output
folder); the defaults are a 5% CPU share, 128 MB per process, 24 hours and a
64 MB sensor. Use the same settings for all three commands.

Ways the run ends:

- **SIGTERM or SIGINT** (as `run.sh stop` sends): the voter finishes the
  current step, ends the replicas, writes its summary and exits with status 0.
- **SIGKILL** on the voter, if ever needed: the replicas see their input close
  and exit by themselves.
- **Maximum run time**: the run ends on its own, with a summary.

`run.sh` is for long runs on any Linux system. The OPS-SAT runner uses the
foreground entry point `run` instead, described next.

## The OPS-SAT PRETTY package

The OPS-SAT experiment environment (ESA's `exp_env` README, v0.3) sets these
rules: the package is one `tar.gz` with relative paths and every file owned by
user and group `exp`; the runner unpacks it into the home folder of user `exp`,
executes `./run` there, and collects everything written under that folder
when the run ends. The target is Alpine Linux 3.21 for 32-bit ARM (armhf,
musl): the `armv7-unknown-linux-musleabihf` release binary runs there with no
library from the image. The processing platform is a Cyclone V SoC with a
dual-core Cortex-A9 at 800 MHz and 1 GB of memory. Each run must complete in
five minutes or less.

[`opssat/run`](../opssat/run) is that entry point. It runs in the foreground,
one policy after the other, shrink then fixed TMR, with the same seed and
fault schedule, each capped at 110 seconds, so the whole run ends by itself
inside the window. All output goes to `toGround/`: `run-info.txt` (start and
end times, user, processor and memory from `/proc`, checksum of the binary,
settings, exit codes) and, for each policy, the summary, the event log and the
console output. A SIGTERM to `run` is forwarded to the voter, which finishes
its step, writes its summary and exits. Nothing else is written. Settings are
environment variables with the defaults a 5% CPU share for the voter, as
stated in the registration, 128 MB per process, a 64 MB sensor scanned every
250 ticks, and seed 1.

[`opssat/faults.5min.txt`](../opssat/faults.5min.txt) is the schedule for
that window, in ticks so that it is the same on any processor: a sensor
self-test at tick 20, one bit flip in a replica's working memory at tick 60,
a stuck replica at tick 120 and a terminated replica at tick 240. The
shrinking quorum retires the stuck replica, then self-checks on the last one
and keeps delivering. Fixed TMR is left with one good and one stuck replica
after tick 240 and delivers nothing more. The faults sit early so that they
land inside the window at the 5% share even on a processor much slower than
the test host.

[`opssat/package.sh`](../opssat/package.sh) builds
`exp4051-mru-flight-VERSION.tar.gz`: it downloads the release binary for the
target, checks it against `SHA256SUMS`, adds `run`, the schedule as
`faults.txt`, a `README.txt` for the operators and the license, and writes the
archive with the required ownership. `QUORUM_BIN=path ./package.sh` packages a
binary you built yourself.

**Ground test.** ESA's environment tarball is a root filesystem. On a machine
with Docker it imports as an image and runs the package as user `exp`:

```sh
docker import --platform linux/arm/v7 exp_env.tar.gz opssat-pretty-env:v0.3
mkdir home && tar -xzf exp4051-mru-flight-v0.3.1.tar.gz -C home
docker run --rm --platform linux/arm/v7 -v "$PWD/home:/home/exp" -w /home/exp \
    -u 1024:1024 -e HOME=/home/exp opssat-pretty-env:v0.3 ./run
```

Measured on 15 October 2026 in that image on a laptop (Apple silicon, Docker's
Linux VM, 32-bit ARM mode), the published package
`exp4051-mru-flight-v0.3.1.tar.gz` (release v0.3.1), 5% CPU share, 64 MB
sensor scanned every 250 ticks, 110 seconds per policy:

- Whole run 223 seconds wall, both exit 0, output seven files, 6,388 bytes.
- Shrink: 7,495 correct results over 14,750 ticks (about 130 ticks per second
  at the 5% share), no wrong results. Stuck replica retired at tick 122, last
  replica alone from tick 240, sensor self-test flip found and repaired at the
  scan at tick 250. Voter CPU time 5.0 s of 111.0 s (4.5%), 70 MB resident
  (the sensor), replicas 5.9 MB.
- Fixed TMR: 239 correct results, then a disagreement on every tick from 240
  to the end, written as two lines in the log.
- An earlier run the same day at a 25% share and the previous schedule (faults
  at ticks 50, 150, 300, 600) gave 52,282 correct results for shrink and 599
  for fixed TMR over the same 220 seconds. The share was lowered to match the
  registration.

The flight processor is slower than the laptop, so the tick counts in orbit
will be lower and the fault ticks sit early in the schedule for that reason.

## Fault schedule

Faults are read from `faults.txt` next to `run.sh`, one per line as
`kind:replica@step`. The release includes `faults.example.txt`:

```
sensor:0@1000      # self-test of the radiation sensor
corrupt:2@50000    # one bit flip in replica 2's working memory
stuck:1@200000     # replica 1 keeps returning the same wrong value
kill:0@600000      # replica 0 is terminated
```

This schedule is for long runs. The OPS-SAT package uses
`faults.5min.txt` instead (see above). A second run with `POLICY=tmr` and the
same schedule gives the comparison; `run` does both runs by itself.

## What comes down

All in the output folder:

- **`summary-POLICY.txt`** (five lines, six with a fault schedule): results
  delivered, disagreements detected, wrong results, when and why the run
  stopped, the tick of the last correct result, footprint, limits, the
  sensor's scans and flips, and how fast the voter reacted to each injected
  fault.
- **`quorum-POLICY.csv`**, the event log: one short line per event (fault
  injected, replica retired or cleared or lost, disagreement detected, sensor
  bit flip with its location). Identical events on consecutive ticks are
  written as the first line plus one closing line with the count. With no real
  upsets, a day-long run produces a few kilobytes.
- **`stdout-POLICY.txt`**: the console output, a copy of the summary.
- **`run-info.txt`** (OPS-SAT package only): start and end times, user,
  checksum of the binary, settings and the exit code of each run.

## What success looks like

1. The shrinking quorum delivers more correct results than fixed TMR under the
   same injected faults, as on the ground.
2. Every injected fault is detected and handled as expected (stuck replica
   retired, lost replica dropped, mode stepped down, sensor self-test flip
   found).
3. Any real bit flips, in the replicas or in the sensor, are logged with their
   time and location, for comparison with PRETTY's dosimetry data.
4. No limit is ever exceeded, and the platform is never affected.
