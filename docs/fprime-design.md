# From prototype to F´ components

How the `quorum` crate and the demo map onto NASA JPL's
[F´](https://github.com/nasa/fprime) flight software framework. This is the
plan for the next step; nothing in this document is built yet.

## Principle: a verified Rust core inside thin F´ components

The decisions (vote, compare, self-check, when to diagnose a replica) live in
the `quorum` crate: `no_std`, no allocation, no dependencies, checked with Kani.
F´ components are C++. Each Mru component is therefore a thin C++ shell that
handles F´ ports, commands, events and telemetry, and calls the Rust core
through a small C interface:

```
F´ component (C++, FPP model)  ──calls──▶  quorum (Rust, no_std, Kani-checked)
  ports, commands, events,                   decide(), mode(), Health
  telemetry, parameters                      exported with extern "C"
```

The Rust crate is built as a static library for the target and linked into the
F´ deployment by CMake. The C interface exchanges only plain values (replies,
verdicts, strike results), so no Rust types cross into C++ and no allocation is
involved.

## Components

| Component | F´ kind | Role | Prototype today |
|---|---|---|---|
| `RedundancyManager` | Active | Collects replica results each cycle, asks the core for a verdict, delivers or rejects the result, and steps the mode down as replicas are lost. | `decide()` and `mode()` in `quorum`; the voter loop in `demo` |
| `HealthRegistry` | Passive | Keeps strikes per replica, triggers known-answer tests, clears or retires replicas. Complements F´'s ping-based `Svc.Health`, which detects silent components but not wrong answers. | `Health` in `quorum`; known-answer tests in `demo` |
| `ReplicaHost` | Active (one per replica) | Runs the payload task for one replica and answers known-answer tests. | the `--replica` process in `demo` |
| `FaultInjector` | Passive, test builds only | Applies scheduled or random faults on command: stop a replica, corrupt a result, make a replica stuck. Used on the ground bench and for the OPS-SAT experiment. | fault options in `demo` |
| `PowerAccountant` | Passive | Every subsystem asks before it spends energy; budgets shrink as power declines. | not started |
| `ScrubScheduler` | Active | Memory scrubbing by priority tier. | not started |

## Interfaces of `RedundancyManager`

| F´ element | Name | Meaning |
|---|---|---|
| Input ports | `replicaResult[3]` (async) | One result, or a self-check pair, per replica per cycle |
| Output port | `result` | The delivered result, only when the verdict allows it |
| Output port | `replicaControl[3]` | Retire, or request a known-answer test |
| Command | `SET_POLICY` | `SHRINK` or `TMR`, for side-by-side runs |
| Event | `MODE_CHANGED` | VOTE → COMPARE → SELF_CHECK → HALTED |
| Event | `REPLICA_RETIRED` | Which replica, and why (stuck, failed known-answer test, lost) |
| Event | `DISAGREEMENT_DETECTED` | A result was rejected this cycle |
| Telemetry | `Mode`, `AliveReplicas`, `Delivered`, `Detected` | Counters for the ground |
| Parameters | `STRIKES`, `STRIKE_WINDOW`, `KAT_PERIOD` | Today constants and options in the prototype |

## Where replicas run

1. **One deployment, three threads.** Three `ReplicaHost` instances in one
   deployment. Fastest way to validate the logic in F´, with no fault isolation
   between replicas.
2. **Three processes.** One deployment per replica, linked through F´'s generic
   hub over local sockets. Matches the demo, and an OPS-SAT experiment can run
   this way on one Linux processor.
3. **Separate processors.** The same deployments on separate boards of the
   hardware-in-the-loop bench, then on flight hardware with independent
   processors.

## What carries over unchanged

- The `quorum` crate, as it is: the same code is unit-tested, Kani-checked,
  built for a bare-metal ARM target in CI, and linked into F´.
- The scenario tests: the same faults and seeds, driven through
  `FaultInjector` commands instead of command-line options.
- The measurements: delivered, detected and wrong results, recovery time and
  footprint.
