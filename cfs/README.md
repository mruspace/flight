# Mru as a cFS application

Mru's decision core, the `quorum` crate, run as an application of NASA's core
Flight System (cFS). Built for OPS-SAT run 4, segment 5
([`opssat/fprime3`](../opssat/fprime3)): built and tested on the ground, not
flown.

## What is here

| Path | What it is |
|---|---|
| [`cfe`](cfe), [`osal`](osal), [`psp`](psp) | NASA's cFE, OSAL and PSP, git submodules pinned to the commits of the cFS bundle v7.0.1. Apache License 2.0, unchanged. |
| [`tools/elf2cfetbl`](tools/elf2cfetbl) | NASA's table tool, a submodule at the bundle's commit. cFE's mission build requires it; the app uses no tables. |
| [`apps/mru`](apps/mru) | The Mru app (C99): three replicas as cFE child tasks, the payload of the F´ deployment, faults from a schedule (`/cf/mru.cfg`), and every decision from the core through [`quorum.h`](../quorum-ffi/include/quorum.h). |
| [`mru_defs`](mru_defs) | The mission configuration: one processor, the app linked statically into `core-cpu1`, OSAL in permissive mode, a static ARM toolchain for OPS-SAT PRETTY's processor. |
| [`build.sh`](build.sh) | Builds `core-cpu1` in the F´ build container: static 32-bit ARM by default, native with `NATIVE=1`. |

No sample or lab application is built: nothing opens a network socket.

## How it runs

Each cycle the app asks each live replica for a result through an OSAL
queue, collects the answers until half the cycle has passed, and calls
`quorum_decide`: vote on three, compare on two, self-check on one. The rules
around the core are the F´ `RedundancyManager`'s: three outvotes within the
strike window ask for a known-answer test, a wrong or missing known answer
retires the replica, three silent cycles lose it, the last replica
self-checks every other cycle with a periodic known-answer test, and a cycle
with every answer missing is withheld. Before cycle 1 every replica gives
its known answer.

Faults are applied between the app and the replicas, as F´'s
`FaultInjector` does: `corrupt` (a bit flip in one computation), `stuck`
(a fixed wrong value from a cycle on) and `hang` (no requests from a cycle
on). Each delivered result is checked against the payload computed without
a fault. After the configured cycles the app prints `MRU SUMMARY:` and ends
cFE with a power-on reset, so the PSP deletes its shared memory.

## Running on a Linux system as a normal user

- OSAL's permissive mode creates a message queue deeper than the kernel
  allows a normal user (10 messages) at the allowed depth, and skips
  real-time priorities that need root. As root in a container without
  `CAP_SYS_RESOURCE`, OSAL does not cap the depth and cFE cannot create its
  pipes: run it as a normal user.
- The PSP uses three System V shared memory segments (about 1.3 MB) and
  writes `EEPROM.DAT` (512 KB) and three empty key files in its working
  folder; OSAL makes a folder `/dev/shm/osal:RAM`. The power-on reset deletes
  the segments. The OPS-SAT entry script runs it in a folder of its own and
  deletes the rest afterward.
- OSAL v7.0.1 does not link with its network layer turned off, so the layer
  is built; nothing calls it.
- glibc reserves a 64 MB arena per thread on 64-bit Linux. With
  `MALLOC_ARENA_MAX=1` the process stays at 9.0 MB of address space natively
  under an enforced 128 MB limit (measured November 13, 2026).

## Measured

On November 13, 2026, the run 4 package ran at full length in ESA's OPS-SAT
PRETTY environment image (32-bit ARM, emulated on a laptop), as user `exp`.
Segment 5, 40 seconds at 100 ms: 400 cycles, 318 correct results, 3
disagreements caught, 0 wrong; the stuck replica 1 retired on its known
answer at cycle 122, the hung replica 0 lost at cycle 242, replica 2 alone in
self-check from cycle 243; no socket open; exit 0; nothing left behind. The
same counts as a native run. CPU under emulation is not a figure for the
SEPP and is not given here.
