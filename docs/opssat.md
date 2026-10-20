# Running Mru Flight on OPS-SAT

**Status: ran in orbit on 18 October 2026.**
ESA's OPS-SAT Space Lab confirmed the experiment on 14 October 2026 as
**exp4051** for OPS-SAT PRETTY. The same package,
`exp4051-mru-flight-v0.3.1.tar.gz` (release v0.3.1), then ran three times:
in ESA's software environment on 15 October 2026, on TU Graz's PRETTY
FlatSat, the flight processor on the bench, on 16 October 2026, and in orbit
on the satellite on 18 October 2026 at 09:45 UTC. Each time both runs exit
0 and every fault lands on its scheduled tick. The orbit run matched the
prediction written to ESA on 16 October, before the run (numbers below).
Scope: three software replicas on one Linux payload processor, in user
space, with faults from a schedule. Not a hardware-redundancy test, and not
qualification evidence.

A note for the OPS-SAT Space Lab team: what the experiment does on the
satellite, what it touches, its limits, how to start and stop it, what it sends
down, and what it can and cannot show. It describes the current `quorum`
program; a later version will add F´ command and telemetry interfaces (see
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
  thousands of simulated missions, see dusk. The 227-second run in orbit on
  18 October 2026 found no real flip in the 64 MB sensor block, only the
  injected self-test.
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
| CPU share | `--cpu-percent 5` | The voter sleeps after each step so that work, including sensor scans and, since v0.4.0, the fill of the sensor block at start-up, takes at most this share of wall time. Measured in tests at 4.7% for a 5% setting. |
| Run time | `--max-seconds S` | The voter stops cleanly when the time is reached. Since v0.4.0 the clock starts before the replicas are spawned and the sensor is filled, so the cap is the whole life of the process. |
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
writes everything to `toGround/`, stops by itself inside the window, and
exits. A SIGTERM to `run` is forwarded to the running voter, which finishes
its step, writes its summary and exits. Nothing else is written. Settings are
environment variables with the defaults a 5% CPU share for the voter, as
stated in the registration, 128 MB per process, a 64 MB sensor scanned every
250 ticks, and seed 1. `toGround/run-info.txt` records the start and end
times, user, processor and memory from `/proc`, the checksum of the binary,
the settings and the exit code of every voter.

[`opssat/package.sh`](../opssat/package.sh) builds
`exp4051-mru-flight-VERSION.tar.gz`: it downloads the release binary for the
target, checks it against `SHA256SUMS`, adds `run`, the schedules, a
`README.txt` for the operators and the license, and writes the archive with
the required ownership and no hidden entries. `QUORUM_BIN=path ./package.sh`
packages a binary you built yourself.

### Run 2 (v0.4.0, confirmed by ESA on 20 October 2026, not yet scheduled)

Run 1 showed the policy comparison on the flight processor. Run 2 shows the
rest of the lifecycle, and that a reset loses nothing. `run` has three
segments, one after the other:

| # | Segment | Policy | Schedule | Cap |
|---|---|---|---|---|
| 1 | shrink | shrinking quorum | [`faults.lifecycle.txt`](../opssat/faults.lifecycle.txt), packaged as `faults.txt` | 80 s, ends earlier when the quorum halts |
| 2 | tmr | fixed TMR | the same file | 30 s |
| 3 | restart | shrinking quorum, checkpoint every 100 ticks | [`faults.restart.txt`](../opssat/faults.restart.txt), packaged as `faults-restart.txt` | 30 s, then the voter is killed and started again for 30 s more |

The lifecycle schedule, in ticks: a sensor self-test at tick 20; one bit
flip in replica 2's working memory at tick 30 (outvoted once, and since
v0.4.0 logged as `outvoted`); replica 2 stops answering at tick 60 (lost
after the 2 s reply timeout); replica 1 stuck on one wrong value at tick 120
(the two remaining replicas disagree, the shrinking quorum runs the
known-answer test on both, names replica 1 and retires it on that tick; fixed
TMR cannot tell which one failed and delivers nothing more); and bit 0 of
replica 0's result register held at 0 from tick 1,280, on the last replica.
1,280 is a known-answer test tick (the test runs every 64 ticks on the last
replica): the test fails on the tick the fault appears, the replica is
retired, and the run halts at tick 1,281. No wrong result is delivered,
because the program delivers nothing it has not checked on that tick. A
stuck bit that lands between two tests is caught at the next test, and
wrong results get out until then; a bit the test cannot see is missed. The
README's known-answer coverage table gives those numbers.

The restart schedule has one fault: replica 1 stuck at tick 200, outvoted on
ticks 200 to 202 and retired by health scoring at 202. After 30 s the script
kills the voter without warning (SIGKILL, our own process, in user space; no
platform reboot, nothing commanded), copies the checkpoint aside as
`checkpoint-restart-at-kill.txt`, and starts the voter again with the same
settings. The voter resumes from the checkpoint: it starts two replicas, not
three, cuts the event log back to the checkpoint, writes one `resumed` line,
and continues the counts. The segment ends at the second cap with one summary
for the whole run (`resumes=1`).

Measured on 20 October 2026 in ESA's image on a laptop (Apple silicon,
Docker's Linux VM, 32-bit ARM mode), the package built from this branch at
the defaults (5% share, 64 MB sensor scanned every 250 ticks):

- Whole run 109 seconds, every voter exit 0 (the killed one 137, as
  planned), 13 files, 9,023 bytes, nothing else written to the home folder.
- Segment 1 (shrink): 698 correct results, 0 wrong, 2 detected (the
  disagreement at tick 120 and the failed known-answer test at 1,280), 21
  known-answer tests, halted at tick 1,281 after 16.8 s. Events on their
  ticks: self-test flip injected at 20, replica 2 outvoted at 30, replica 2
  hung and lost at 60, replica 1 stuck, retired and the disagreement detected
  at 120, the self-test flip found and repaired at 250, the stuck bit set and
  replica 0 retired at 1,280, halted at 1,281. Voter CPU 0.713 s of 16.755 s
  (4.3%, start-up included), 71,512 KiB resident, replicas 6,016 KiB.
- Segment 2 (fixed TMR): 119 correct results, then a detected disagreement
  on every tick from 120 to 1,869, where the 30 s cap ended it; 0 wrong; the
  same fault events on the same ticks. Voter CPU 4.4%.
- Segment 3 (restart): replica 1 stuck at 200, outvoted on 200 to 202,
  retired at 202; the voter killed 30 s in with its checkpoint at tick 2,000;
  resumed from tick 2,000 (one `resumed` line), ended at the second cap at
  tick 3,961 with 3,960 correct results, 0 wrong, two replicas alive,
  `resumes=1`. Voter CPU 4.6%.

The same package at a 2% share, to mimic a processor about three times
slower (the SEPP against the laptop in run 1): whole run 132 s, segment 1
halted at tick 1,281 after 40.0 s against its 80 s cap, the same counts and
events, the restart from the checkpoint at tick 600, 1,329 correct results
in segment 3, 0 wrong everywhere.

Expected on the SEPP: the same events on the same ticks and the same counts
for segment 1 (698, 0, halted at 1,281), since the schedule is in ticks; the
tick counts of segments 2 and 3 scale with the processor (about 45 ticks per
second in self-check mode on the SEPP in run 1, fewer in compare mode, where
two replicas compute on every tick). Segment 1 should take about 40 s there
against its 80 s cap. The caps add up to 170 s plus a few seconds of start-up
per voter, so the whole run stays under about 200 s in the worst case.

### Run 1 (v0.3.1, flown 18 October 2026)

In run 1, `run` ran one policy after the other, shrink then fixed TMR, with
the same seed and fault schedule, each capped at 110 seconds.
[`opssat/faults.5min.txt`](../opssat/faults.5min.txt) was the schedule, in
ticks so that it is the same on any processor: a sensor self-test at tick 20,
one bit flip in a replica's working memory at tick 60, a stuck replica at
tick 120 and a terminated replica at tick 240. The shrinking quorum retires
the stuck replica, then self-checks on the last one and keeps delivering.
Fixed TMR is left with one good and one stuck replica after tick 240 and
delivers nothing more. The faults sit early so that they land inside the
window at the 5% share even on a processor much slower than the test host.

**Ground test.** ESA's environment tarball is a root filesystem. On a machine
with Docker it imports as an image and runs the package as user `exp`:

```sh
docker import --platform linux/arm/v7 exp_env.tar.gz opssat-pretty-env:v0.3
mkdir home && tar -xzf exp4051-mru-flight-VERSION.tar.gz -C home
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

ESA's software environment test of the same package, run on 15 October 2026
(start 13:05:29 UTC) on a Linux VM with two Cortex-A53 cores in 32-bit mode,
4 GB of memory, as root; results received 16 October:

- Whole run 222 seconds, both exit 0, seven files. The checksum of `quorum`
  in `run-info.txt` matches the release.
- Shrink: 14,886 correct results over 29,532 ticks (about 268 ticks per
  second), no wrong results. Stuck replica retired at tick 122, last replica
  alone from tick 240, sensor self-test flip found and repaired at the scan at
  tick 250 (118 scans). Voter 71,296 KiB resident, replicas 4,992 KiB.
- Fixed TMR: 239 correct results, then a detected disagreement on every tick
  from 240 to 19,250.
- Voter CPU time 5.709 s of 110.004 s wall (5.2%). The main loop is capped at
  5%; the start-up (filling the 64 MB sensor block and spawning the replicas)
  runs outside the cap.
- The bit flip at tick 60 is outvoted at once and leaves no line in the event
  log; it is counted in the detection line (`faults=2 caught=2`).

The sandbox ran about twice as fast as the laptop under emulation, so it has
about twice the results; every event sits on the same tick. In self-check mode
the voter computes on even ticks only, so the shrink count is about half the
tick count after tick 240 by design.

TU Graz's FlatSat test of the same package, run on 16 October 2026 (start
05:20:11 UTC) on the SEPP itself: Altera SoC FPGA, two Cortex-A9 cores, 1 GB
of memory, Linux 4.9, started by systemd as "Experiment 4051" in a container
named exp-4051, as user exp; results received the same day:

- Whole run 224 seconds, both exit 0, seven files, checksum matches the
  release.
- Shrink: 2,745 correct results over 5,250 ticks, no wrong results. Stuck
  replica retired at tick 122, last replica alone from tick 240, sensor
  self-test flip found and repaired at the scan at tick 250 (21 scans). Voter
  65,992 KiB resident, replicas 452 KiB.
- Fixed TMR: 239 correct results, then a detected disagreement on every tick
  from 240 to 4,778.
- **Tick rate on the flight processor at the 5% share: about 45 per second**
  (5,251 ticks in 113.2 s for shrink, 4,779 in 110.0 s for fixed TMR), about
  six times slower than ESA's sandbox VM and three times slower than the
  laptop. The last fault lands about five seconds in.
- The shrink run ended at 113.2 s instead of 110: the time limit is checked
  once per tick, and tick 5,250 was a scan of the 64 MB sensor block, which is
  slow on this processor, followed by its 5% sleep. The total stayed well
  inside the five-minute window.
- Voter CPU time 5.880 s of 113.247 s (5.2%) for shrink and 6.000 s of
  110.004 s (5.4%) for fixed TMR, including the unthrottled start-up.

Every event sits on the same tick on all three hosts. Expected for the run in
orbit, stated to ESA on 16 October before the run: the same events on the
same ticks, about 2,700 correct results for shrink and 239 for fixed TMR,
zero wrong in both, about 225 seconds in total, and any real bit flip in the
64 MB sensor block as an extra `sensor_upset` line with its location.

### In orbit, 18 October 2026

Mru Flight ran in orbit. ESA started experiment exp4051 on OPS-SAT PRETTY on
18 October 2026 at 09:45:37 UTC, on the SEPP payload processor (host `sepp1`,
Linux 4.9, systemd unit "Experiment 4051", container `exp-4051`, user `exp`,
user space only), eleven days after registration. The whole run took 227
seconds: the shrinking quorum ended at 09:47:31 UTC and fixed TMR at 09:49:24
UTC, both with exit 0. The checksum of `quorum` in `run-info.txt` matches
release v0.3.1. The files that came down, plus the journal lines from TU Graz,
are in
[`opssat/results/2026-10-18-ops-sat-pretty-on-board`](../opssat/results/2026-10-18-ops-sat-pretty-on-board);
the software environment run is in
[`2026-10-15-esa-software-environment`](../opssat/results/2026-10-15-esa-software-environment)
and the FlatSat run in
[`2026-10-16-tu-graz-flatsat`](../opssat/results/2026-10-16-tu-graz-flatsat).

| | Shrinking quorum | Fixed TMR |
|---|---|---|
| Host | `sepp1`, the SEPP of OPS-SAT PRETTY, in orbit | same |
| Start (UTC) | 09:45:37 | 09:47:31, when the shrink run ended |
| Wall time | 113.5 s | 113.1 s (227 s in total) |
| Exit code | 0 | 0 |
| Correct results | 2,745 over 5,250 ticks | 239, then a detected disagreement on every tick from 240 to 4,500 |
| Wrong results | 0 | 0 |
| Replicas alive at the end | 1 | 2 |
| Known-answer tests | 80 | 0 |
| Tick rate at the 5% share | about 46 per second | about 40 per second |
| Memory, voter and replicas | 65,992 KiB, 452 KiB | 65,992 KiB, 452 KiB |
| Voter CPU time | 5.930 s of 113.521 s (5.2%) | 5.800 s of 113.136 s (5.1%) |
| Sensor, 64 MB | 21 scans, 1 flip (the self-test) | 18 scans, 1 flip (the self-test) |

Events, from the logs: sensor self-test injected at tick 20, replica 1 stuck
at tick 120 and retired at tick 122, replica 0 killed at tick 240, the
self-test flip found and repaired at the scan at tick 250. The bit flip at
tick 60 was outvoted at once (`faults=2 caught=2`). Fixed TMR detected a
disagreement from tick 240 on and delivered nothing more. Every event sits on
the same tick as on the FlatSat and in ESA's software environment. The shrink
counts (2,745 correct over 5,250 ticks) and the 239 for fixed TMR are the
same as on the FlatSat two days earlier.

Every number matches the prediction written to ESA on 16 October at 14:10
UTC, before the run: the same events on the same ticks, about 2,700 correct
results for shrink (2,745), 239 for fixed TMR (239), zero wrong (0 and 0),
about 225 seconds (227), and no `sensor_upset` line other than the self-test
(none).

Both runs overran the 110-second cap by about 3 seconds, as the shrink run
did on the FlatSat: in v0.3.1 the limit is checked once per tick, and the last
tick was a scan of the 64 MB sensor block. The total stayed well inside the
five-minute window. Fixed after the run, for the next package: the scan looks
at the limit every 1 MiB and stops there, and the CPU-share sleep never runs
past it, so a run ends within a few milliseconds of the cap.

Scope: three software replicas on one Linux payload processor, in user space,
with faults from a schedule. The run tests the software policy on the flight
processor, not hardware redundancy, and it is not qualification evidence.
With no real upset in 227 seconds it calibrates nothing about the radiation
environment; the 1.34× figure in the README comes from dusk's 10,000
simulated missions, not from this run.

## Fault schedule

Faults are read from `faults.txt` next to `run.sh`, one per line as
`kind:replica@step`. The release includes `faults.example.txt`:

```
sensor:0@1000      # self-test of the radiation sensor
corrupt:2@50000    # one bit flip in replica 2's working memory
stuck:1@200000     # replica 1 keeps returning the same wrong value
kill:0@600000      # replica 0 is terminated
```

This schedule is for long runs. The OPS-SAT package uses the short schedules
instead (see above): `faults.5min.txt` flew in run 1, `faults.lifecycle.txt`
and `faults.restart.txt` are run 2. A second run with `POLICY=tmr` and the
same schedule gives the comparison; `run` does the segments by itself.

## What comes down

All in the output folder:

- **`summary-POLICY.txt`** (five lines, six with a fault schedule): results
  delivered, disagreements detected, wrong results, when and why the run
  stopped, the tick of the last correct result, footprint, limits, the
  sensor's scans and flips, and how fast the voter reacted to each injected
  fault.
- **`quorum-POLICY.csv`**, the event log: one short line per event (fault
  injected, replica outvoted, retired, cleared or lost, disagreement detected,
  sensor bit flip with its location, resumed from a checkpoint). Identical
  events on consecutive ticks are written as the first line plus one closing
  line with the count. With no real upsets, a day-long run produces a few
  kilobytes.
- **`stdout-POLICY.txt`**: the console output, a copy of the summary.
- **`run-info.txt`** (OPS-SAT package only): start and end times, user,
  checksum of the binary, settings and the exit code of each voter.
- In the run 2 package the restart segment adds the console output before
  and after the kill (`stdout-restart-1.txt`, `stdout-restart-2.txt`), the
  checkpoint as it was at the kill (`checkpoint-restart-at-kill.txt`) and
  the final checkpoint (`checkpoint-restart.txt`), a few hundred bytes each.

## What success looks like

1. The shrinking quorum delivers more correct results than fixed TMR under the
   same injected faults, as on the ground. **Outcome in orbit:** 2,745
   against 239, zero wrong in both.
2. Every injected fault is detected and handled as expected (stuck replica
   retired, lost replica dropped, mode stepped down, sensor self-test flip
   found). **Outcome:** all four, on the same ticks as on the ground: stuck at
   120 and retired at 122, killed at 240 with the mode stepped down, the
   self-test flip found and repaired at the scan at 250, the bit flip at 60
   outvoted at once.
3. Any real bit flips, in the replicas or in the sensor, are logged with their
   time and location. PRETTY's dosimeter data is not available to
   experimenters (ESA, 16 October 2026), so the sensor's counts stand on their
   own. **Outcome:** none in 227 seconds. The only flip in the 64 MB sensor
   block was the injected self-test, one per run (21 and 18 scans).
4. No limit is ever exceeded, and the platform is never affected. **Outcome:**
   the voter stayed at 65,992 KiB and each replica at 452 KiB against the 128
   MB limit; voter CPU time was 5.930 s over 113.521 s of wall (shrink) and
   5.800 s over 113.136 s (fixed TMR), inside the 5% share once start-up is
   counted; the load average at start was 0.12. The 110-second cap per run
   was overrun by about 3 seconds (see above). On the platform, the only
   evidence is what ESA's OPS-SAT Space Lab reported: the run "has been
   completed successfully". Nothing else about the platform was reported.

Result files: [`opssat/results`](../opssat/results).
