# Running Mru Flight on OPS-SAT

A one-page note for the OPS-SAT Space Lab team: what the experiment does on the
satellite, what it touches, its limits, how to stop it and what it sends down.
It describes the current `quorum` program; the flight version will add F´
command and telemetry interfaces (see [fprime-design.md](fprime-design.md)).

## What runs

One program, `quorum`, as a fully static Linux binary (no shared libraries,
about 0.6 MB). Release builds are provided for 64-bit and 32-bit ARM Linux;
the scenario tests run on both under emulation in CI on every change.

At start it launches three copies of itself as replicas. The voter sends each
replica a small deterministic task, compares the results, and steps down from
vote to compare to self-check as replicas fail or are retired. Faults are
injected by the voter on a published schedule; real upsets, if any, show up as
disagreements in the log.

## What it touches

| | |
|---|---|
| Platform control | **None.** User space only: no drivers, no device files, no reboots, no privileged calls. |
| Network | **None.** Replicas talk to the voter over pipes only. |
| Files | Writes **one** log file, if `--log` is given. Reads nothing else. |
| Processes | The voter and three replicas. Replicas exit on their own when the voter ends, for any reason. |

## Limits, enforced by the program itself

| Limit | Option | How it is enforced |
|---|---|---|
| Memory | `--max-memory-mb 128` (default) | Address-space limit set by each process on itself (Linux `RLIMIT_AS`). The kernel refuses allocations above it. |
| CPU time | `--max-cpu-seconds S` | CPU-time limit set by each process on itself (`RLIMIT_CPU`). |
| CPU share | `--cpu-percent 5` | The voter sleeps after each step so that work takes at most this share of wall time. Measured in tests at 4.7% for a 5% setting. |
| Run time | `--max-seconds S` | The voter stops cleanly when the time is reached. |
| Unresponsive replica | `--reply-timeout-ms 2000` (default) | A replica that does not answer in time is treated as lost and killed. |

Measured footprint in CI (Linux ARM64, 20,000 steps): about 1.5 MB resident
memory per process.

## How to stop it

- **SIGTERM or SIGINT**: the voter finishes the current step, ends the
  replicas, prints its summary and exits with status 0.
- **SIGKILL** on the voter, if ever needed: the replicas see their input close
  and exit by themselves.
- **Maximum run time** (`--max-seconds`): the run ends on its own.

## Suggested command

```sh
./quorum --policy shrink --ticks 100000000 --seed 1 \
  --cpu-percent 5 --max-memory-mb 128 --max-seconds 86400 \
  --fault stuck:1@200000 --fault kill:0@600000 \
  --log quorum.csv
```

A second run with `--policy tmr` and the same faults gives the comparison. Fault
times, run length and CPU share would be agreed with the OPS-SAT team.

## What comes down

- **The summary** (four lines): results delivered, disagreements detected,
  wrong results, when the run stopped and why, footprint and limits.
- **The event log** (`quorum.csv`): one short line per event (fault injected,
  replica retired or cleared, disagreement detected). With no real upsets, a
  day-long run produces a few kilobytes.

## What success looks like

1. The shrinking quorum delivers more correct results than fixed TMR under the
   same injected faults, as on the ground.
2. Every injected fault is detected and handled as expected (stuck replica
   retired, lost replica dropped, mode stepped down).
3. Any real radiation upsets are logged with their time, for comparison with
   PRETTY's dosimetry data.
4. No limit is ever exceeded, and the platform is never affected.
