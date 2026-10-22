# F´ components: requirements

Status: **approved on 11 October 2026**, with decisions D1 to D7 as
recommended. After a comparison with the whitepaper, three additions were
approved the same day: spares and standby (MRU-RM-018 to 021), reset
survival (MRU-RM-022, which replaces D8), and Kani proofs on the C interface
(MRU-FFI-005). MRU-SYS-001 was changed as the
[design](fprime-design-fpp.md#change-to-mru-sys-001) proposed. F´'s
development process
([nasa/fprime](https://github.com/nasa/fprime/blob/devel/docs/user-manual/overview/development-practice.md))
gates each phase: requirements are approved before the FPP design, and the
design is approved before any implementation. This file is the first gate.
The architecture is in [fprime-design.md](fprime-design.md).

Every decision stays in the `quorum` crate. The components move data, keep
state that F´ needs (ports, commands, events, telemetry, parameters), and
call the crate through a C interface. No vote, compare or self-check logic is
written again in C++.

## Decisions to confirm

Each has a recommended answer. The requirements below assume it.

| # | Decision | Recommended | Why |
|---|---|---|---|
| D1 | Where replicas run first | One deployment, three `ReplicaHost` threads | Step 1 of fprime-design.md. Fastest way to validate the logic in F´. Three processes and separate processors follow with the same components. |
| D2 | What drives a cycle | An F´ rate group calls `RedundancyManager` once per cycle | Deadline-driven work, as in F´ reference deployments. One result per cycle, as one tick in the demo. |
| D3 | When a replica that does not answer is absent | A reply that has not arrived by the next cycle is absent for its cycle. Three missed cycles in a row mark the replica lost. Changed on 22 October 2026 by MRU-RM-023: a cycle that closes late does not count as a miss. | Bounded and deterministic. The demo uses a reply timeout; in F´ the cycle is the clock. The change: on one processor the manager and the replicas share the clock, so a late manager or a starved processor looked like three dead replicas (found in CI, 22 October 2026). |
| D4 | Health as its own component | Yes: `HealthRegistry`, passive, called by `RedundancyManager` | As in fprime-design.md. It complements F´'s `Svc.Health`, which finds silent components but not wrong answers. |
| D5 | Strike limits | Fixed in the `quorum` crate (3 outvotes in 200 cycles), reported, not changeable at run time | They are compile-time constants in the core today. `KAT_PERIOD` is an F´ parameter. |
| D6 | Payload | A result is one 64-bit value per cycle. The reference payload is the demo's task (a hash over a 4 KiB block). A mission replaces it. | Keeps the manager independent of the payload. |
| D7 | Known answer | The payload gives the known-answer input and its expected result at start-up | The manager cannot know the payload. |
| D8 | Reset survival in F´ | ~~Not in this first version~~ Replaced by MRU-RM-022 | The whitepaper's transit is years of hibernation with periodic wakes (§13), and every real board reboots. |

## C interface to the core: `quorum-ffi`

| ID | Requirement | Verification |
|---|---|---|
| MRU-FFI-001 | A static library shall expose `mode`, `decide`, `Health::strike` and `Health::clear` of the `quorum` crate as C functions. | Unit test in C, built in CI |
| MRU-FFI-002 | Only plain values shall cross the interface: integers, fixed-size arrays and structs of them. No Rust type, pointer to Rust memory or allocation. | Inspection |
| MRU-FFI-003 | The C header shall match the Rust definitions. A test shall check sizes and values of every type and constant. | Unit test |
| MRU-FFI-004 | The library shall add no decision logic. Each function shall call the crate and convert its inputs and outputs. | Inspection |
| MRU-FFI-005 | Kani proofs shall show that each function gives the crate's answer for every valid input, and the safe answer (Halted, no change, false) for every out-of-range input. | Kani, in CI |

## `RedundancyManager` (active)

| ID | Requirement | Verification |
|---|---|---|
| MRU-RM-001 | Each cycle, the component shall request one result from every live replica. With one live replica under the shrinking quorum, the request shall ask for two results of the same computation. | Unit test |
| MRU-RM-002 | Each cycle, the component shall decide the previous cycle with `decide()` from the replies received for it. A reply not received by then shall count as absent, except in a cycle that closes late (MRU-RM-023). | Unit test |
| MRU-RM-003 | The component shall send a result downstream only when the verdict is Deliver, and with the delivered value. | Unit test |
| MRU-RM-004 | The component shall run in the mode `mode()` gives for the policy and the number of live replicas, and emit an event when the mode changes. | Unit test |
| MRU-RM-005 | On a caught disagreement, the component shall deliver nothing for that cycle, count it, and emit a throttled event. | Unit test |
| MRU-RM-006 | The component shall pass each outvote to `HealthRegistry`. When `HealthRegistry` asks for a diagnosis, the component shall request a known-answer test from that replica. | Unit test |
| MRU-RM-007 | When two replicas disagree under the shrinking quorum, the component shall request a known-answer test from both. | Unit test |
| MRU-RM-008 | With one live replica under the shrinking quorum, the component shall request a known-answer test every `KAT_PERIOD` cycles. | Unit test |
| MRU-RM-009 | A replica whose known-answer result is wrong, or missing by the next cycle, shall be retired. A replica that passes shall have its strikes cleared. A result is missing only when a cycle closes on time without it (MRU-RM-023). | Unit test |
| MRU-RM-010 | A replica that misses three cycles in a row shall be marked lost and retired. A cycle that closes late is not a miss, and a reply that arrives after its cycle closed clears the replica's misses (MRU-RM-023). | Unit test |
| MRU-RM-011 | Each retirement shall emit an event with the replica and the reason: failed known-answer test, no known-answer result, or lost. A retired replica shall be disabled and receive no further requests. | Unit test |
| MRU-RM-012 | When the mode is Halted, the component shall stop requesting results and emit one event, until a replica is admitted (MRU-RM-020). | Unit test |
| MRU-RM-013 | A `SET_POLICY` command shall choose the shrinking quorum or fixed TMR. | Unit test |
| MRU-RM-014 | The component shall report as telemetry: mode, live replicas, results delivered, disagreements caught, replicas retired, cycles withheld (MRU-RM-023). | Unit test |
| MRU-RM-015 | `KAT_PERIOD` shall be an F´ parameter, default 64 cycles. | Unit test |
| MRU-RM-016 | The component shall make no decision itself: votes, comparisons, self-checks and modes come from the `quorum` crate. | Inspection |
| MRU-RM-017 | The component shall allocate no memory after initialization. | Inspection |
| MRU-RM-018 | Each replica slot shall be ACTIVE, STANDBY or RETIRED. Only ACTIVE slots shall receive requests and count as live. | Unit test |
| MRU-RM-019 | A `STANDBY_REPLICA` command shall disable an ACTIVE slot and set it to STANDBY, to save power (whitepaper §4.5, §8). The command shall be refused if the policy could not run on the slots left. | Unit test |
| MRU-RM-020 | An `ADMIT_REPLICA` command on a STANDBY or RETIRED slot shall enable it and request a known-answer test. If the result is right by the next cycle, the slot shall become ACTIVE with no strikes. If not, it shall be disabled and keep its state. A RETIRED slot admitted this way holds new hardware: a spare or salvaged processor (whitepaper §4.5, §8). | Unit test |
| MRU-RM-021 | The component shall report the state of each slot as telemetry. | Unit test |
| MRU-RM-022 | The component shall save the slot states, the policy and the cycle counter to a state file when any of them changes, and every `StateSavePeriod` cycles (a parameter, default 1000; 0 saves on change only). It shall replace the file atomically. At start, it shall restore them from a valid file and emit an event, or start with all slots ACTIVE and emit a warning. Strikes are not saved. | Unit test |
| MRU-RM-023 | A cycle closes late when other messages still wait in the component's queue as it closes (the component fell behind), or when every one of two or more expected answers is missing (the processor they share fell behind). In a cycle that closes late, a missing answer shall not count against a replica: no miss, and no known-answer test or admission failed for a missing answer. If an expected answer is missing, the cycle shall be withheld: nothing delivered, no strike, counted as telemetry, with a throttled warning event. A reply that arrives after its cycle closed shall clear that replica's count of misses. Added on 22 October 2026. | Unit test; a load test of the deployment, run by hand on 22 October 2026 (no halt under CPU starvation) |

## `HealthRegistry` (passive)

| ID | Requirement | Verification |
|---|---|---|
| MRU-HR-001 | The component shall record an outvote of a replica at a cycle, and return whether that replica must now be diagnosed, as `Health::strike` decides. | Unit test |
| MRU-HR-002 | The component shall clear a replica's strikes on request. | Unit test |
| MRU-HR-003 | The component shall report each replica's strike count as telemetry. | Unit test |

## `ReplicaHost` (active, one per replica)

| ID | Requirement | Verification |
|---|---|---|
| MRU-RH-001 | On a request for a cycle, the component shall compute the payload for that cycle and return the result, tagged with the cycle. | Unit test |
| MRU-RH-002 | When the request asks for two results, the component shall compute twice and return both. | Unit test |
| MRU-RH-003 | On a known-answer request, the component shall compute the known-answer input and return the result. | Unit test |
| MRU-RH-004 | After retirement, the component shall not answer. | Unit test |

## `FaultInjector` (passive, test builds only)

| ID | Requirement | Verification |
|---|---|---|
| MRU-FI-001 | Commands shall inject, on a chosen replica and cycle: kill, hang, stuck and corrupt, as the demo's `--fault` option does. | Unit test |
| MRU-FI-002 | A command shall set a random upset rate and seed, as the demo's `--upset-rate` and `--seed` do. | Unit test |
| MRU-FI-003 | The component shall not be part of a flight build. | Inspection of the build |

## `RunRecorder` (passive, experiment builds only)

Added on 22 October 2026 for the OPS-SAT deployment (run 3). It stands in for the ground: the manager never sees it.

| ID | Requirement | Verification |
|---|---|---|
| MRU-RR-001 | The component shall check each delivered result against the payload computed for its cycle without a fault, count correct and wrong results, and report both as telemetry. | Unit test |
| MRU-RR-002 | A wrong result shall emit a throttled warning event with its cycle. | Unit test |
| MRU-RR-003 | The component shall give the run's counts with the manager's last policy, mode, live replicas, caught count and slot states, read from the telemetry database. | Unit test |
| MRU-RR-004 | The component shall record the end of the command sequence and whether every command in it succeeded. | Unit test |
| MRU-RR-005 | The component shall not be part of a flight build. | Inspection of the build |
| MRU-RR-006 | The component shall call a function the deployment gives it with the number of each cycle, at the start of the cycle, before the manager's work for it. | Unit test |

## Deployment `MruOpsSat`

Added on 22 October 2026: the first F´ deployment of the components, for OPS-SAT run 3.

| ID | Requirement | Verification |
|---|---|---|
| MRU-DEP-001 | The deployment shall run the four components with F´'s command dispatcher, event manager, telemetry database, health pings and command sequencer, driven by a Linux timer. | Integration test (`scripts/fprime-deployment.sh`) |
| MRU-DEP-002 | The policy and the faults shall be commanded by an F´ command sequence that ends before cycle 1. | Integration test |
| MRU-DEP-003 | The deployment shall stop at a time cap or on SIGTERM and write a summary of the run. | Integration test |
| MRU-DEP-004 | A static 32-bit ARM Linux build shall run in ESA's OPS-SAT experiment environment as user `exp`, within 128 MB of address space and the registered 5% CPU share. | Full-length run in ESA's environment image |
| MRU-DEP-005 | Version 2 (`MruQuorum` and `MruReplica`, added 22 October 2026) shall run each replica as a process of its own, linked to the manager process through F´'s GenericHub over a Unix socket pair that keeps message boundaries (no network, no framing). | Integration test (`VERSION=2 scripts/fprime-deployment.sh`) |
| MRU-DEP-006 | Each replica process shall pin itself to a configured core and report the core it runs on. | Integration test; full-length run in ESA's environment image |
| MRU-DEP-007 | Version 2 shall apply a kill or a hang as a real signal to a replica process (SIGKILL, SIGSTOP) at the start of a configured cycle, and the summary shall report how each replica process ended. | Integration test |

## System requirement

| ID | Requirement | Verification |
|---|---|---|
| MRU-SYS-001 | On the schedules of the bench (`scripts/bench.sh`), the F´ deployment shall give the same correct, caught and wrong counts as the demo for the same seed, except that each kill or hang fault may cost up to three correct results. | Integration test |

## Not in this version

Compared with Layer 1 in the whitepaper (§8):

| Whitepaper | Status after this version |
|---|---|
| Shrinking quorum: vote, compare, self-check | Built (core and components) |
| Dual redundancy **with checkpointing**: on a caught disagreement with two replicas, roll back and recompute the cycle | Not built. A retry rule belongs in the core, with its own Kani proofs. Today the cycle is lost. |
| Health registry scoring error rate, response time and gate use; gradual deprioritization; donors | Strikes only. Response time is measurable from reply arrival and comes next. Gate use needs FPGA hardware. |
| Spares from salvaged hardware; deprioritized nodes | Slots can be admitted and put on standby (MRU-RM-018 to 020). Choosing when is for the hardware inventory and the power accountant, not built. |
| Power accountant | Not built. STANDBY_REPLICA is the hook it will use. |
| Memory scrubbing by tier | Not built (`ScrubScheduler`). |
| Duty cycling and hibernation | The components count in cycles, not time, so gaps between wakes do not break the strike window or the lost rule. State survives resets (MRU-RM-022). The scheduler is not built. |
| Layer 0 | Not built. All decisions stay in the `no_std` core, so they can move under Layer 0 unchanged. |

Also not in this version: three processes (D1 step 2), separate processors
(D1 step 3).
