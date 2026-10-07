<p align="center">
  <a href="https://mru.space">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="https://mru.space/assets/readme/mru-github-dark.gif">
      <img src="https://mru.space/assets/readme/mru-github-light.gif" alt="Mru" width="120" height="120">
    </picture>
  </a>
</p>

# Mru Flight

Onboard software that keeps a spacecraft doing useful work as its computers
fail. Part of [Mru](https://mru.space).

Classic triple modular redundancy votes on three computers and stops when it
can no longer form a majority. Mru's **shrinking quorum** keeps going: vote on
three, compare on two, self-check on one. In 10,000 simulated missions it
delivered 1.34× the useful work of fixed triple redundancy on the same hardware,
and never less on any single mission ([dusk](https://github.com/mruspace/dusk)).

This repository is where that policy becomes flight software: reusable
components for NASA JPL's open-source [F´](https://github.com/nasa/fprime)
framework.

**Status (October 2026): early prototype.** The `prototype/` directory holds a
small standalone program that shows the policy working on real processes, with
faults injected. The F´ components come next (see [Roadmap](#roadmap)).

## The prototype

`prototype/quorum.cpp` is about 400 lines of C++17 with no dependencies. It
runs on Linux and macOS.

- **Three replica processes** run the same deterministic task (a hash over a
  4 KiB block derived from the tick).
- **A voter** compares their outputs and steps down as replicas fail:
  - `shrink`: vote on three, compare on two, self-check on one (at half rate,
    because the one replica computes everything twice).
  - `tmr`: vote on three, compare on two, stop on one (classic fixed TMR).
- **Health scoring** retires a replica that is outvoted three times within 200
  ticks.
- **Known-answer tests** run a replica on an input whose result is known. They
  tell which side is wrong when two replicas disagree, and they catch a replica
  that is stuck but self-consistent.
- **Fault injection** by schedule or at random: kill a replica, corrupt one
  result, or make a replica stuck. Random upsets hit each replica independently.
  On one replica, a share of upsets (`--common-mode`) hits both self-check runs
  alike, which is how a wrong result can slip through.
- **Every run is reproducible** from its seed, and reports correct results,
  detected errors, undetected (wrong) results, when it halted, and its own
  footprint.

### Run it

```sh
cd prototype
make demo
```

`make demo` runs both policies with the same seed and faults: random upsets
throughout, replica 1 stuck from tick 4,000, replica 0 dead at tick 9,000.

```
./quorum --policy tmr --ticks 20000 --seed 1 --upset-rate 0.001 --fault stuck:1@4000 --fault kill:0@9000
useful=8993 detected=11007 wrong=0 halted_at=never alive_at_end=2

./quorum --policy shrink --ticks 20000 --seed 1 --upset-rate 0.001 --fault stuck:1@4000 --fault kill:0@9000
useful=14490 detected=10 wrong=0 halted_at=never alive_at_end=1
```

Fixed TMR masks the stuck replica while it has three, but once another replica
dies it is left comparing a good replica with a stuck one, and every result is
rejected. The shrinking quorum retired the stuck replica two ticks after it
failed (health scoring), then carried on with self-check on the last good one.

`--log file.csv` writes every event (stuck, retired, killed, detected, wrong).
`./quorum` with no arguments prints all options.

### More scenarios

20,000 ticks, upset rate 0.001, seed 1:

| Scenario | Fixed TMR | Shrinking quorum |
|---|---|---|
| Random upsets only | 20,000 correct | 20,000 correct |
| Replicas die at 5,000 and 10,000 | 9,990 correct, **halts at 10,000** | **14,989** correct, still running |
| One dies at 5,000, one stuck at 10,000 | 9,990 correct, then **stuck in disagreement** | **14,988** correct; the known-answer test finds the stuck replica |

These are illustrative scenarios, not a reliability estimate. For the
statistics over thousands of simulated missions, see
[dusk](https://github.com/mruspace/dusk).

### The honest cost

On one replica, a stuck fault is only caught by the periodic known-answer test
(every 64 ticks). With replicas lost at 5,000 and 8,000 and the last one stuck at
10,000, the prototype delivered 24 wrong results before the test caught it and
the system stopped. Fixed TMR would have stopped long before, at the second
loss, and delivered none. Shortening the test period trades throughput for a
smaller window. This is the trade the whitepaper and dusk quantify: more useful
work, at the cost of a small number of unchecked results late in life.

### Footprint

Measured on an Apple silicon development machine (arm64), 20,000 results:

| | Measured |
|---|---|
| Voter | about 1.8 MB resident memory |
| Each replica | about 1.2 MB resident memory |
| CPU | about 0.3 s in total |

Linux and target-processor numbers will follow. The planned OPS-SAT experiment
targets under 5% CPU, under 128 MB memory and under 50 MB storage.

## Roadmap

1. **F´ components.** Port the policy into reusable components:
   `RedundancyManager` (the shrinking quorum), `HealthRegistry` (scoring and
   known-answer tests), `PowerAccountant` and `ScrubScheduler`.
2. **Linux and ARM measurements** of the footprint on representative processors.
3. **Hardware-in-the-loop bench**: real boards with injected faults (bit flips,
   resets, power cuts).
4. **In orbit**: an experiment proposed for ESA's
   [OPS-SAT Space Lab](https://opssat.esa.int/missions) (OPS-SAT PRETTY), logging
   real radiation upsets next to the satellite's own dosimetry.
5. **On Earth**: the same software on an unattended ocean node, proposed for an
   EMSO ERIC observatory test.

## Related

- Whitepaper: W. Binns, *Mru: A Fault-Tolerant Operating System for
  Thousand-Year Autonomous Operation*, 2026.
  [doi:10.5281/zenodo.20579438](https://doi.org/10.5281/zenodo.20579438)
- Simulator: [dusk](https://github.com/mruspace/dusk), interactive at
  [dusk.mru.space](https://dusk.mru.space)
- Website: [mru.space](https://mru.space)

## Questions or contributions

Email [contact@mru.space](mailto:contact@mru.space).

## Licence

Code under [Apache License 2.0](./LICENSE). The **Mru** name and mark are
trademarks; see [TRADEMARK.md](./TRADEMARK.md).
