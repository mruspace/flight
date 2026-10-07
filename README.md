<p align="center">
  <a href="https://mru.space">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="https://mru.space/assets/readme/mru-github-dark.gif">
      <img src="https://mru.space/assets/readme/mru-github-light.gif" alt="Mru" width="120" height="120">
    </picture>
  </a>
</p>

# Mru Flight

[![ci](https://github.com/mruspace/flight/actions/workflows/ci.yml/badge.svg)](https://github.com/mruspace/flight/actions/workflows/ci.yml)

Onboard software that keeps a spacecraft doing useful work as its computers
fail. Part of [Mru](https://mru.space).

Classic triple modular redundancy votes on three computers and stops when it
can no longer form a majority. Mru's **shrinking quorum** keeps going: vote on
three, compare on two, self-check on one. In 10,000 simulated missions it
delivered 1.34× the useful work of fixed triple redundancy on the same hardware,
and never less on any single mission ([dusk](https://github.com/mruspace/dusk)).

This repository is where that policy becomes flight software: a small,
verified Rust core, to be wrapped as components for NASA JPL's open-source
[F´](https://github.com/nasa/fprime) framework.

**Status (October 2026): early prototype.** The decision core and a demo on
real processes, with faults injected, work today. The F´ components come next
([design](docs/fprime-design.md)).

## What is here

| Crate | What it is |
|---|---|
| [`quorum`](quorum/src/lib.rs) | The decision core: vote, compare, self-check, and the health record that decides when a replica must be diagnosed. `no_std`, no allocation, no dependencies. Unit-tested, checked with [Kani](https://github.com/model-checking/kani), and built in CI for a bare-metal ARM Cortex-M target. |
| [`demo`](demo/src/main.rs) | The `quorum` program: three replica processes, a voter built on the core, fault injection, and a report of what was delivered and what it cost. Runs on Linux and macOS. |

### What the core guarantees

Checked by unit tests over every combination of a small value domain, and by
Kani over every possible input:

- **Never less than fixed TMR.** While fixed TMR can still run (two or more
  replicas), both policies make exactly the same decision. The shrinking
  quorum only differs after fixed TMR has stopped.
- **Fixed TMR stops below two replicas.** The shrinking quorum halts only when
  no replica is left.
- **Only agreed results are delivered.** A delivered value was produced by at
  least two replicas, or by both runs of the last replica's self-check.

### How the demo works

- **Three replica processes** run the same deterministic task (a hash over a
  4 KiB block derived from the tick).
- **The voter** asks the core for a verdict each tick and steps down as replicas
  fail. Self-check on one replica runs at half rate, because that replica
  computes everything twice.
- **Health scoring:** a replica outvoted three times within 200 ticks is
  diagnosed with a **known-answer test** (a fixed input whose result is known).
  Passing means the upsets were transient, so its strikes are cleared. Failing
  retires it.
- **Known-answer tests** also tell which side is wrong when two replicas
  disagree, and run periodically on the last replica to catch a stuck fault.
- **Fault injection** by schedule or at random: kill a replica, corrupt one
  result, or make a replica stuck. On one replica, a share of upsets
  (`--common-mode`) hits both self-check runs alike, which is how a wrong
  result can slip through.
- **Every run is reproducible** from its seed.

## Run it

```sh
cargo test --workspace         # unit tests
./scripts/scenarios.sh         # scenario tests against the real program
cargo run --release -- --policy shrink --ticks 20000 --seed 1 --upset-rate 0.001 \
  --fault stuck:1@4000 --fault kill:0@9000
```

`--log file.csv` writes every event (stuck, retired, cleared, killed, detected,
wrong). `quorum` with an unknown option prints all options. To run the proofs,
install Kani and run `cargo kani -p quorum`.

## Results

The same seed and faults for both policies: random upsets throughout,
replica 1 stuck from tick 4,000, replica 0 dead at tick 9,000.

```
quorum --policy tmr    ...  useful=8993  detected=11007 wrong=0 halted_at=never
quorum --policy shrink ...  useful=14490 detected=10    wrong=0 halted_at=never
```

Fixed TMR masks the stuck replica while it has three, but once another replica
dies it is left comparing a good replica with a stuck one, and every result is
rejected. The shrinking quorum retired the stuck replica two ticks after it
failed, then carried on with self-check on the last good one.

20,000 ticks, upset rate 0.001, seed 1:

| Scenario | Fixed TMR | Shrinking quorum |
|---|---|---|
| Random upsets only | 20,000 correct | 20,000 correct |
| Replicas die at 5,000 and 10,000 | 9,990 correct, **halts at 10,000** | **14,989** correct, still running |
| One dies at 5,000, one stuck at 10,000 | 9,990 correct, then **stuck in disagreement** | **14,988** correct; the known-answer test finds the stuck replica |

These are illustrative scenarios, not a reliability estimate. For statistics
over thousands of simulated missions, see [dusk](https://github.com/mruspace/dusk).

### The honest cost

On one replica, a stuck fault is only caught by the periodic known-answer test
(every 64 ticks, `--kat-period`). With replicas lost at 5,000 and 8,000 and the
last one stuck at 10,000, the demo delivered 24 wrong results before the test
caught it and the system stopped. Fixed TMR would have stopped long before, at
the second loss, and delivered none. A shorter test period trades throughput
for a smaller window. This is the trade the whitepaper and dusk quantify: more
useful work, at the cost of a small number of unchecked results late in life.

The scenario tests also caught a real flaw during development: health scoring
first retired a replica as soon as it was outvoted three times, so random upsets
could retire a healthy one, leaving the shrinking quorum with less hardware than
fixed TMR. Diagnosing with a known-answer test before retiring fixed it, and
`scripts/scenarios.sh` now checks over ten seeds that the shrinking quorum never
delivers fewer correct results than fixed TMR.

### Footprint

20,000 results, measured on an Apple silicon machine (arm64):

| | Measured |
|---|---|
| Voter | about 1.9 MB resident memory |
| Each replica | about 1.8 MB resident memory |
| CPU | about 0.3 s in total |
| Binary | about 0.5 MB |

CI prints the same measurements on Linux x86_64 and Linux ARM64 on every
change. The planned OPS-SAT experiment targets under 5% CPU, under 128 MB memory
and under 50 MB storage.

## Roadmap

1. **F´ components**: `RedundancyManager`, `HealthRegistry`, `ReplicaHost` and a
   test-only `FaultInjector`, as thin C++ shells around the Rust core
   ([design](docs/fprime-design.md)). `PowerAccountant` and `ScrubScheduler`
   follow.
2. **Measurements on representative processors**, cross-compiled for ARM Linux.
3. **Hardware-in-the-loop bench**: real boards with injected faults (bit flips,
   resets, power cuts).
4. **In orbit**: an experiment proposed for ESA's
   [OPS-SAT Space Lab](https://opssat.esa.int/missions) (OPS-SAT PRETTY),
   logging real radiation upsets next to the satellite's own dosimetry.
5. **On Earth**: the same software on an unattended ocean node, proposed for an
   EMSO ERIC observatory test.

## Related

- Whitepaper: W. Binns, *Mru: A Fault-Tolerant Operating System for
  Thousand-Year Autonomous Operation*, 2026.
  [doi:10.5281/zenodo.20579438](https://doi.org/10.5281/zenodo.20579438)
- Simulator: [dusk](https://github.com/mruspace/dusk), interactive at
  [dusk.mru.space](https://dusk.mru.space)
- Website: [mru.space](https://mru.space)

To cite this work, see [CITATION.cff](CITATION.cff).

## Questions or contributions

Email [contact@mru.space](mailto:contact@mru.space).

## Licence

Code under [Apache License 2.0](./LICENSE). The **Mru** name and mark are
trademarks; see [TRADEMARK.md](./TRADEMARK.md).
