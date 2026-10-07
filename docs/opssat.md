# Running Mru Flight on OPS-SAT

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

- The shrinking quorum's decisions and the operations behaviour on real flight
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

If the satellite's software environment requires experiments to be packaged
in a particular way (for example as an app of ESA's NanoSat MO Framework, as on
OPS-SAT-1), the program and script stay the same and a small wrapper starts
and stops them.

## Fault schedule

Faults are read from `faults.txt` next to `run.sh`, one per line as
`kind:replica@step`. The release includes `faults.example.txt`:

```
sensor:0@1000      # self-test of the radiation sensor
corrupt:2@50000    # one bit flip in replica 2's working memory
stuck:1@200000     # replica 1 keeps returning the same wrong value
kill:0@600000      # replica 0 is terminated
```

Fault times, run length and CPU share would be agreed with the OPS-SAT team. A
second run with `POLICY=tmr` and the same schedule gives the comparison.

## What comes down

All in the output folder:

- **`summary-POLICY.txt`** (five lines): results delivered, disagreements
  detected, wrong results, when and why the run stopped, footprint, limits, and
  the sensor's scans and flips.
- **`quorum-POLICY.csv`**, the event log: one short line per event (fault
  injected, replica retired or cleared or lost, disagreement detected, sensor
  bit flip with its location). With no real upsets, a day-long run produces a
  few kilobytes.
- **`stdout-POLICY.txt`**: the console output, a copy of the summary.

## What success looks like

1. The shrinking quorum delivers more correct results than fixed TMR under the
   same injected faults, as on the ground.
2. Every injected fault is detected and handled as expected (stuck replica
   retired, lost replica dropped, mode stepped down, sensor self-test flip
   found).
3. Any real bit flips, in the replicas or in the sensor, are logged with their
   time and location, for comparison with PRETTY's dosimetry data.
4. No limit is ever exceeded, and the platform is never affected.
