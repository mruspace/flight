# F´ components: requirements

Status: **approved on 11 October 2026**, with decisions D1 to D8 as
recommended. A change to MRU-SYS-001 is proposed in the
[design](fprime-design-fpp.md#change-to-mru-sys-001-for-approval). F´'s
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
| D3 | When a replica that does not answer is absent | A reply that has not arrived by the next cycle is absent for its cycle. Three missed cycles in a row mark the replica lost. | Bounded and deterministic. The demo uses a reply timeout; in F´ the cycle is the clock. |
| D4 | Health as its own component | Yes: `HealthRegistry`, passive, called by `RedundancyManager` | As in fprime-design.md. It complements F´'s `Svc.Health`, which finds silent components but not wrong answers. |
| D5 | Strike limits | Fixed in the `quorum` crate (3 outvotes in 200 cycles), reported, not changeable at run time | They are compile-time constants in the core today. `KAT_PERIOD` is an F´ parameter. |
| D6 | Payload | A result is one 64-bit value per cycle. The reference payload is the demo's task (a hash over a 4 KiB block). A mission replaces it. | Keeps the manager independent of the payload. |
| D7 | Known answer | The payload gives the known-answer input and its expected result at start-up | The manager cannot know the payload. |
| D8 | Reset survival in F´ | Not in this first version | The demo's checkpoint covers the bench. F´ state across resets uses F´'s own parameter and file services and comes later. |

## C interface to the core: `quorum-ffi`

| ID | Requirement | Verification |
|---|---|---|
| MRU-FFI-001 | A static library shall expose `mode`, `decide`, `Health::strike` and `Health::clear` of the `quorum` crate as C functions. | Unit test in C, built in CI |
| MRU-FFI-002 | Only plain values shall cross the interface: integers, fixed-size arrays and structs of them. No Rust type, pointer to Rust memory or allocation. | Inspection |
| MRU-FFI-003 | The C header shall match the Rust definitions. A test shall check sizes and values of every type and constant. | Unit test |
| MRU-FFI-004 | The library shall add no decision logic. Each function shall call the crate and convert its inputs and outputs. | Inspection |

## `RedundancyManager` (active)

| ID | Requirement | Verification |
|---|---|---|
| MRU-RM-001 | Each cycle, the component shall request one result from every live replica. With one live replica under the shrinking quorum, the request shall ask for two results of the same computation. | Unit test |
| MRU-RM-002 | Each cycle, the component shall decide the previous cycle with `decide()` from the replies received for it. A reply not received by then shall count as absent. | Unit test |
| MRU-RM-003 | The component shall send a result downstream only when the verdict is Deliver, and with the delivered value. | Unit test |
| MRU-RM-004 | The component shall run in the mode `mode()` gives for the policy and the number of live replicas, and emit an event when the mode changes. | Unit test |
| MRU-RM-005 | On a caught disagreement, the component shall deliver nothing for that cycle, count it, and emit a throttled event. | Unit test |
| MRU-RM-006 | The component shall pass each outvote to `HealthRegistry`. When `HealthRegistry` asks for a diagnosis, the component shall request a known-answer test from that replica. | Unit test |
| MRU-RM-007 | When two replicas disagree under the shrinking quorum, the component shall request a known-answer test from both. | Unit test |
| MRU-RM-008 | With one live replica under the shrinking quorum, the component shall request a known-answer test every `KAT_PERIOD` cycles. | Unit test |
| MRU-RM-009 | A replica whose known-answer result is wrong, or missing by the next cycle, shall be retired. A replica that passes shall have its strikes cleared. | Unit test |
| MRU-RM-010 | A replica that misses three cycles in a row shall be marked lost and retired. | Unit test |
| MRU-RM-011 | Each retirement shall emit an event with the replica and the reason: failed known-answer test, no known-answer result, or lost. A retired replica shall receive no further requests. | Unit test |
| MRU-RM-012 | When the mode is Halted, the component shall stop requesting results and emit one event. | Unit test |
| MRU-RM-013 | A `SET_POLICY` command shall choose the shrinking quorum or fixed TMR. | Unit test |
| MRU-RM-014 | The component shall report as telemetry: mode, live replicas, results delivered, disagreements caught, replicas retired. | Unit test |
| MRU-RM-015 | `KAT_PERIOD` shall be an F´ parameter, default 64 cycles. | Unit test |
| MRU-RM-016 | The component shall make no decision itself: votes, comparisons, self-checks and modes come from the `quorum` crate. | Inspection |
| MRU-RM-017 | The component shall allocate no memory after initialization. | Inspection |

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

## System requirement

| ID | Requirement | Verification |
|---|---|---|
| MRU-SYS-001 | On the schedules of the bench (`scripts/bench.sh`), the F´ deployment shall give the same correct, caught and wrong counts as the demo for the same seed. (Change proposed in the design: each kill or hang fault may cost up to three correct results.) | Integration test |

## Not in this version

`PowerAccountant`, `ScrubScheduler`, reset survival (D8), three processes
(D1 step 2), separate processors (D1 step 3).
