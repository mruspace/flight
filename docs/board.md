# Bench on a board

How to run the Layer 1 bench on a target-class board, with resets and power
cuts. Today the bench runs on general-purpose processors (see
[docs.mru.space/core/bench](https://docs.mru.space/core/bench/)). The same
bench on a target-class board, with its results published, moves Layer 1 from
Prototype to Bench-tested on the
[architecture status page](https://docs.mru.space/concepts/architecture/).

No board run has been done yet. This note is the procedure.

## Which board

A board with the same class of processor as a flight payload computer, running
Linux. For reference, ESA's OPS-SAT carried a payload processor built on an
Altera Cyclone V SoC: a dual-core ARM Cortex-A9 at 800 MHz with an FPGA,
running Linux ([ESA](https://www.esa.int/Enabling_Support/Operations/OPS-SAT)).
A Cyclone V SoC development board that runs Linux is the closest match, and
its FPGA also serves later Layer 0 work.

Any ARM Cortex-A board that runs Linux can run the bench. Use the static binary
that matches it:

| Processor | Binary |
|---|---|
| 32-bit ARM with hardware floating point (Cortex-A9, Cortex-A7) | `armv7-unknown-linux-musleabihf` |
| 32-bit ARM without it | `armv7-unknown-linux-musleabi` |
| 64-bit ARM (Cortex-A53, Cortex-A72) | `aarch64-unknown-linux-musl` |
| x86_64 | `x86_64-unknown-linux-musl` |

Each tagged release has a package per target with `quorum` and `bench`. Every
CI run also uploads them as artifacts.

## What the board needs

- Linux, reachable over SSH with a key.
- `sh`, `awk`, `sed` and `pgrep` (BusyBox is enough).
- Persistent storage for the checkpoint (an SD card or eMMC, not a RAM disk).
- For resets: passwordless `sudo`, so the script can trigger an immediate
  reboot with sysrq.
- For power cuts: a relay or smart plug that a command on your machine can
  switch off and on.

## Run it

From a clone of this repository, on a machine that can reach the board:

```sh
# scenario checks, then the full matrix (300 runs) on the board
CORE_BIN=path/to/bench scripts/board.sh board path/to/quorum

# 5 resets during one run: immediate reboots without sync
scripts/board.sh board path/to/quorum --resets 5

# 5 power cuts: your command switches the board's power off and on
RESET_CMD='plug off; sleep 5; plug on' scripts/board.sh board path/to/quorum --resets 5
```

Results come back to `bench/results/`: the bench JSON and CSV, the scenario
output, and for resets a text file with each reset and the final comparison.

## How resets are checked

The demo saves its state to a checkpoint file every 100 ticks
(`--checkpoint`, `--checkpoint-every`). It writes a new file, flushes it to
storage and renames it over the old one, so a power cut leaves either the old
checkpoint or the new one. After a reset, the same command resumes from the
checkpoint. Dead, stuck and hung replicas stay that way, because that is
hardware state. The ticks after the last checkpoint run again, and the run is
deterministic, so they give the same results.

The reset test first runs the same options without resets. It passes only if
the run with resets ends with the same counts: correct, detected and wrong
results, the halt, the last correct result and the detection line. The scenario
checks test the same thing on every change, with `kill -9` in place of a
reboot.

## What the board results show, and what they do not

- They show the shrinking quorum's decisions, recovery from resets and power
  cuts, timing and footprint on target-class processors.
- The three replicas still share one processor. Separate processors, one per
  replica, are the next step of the bench.
- It is not a radiation test. Upsets are still injected. A beam test (for
  example RADNEXT) or flight (OPS-SAT) gives real upsets.
