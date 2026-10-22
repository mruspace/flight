# F´ components: design (draft for review)

Status: **approved on 11 October 2026**, with the change to MRU-SYS-001 and
three additions from a comparison with the whitepaper: spares and standby,
reset survival and Kani proofs on the C interface. This is the second gate of
F´'s development process; the [requirements](fprime-requirements.md) are the
first.

The models are in [`fprime/Mru`](../fprime/Mru). They compile with F´ v4.4.1
(`fprime-util build` builds the generated base classes of all four
components).

## Layout

```
fprime/                    F´ project (settings.ini, CMakeLists.txt)
  lib/fprime               F´ v4.4.1, git submodule
  Mru/Types/Types.fpp      Replicas = 3, Policy, Mode, RequestKind, RetireReason,
                           Upset, FaultKind, StrikeCounts, SlotState, SlotStates,
                           RefuseReason, AdmitFailure
  Mru/Ports/Ports.fpp      ReplicaRequest, ReplicaReply, ReplicaEnable, Result,
                           HealthStrike, HealthClear
  Mru/Components/          RedundancyManager, HealthRegistry, ReplicaHost,
                           FaultInjector
quorum-ffi/                Rust static library with the C interface (new crate)
```

## Components

| Component | Kind | Why this kind |
|---|---|---|
| `RedundancyManager` | active | Replies arrive from three replica threads. One queue and one thread order them with the cycle, so the decision needs no locks. |
| `HealthRegistry` | passive, guarded ports | Called synchronously by the manager. Guarded so telemetry and calls cannot interleave. |
| `ReplicaHost` | active, one per replica | Each replica runs on its own thread (D1). |
| `FaultInjector` | passive, guarded ports | Test builds only. Sits between the manager and the replicas. |

The full models: [RedundancyManager.fpp](../fprime/Mru/Components/RedundancyManager/RedundancyManager.fpp),
[HealthRegistry.fpp](../fprime/Mru/Components/HealthRegistry/HealthRegistry.fpp),
[ReplicaHost.fpp](../fprime/Mru/Components/ReplicaHost/ReplicaHost.fpp),
[FaultInjector.fpp](../fprime/Mru/Components/FaultInjector/FaultInjector.fpp).

### RedundancyManager

| Element | Name | Notes |
|---|---|---|
| Input | `schedIn: Svc.Sched` (async) | From a rate group, once per cycle |
| Output | `requestOut[3]: ReplicaRequest` | Cycle, kind (`COMPUTE`, `COMPUTE_TWICE`, `KNOWN_ANSWER`), upset (always none from the manager) |
| Input | `replyIn[3]: ReplicaReply` (async) | Cycle, kind, first, second |
| Output | `enableOut[3]: ReplicaEnable` | Disables a retired or standby replica, enables one being admitted |
| Output | `resultOut: Result` | Cycle and delivered value |
| Output | `healthStrike`, `healthClear` | To HealthRegistry |
| Ping | `pingIn` / `pingOut` | F´ `Svc.Health` |
| Commands | `SET_POLICY(policy)`, `STANDBY_REPLICA(slot)`, `ADMIT_REPLICA(slot)` (all async) | |
| Parameters | `KatPeriod: U32`, default 64; `StateSavePeriod: U32`, default 1000 | |
| Events | `PolicySet`, `ModeChanged`, `ReplicaStandby`, `AdmitRequested`, `ReplicaAdmitted`, `StateRestored` (activity high); `DiagnosisRequested`, `ReplicaCleared` (activity low); `DisagreementCaught` (warning low, throttled at 10), `AdmitFailed`, `SlotCommandRefused`, `StateNotRestored` (warning low); `ReplicaRetired`, `Halted`, `StateSaveFailed` (warning high, the last throttled at 5) | |
| Telemetry | `CurrentPolicy`, `CurrentMode`, `LiveReplicas`, `Retired`, `Slots` on change; `Delivered`, `Caught` each cycle | |

**Slots.** Each slot is ACTIVE, STANDBY or RETIRED. Only ACTIVE slots get
requests. `STANDBY_REPLICA` disables an ACTIVE slot, unless the policy could
not run on the slots left. `ADMIT_REPLICA` enables a STANDBY or RETIRED slot
and requests a known-answer test; the slot becomes ACTIVE only if the right
answer arrives by the next cycle. One admission runs at a time.

**State file.** The manager saves the policy, the slot states and the cycle
counter when any of them changes, and every `StateSavePeriod` cycles. It
writes a new file and renames it over the old one, as the demo's checkpoint
does, through F´'s `Os::File`. The path is set in `configure()`. At start it
restores a valid file, or starts with all slots ACTIVE. Strikes are not saved:
after a reset, a replica gets a fresh window.

### HealthRegistry

`strikeIn(replica, cycle) -> bool` and `clearIn(replica)`, guarded.
Telemetry `Strikes: [3] U32` on change. It holds a `quorum::Health` record
through the C interface.

### ReplicaHost

`requestIn` (async), `replyOut`, `enableIn: ReplicaEnable` (async), ping.
A disabled host does not answer. The
payload is the demo's task: a hash over a 4 KiB block derived from the cycle
(D6). It applies the request's upset to its working memory during the
computation, so an injected fault travels through the real calculation.

### FaultInjector

Guarded `requestIn[3]` → `requestOut[3]` and `replyIn[3]` → `replyOut[3]`.
Commands `INJECT(kind, replica, cycle)` and `SET_UPSETS(rate, commonMode,
seed)`. Events `FaultScheduled`, `FaultApplied`, `FaultRefused`,
`UpsetsSet`.

- **Kill and hang**: it drops the replica's requests from that cycle.
- **Stuck**: it replaces the replica's replies with a fixed wrong value.
- **Corrupt and random upsets**: it sets the request's upset.
- **Same upsets as the demo**: the manager sends requests on its own thread,
  replica 0, then 1, then 2. The injector draws in that order with the
  demo's generator and seed, and draws nothing for known-answer requests, as
  the demo does.

## Topologies

```
Flight:  RateGroup ─▶ RedundancyManager ─requestOut[i]─▶ ReplicaHost[i]
                         ▲      │   └──────resultOut──▶ (payload consumer)
                         │      └──healthStrike/Clear─▶ HealthRegistry
                         └────────replyIn[i]─────────── ReplicaHost[i]

Test:    RedundancyManager ─▶ FaultInjector ─▶ ReplicaHost[i]
                         ◀──  FaultInjector ◀──
```

The manager and the replicas are the same in both. Only the wiring changes,
so no test code is in the flight components.

## One cycle

As built (this refines the first draft, which decided a cycle only at the next
`schedIn`; deciding at once is what lets the counts match the demo):

1. At `schedIn` for cycle N, close cycle N−1: decide it if it is not yet
   decided, with any missing reply as `Absent`; count missed replies (three in
   a row: lost, MRU-RM-010); fail known-answer tests and admissions that had a
   full cycle to answer (MRU-RM-009, 020).
2. Start cycle N: compute the mode, request from every ACTIVE slot, and on the
   last replica add the periodic known-answer test every `KatPeriod` cycles
   (MRU-RM-008). The demo's tick does these in the same order.
3. Decide cycle N as soon as every expected answer is in, the periodic test
   included. Strikes, the diagnosis of two disagreeing replicas and the
   periodic test follow at once, as in the demo's tick. Known-answer tests
   asked for here are answered before the next cycle when the cycle is longer
   than the work.

**Self-check delivers every other cycle.** On one replica, the manager asks
for two results of the same computation on even cycles only. The replica gets
two cycles of time for the double work, and throughput is half, as in the
demo and Dusk.

**A dead replica is found by silence.** The demo kills a replica from the
voter, so the voter knows at once. In F´ a replica that dies simply stops
answering. The manager treats a missing reply as absent at once, and as lost
after three cycles in a row (D3). When the death leaves one replica, the
cycles in which the manager still asks for single results end as caught
disagreements.

**Parameters not loaded.** F´ returns 0 for a parameter that was never
loaded. The manager then uses the model's defaults (64 and 1000), so a
topology that forgets `loadParameters()` does not test every cycle.

## Results

| Check | Result |
|---|---|
| Kani, `quorum` and `quorum-ffi` | 9 of 9 proofs verified |
| C test of `quorum.h` | passes |
| F´ unit tests | 40 (HealthRegistry 5, ReplicaHost 5, FaultInjector 8, RedundancyManager 19, RunRecorder 3), with address and undefined-behavior sanitizers |
| MRU-SYS-001, 60 cases (5 schedules, 2 policies, 3 upset rates, 2 seeds, 4,000 cycles) | 36 identical to the demo. In the other 24, a kill that leaves one replica costs the shrinking quorum 2 correct results and adds 3 caught disagreements, as predicted. Wrong results are identical in all 60. |

The expected counts come from the demo itself (`scripts/parity-cases.sh`),
and CI checks that they are current.

## Deployment: `MruOpsSat`

Added on 22 October 2026, for OPS-SAT run 3 ([package](opssat.md)). The
first F´ deployment of the components, in [`fprime/MruOpsSat`](../fprime/MruOpsSat):

```
LinuxTimer (100 ms) ─▶ RateGroupDriver ─▶ fast group: RedundancyManager, RunRecorder
                                      └─▶ slow group (1 s): telemetry, commands,
                                          health pings, events, sequencer
CmdSequencer ─▶ CmdDispatcher ─▶ SET_POLICY, INJECT, SET_UPSETS, parameters
RedundancyManager ─▶ FaultInjector ─▶ ReplicaHost[3] ─▶ FaultInjector ─▶ RedundancyManager
RedundancyManager ─resultOut─▶ RunRecorder ─TlmGet─▶ telemetry database
```

- **Services**: F´'s `CdhCore` subtopology (command dispatcher, event
  manager, telemetry database, health pings, version, text logger, fatal
  handling), a command sequencer, a Linux timer and two rate groups. No
  communications stack: the target has no network, so events are printed as
  text lines and the summary is written by `Main.cpp`.
- **`RunRecorder`** (passive, experiment builds only, like `FaultInjector`):
  checks each delivered result against the payload computed without a fault,
  counts correct and wrong, and reads the manager's last telemetry from the
  database (MRU-RR-001 to 005). It is the ground truth, not part of the
  manager's loop.
- **Commands before cycle 1**: `Main.cpp` asks the sequencer to run the
  sequence, waits for it to end, then starts the timer (MRU-DEP-002).
- **Signals**: blocked in every thread and taken by one thread with
  `sigwait`, so the timer is never stopped from inside a signal handler. A
  time cap is a `SIGALRM` from `setitimer`.
- **Parameters** are not loaded from a database (none in this deployment);
  the manager uses the model's defaults, and a sequence can set a parameter
  by command (`STATESAVEPERIOD_PRM_SET`).
- **Builds**: native Linux in CI (`scripts/fprime-deployment.sh`, which
  checks the counts against the demo), and a static, optimized 32-bit ARM
  build (`fprime/cmake/toolchain/armv7-linux-static.cmake`) in the build
  container `fprime/toolchain/Dockerfile`. On 32-bit ARM the Rust core's
  static library brings its own integer-division helpers, which libgcc also
  has (`--allow-multiple-definition`), and names a personality routine that
  is never called with `panic=abort` (`MruOpsSat/RustSupport.cpp`).

## Change to MRU-SYS-001

The first MRU-SYS-001 asked for the same counts as the demo on the bench
schedules. Because of the point above, the approved text is:

> On the bench schedules, the F´ deployment shall give the same correct,
> caught and wrong counts as the demo for the same seed, except that each kill
> or hang fault may cost up to three correct results.

## C interface: `quorum-ffi`

A new crate in the workspace, built as a static library by `cargo` from a CMake
step in the F´ build (F´ v4.4.1 finds `cargo` but has no Rust build support).
Header:

```c
#include <stdbool.h>
#include <stdint.h>

#define QUORUM_REPLICAS 3

enum { QUORUM_SHRINK = 0, QUORUM_TMR = 1 };                  /* policy */
enum { QUORUM_VOTE = 0, QUORUM_COMPARE = 1,
       QUORUM_SELF_CHECK = 2, QUORUM_HALTED = 3 };           /* mode */
enum { QUORUM_ABSENT = 0, QUORUM_SINGLE = 1, QUORUM_PAIR = 2 }; /* reply */
enum { QUORUM_DELIVER = 0, QUORUM_DETECTED = 1,
       QUORUM_OUTCOME_HALTED = 2 };                           /* outcome */

typedef struct { uint8_t kind; uint64_t first; uint64_t second; } quorum_reply;

typedef struct {
    uint8_t mode;
    uint8_t outcome;
    uint64_t value;                     /* valid when outcome is DELIVER */
    bool dissent[QUORUM_REPLICAS];
    bool diagnose;
} quorum_verdict;

typedef struct {                        /* quorum::Health, owned by the caller */
    uint64_t strikes[QUORUM_REPLICAS][3];
    uint64_t count[QUORUM_REPLICAS];
} quorum_health;

uint8_t quorum_mode(uint8_t policy, uint32_t alive);
quorum_verdict quorum_decide(uint8_t policy, const quorum_reply replies[QUORUM_REPLICAS]);
void quorum_health_init(quorum_health *h);
bool quorum_health_strike(quorum_health *h, uint32_t replica, uint64_t cycle);
void quorum_health_clear(quorum_health *h, uint32_t replica);
```

The health record lives in the caller's memory (HealthRegistry), and each call
converts it with `Health::to_raw` and `Health::from_raw`. No pointer to Rust
memory crosses (MRU-FFI-002). Out-of-range values (a policy other than 0 or 1,
a replica above 2) return the safe answer: `HALTED`, or no change and `false`.

## Verification plan

| Level | What |
|---|---|
| `quorum-ffi` | Kani proofs: each function equals the core for every valid input, and gives the safe answer for every out-of-range input (MRU-FFI-005). A C test checks sizes, constants and known cases. |
| Each component | F´ unit tests (`fprime-util check`) for each requirement marked "Unit test". |
| System | A test deployment runs the bench schedules through FaultInjector and compares the counts with the demo (MRU-SYS-001). |
| CI | A new job installs F´, builds, and runs all of the above. |

## Toolchain note

On this macOS (Darwin 27), the CMake 3.26 that F´ v4.4.1 pins fails with a
recursion error, also on F´'s own sample project. CMake 4.4 works. CI on Linux
can use the pinned version.
