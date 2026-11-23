# Fault model: what the runs simulate, and how

Mru never sees hardware. It sees answers. Each replica runs the same task
and sends back a result each cycle. Mru votes on the results, compares them,
or has one replica compute twice and check itself. It also asks a replica,
now and then, for the answer to a fixed input whose result it knows (a
known-answer test).

So a hardware fault reaches Mru only as a **symptom at the interface**: a
wrong value, the same wrong value every time, a late answer, or no answer at
all. Most of the runs reproduce those symptoms in software. They do not
reproduce the physics that cause them.

Run 4's last segment goes one step closer to the cause. Each replica
process flips **real bits in its own memory**, one at a time at random
times: its code, data, heap and stacks. Nothing chooses the symptom.
Whatever the flip does (nothing, a wrong result, a crash, a hang, a
malformed message) is what Mru gets. See [Run 4, segment 6](#run-4-segment-6-real-bit-flips).

On OPS-SAT PRETTY, the three replicas run on one processor with two cores:
as processes (runs 1 to 3, and run 4's F´ segments) or as cFE child tasks,
threads of one process (run 4's cFS segment). They are not separate
hardware. A fault that hits the shared processor hits every replica at once;
see [Not modeled](#not-modeled). Runs 1 and 2 used the `quorum` program,
which injects the same faults through its `--fault` option; runs 3 and 4 use
the F´ components and `FaultInjector`, named in the table; run 4's cFS
segment applies them in the Mru cFS app, between the app and its replica
tasks.

## The table

| Failure on a real system | Symptom Mru sees | How the run produces it | What Mru does | Runs |
|---|---|---|---|---|
| A single-event upset flips a bit during a computation | One unit gives one wrong result, once | `FaultInjector`: a bit flip in that replica's working memory for one cycle (`INJECT CORRUPT`), or random flips at a set rate (`SET_UPSETS`). In the cFS segment: `corrupt` in `/cf/mru.cfg` | Three units: outvoted, the right value delivered. Two: the disagreement is caught and nothing is delivered for that cycle. One: its two computations differ and nothing is delivered. Nothing wrong is delivered. | 1, 2, 3; 4 (cFS segment) |
| A unit is permanently damaged: a stuck output or a stuck bit | The same wrong value every cycle, the known-answer test included | `FaultInjector`: the replica's replies are replaced by a fixed value (`INJECT STUCK`). In the cFS segment: `stuck` | Outvoted, then tested after three outvotes. The known-answer test fails and the unit is retired. On the last unit, Mru stops rather than deliver a wrong result. | 1, 2, 3; 4 (cFS segment) |
| A unit dies: a crash, or a part that stops for good | No answer, ever | A real kill: the manager sends SIGKILL to the replica's process at the start of a cycle (`-k kill:N@C`). Or `FaultInjector` drops its requests (`INJECT KILL`). In the cFS segment, the app sends the replica task no more requests (`hang`, which no reset ends there) | Lost after three cycles without an answer, retired, and the others carry on. With resets on (MRU-RM-024): reset, tested, and kept out after the last reset allowed, because a dead unit fails every test. | 1, 2, 3; 4 with resets and in the cFS segment |
| A device on a bus stops answering, and a reset brings it back | No answer, until a reset | `FaultInjector` drops the replica's requests (`INJECT HANG`). A reset from the manager ends the hang, as a platform reset ends it in a device (MRU-FI-004) | Lost after three cycles without an answer. With resets on: Mru resets it, tests it with a known answer, and takes it back only if the test passes. At most `ResetLimit` resets per unit, so a unit that keeps failing cannot cause a reset loop. | 4 |
| A process freezes without dying | No answer | A real SIGSTOP to the replica's process (`-k stop:N@C`) | As for a unit that stops answering | Supported, not used in a run yet |
| Power cut or reboot in the middle of work | The manager disappears, then starts again | The manager process is killed with SIGKILL and started again with new replica processes | It restores the policy, the slot states and the cycle count from its state file, and carries on with the failed units still out. A corrupted state file is caught by its checksum, and Mru starts fresh. | 2, 3 |
| Sensors of one quantity disagree: noise, a spike, a slow drift, a frozen sensor | Readings that differ, slightly or by a lot | `SensorSim`: three readings of a known value, with noise, and `SENSOR_FAULT` SPIKE, DRIFT, STUCK or DROP | `SensorVoter` chooses one value within a tolerance: the median of three, the midpoint of two. A reading beyond the tolerance is an outlier and is not chosen; a sensor that is an outlier five agreed cycles in a row is excluded, so a short spike is forgiven. With two readings left that disagree, nothing is delivered. | 4 (sensor segment) |
| A particle flips a bit anywhere in a unit's memory: its code, its data, its stack | Whatever the flip causes: usually nothing; sometimes a wrong result, a crash, a hang, a garbled message | Real: a thread in each replica process flips random bits in that process's own memory, at random times (`MRU_FLIPS`, `fprime/MruReplica/BitFlips.hpp`). The cause is injected; the symptom is not chosen | As for each symptom. A message that is not a well-formed reply, or one too many in a cycle, is dropped on the link (MRU-DEP-008), so the replica looks silent and is lost. | 4 (segment 6) |
| The processor is overloaded, or the manager falls behind | Answers arrive late | Real: found on loaded CI runners, not injected | The cycle is withheld and no unit is blamed (MRU-RM-023). A unit that answers late is alive, not lost. | CI; withheld once on the FlatSat in run 3 |

Faults are commanded the way an operator commands anything in F´: by command
sequences (`INJECT`, `SET_UPSETS`) compiled against the deployment's
dictionary, or by the deployment's `-k` option for real signals. The
sequences for each run are in `opssat/` (for example
[`opssat/run4/reset.seq`](../opssat/run4/reset.seq) for run 4). In the cFS
segment, the entry script writes the schedule to `/cf/mru.cfg` before cFS
starts, and copies it to the output (`cfs-mru.cfg`). Every event lands in the
run's event file with its cycle.

## Run 4 (flown November 20, 2026)

The OPS-SAT team approved run 4 on November 16, 2026. The package is the
v0.9.0 release asset `exp4051-mru-flight-v0.9.0.tar.gz`. It ran on TU Graz's
FlatSat and in orbit on OPS-SAT PRETTY on November 20, 2026, with 0 wrong
results in every segment (results below and in
[opssat.md](opssat.md#run-4-v090-flown-november-20-2026)). Segments 1 to 5
use simulated faults, as described here; segment 6 uses real bit flips made
by software.

Run 4 shows the second half of the bus-device row, recovery, and the sensor
row, and runs the same decision core under cFS. Segments 1 to 3 run the same
faults, 40 seconds each, at 100 ms per cycle:

- Cycle 60: replica 1 stops answering (a hang).
- Cycle 150: replica 2's process is killed with SIGKILL, a real dead unit.
- Cycle 300: replica 1 hangs again.

| Segment | What it shows |
|---|---|
| Shrinking quorum, resets on (`ResetLimit` 3) | Replica 1 is reset and taken back, both times. Replica 2 is reset three times, fails every known-answer test, and is kept out. |
| Shrinking quorum, resets off | Both units stay out; replica 0 goes on alone in self-check. |
| Fixed TMR, no resets | One unit left after the kill; fixed TMR stops. |

Segment 4, also 40 seconds, runs three simulated sensors next to the
replicas: a spike on sensor 1 at cycle 60 (three cycles), a drift on sensor 2
from cycle 120, and sensor 0 frozen from cycle 260. Tolerance 2.00, noise up
to 0.50, a sensor excluded after five outlier cycles in a row. A full-length
native run in the build container (not the SEPP), 399 cycles: 279 correct
values, 0 wrong, and 120 cycles with no value, after the two readings left
disagreed from cycle 279. The spike was masked and sensor 1 kept; sensor 2
was excluded at cycle 137. A plain average of the same readings was wrong in
261 of the 399 cycles.

Segment 5 runs the same decision core as an application of NASA's core
Flight System (cFS v7.0.1), with the faults of run 1: a flip at cycle 60, a
stuck replica at 120, a hung one at 240 ([`cfs`](../cfs)). The cFS app has
the core's decisions and the rules of the F´ manager around them (strikes,
known-answer tests, loss after three silent cycles, self-check on the last
replica), but not the automatic resets (MRU-RM-024) or the sensor voter:
those are F´ only. In ESA's image: 400 cycles, 318 correct, 3 caught, 0
wrong; the stuck replica 1 retired on its known answer at cycle 122, the hung
replica 0 lost at cycle 242, replica 2 alone in self-check from cycle 243; no
socket open.

On November 13, 2026, the five-segment package ran at full length in ESA's
OPS-SAT PRETTY environment image (32-bit ARM, emulated on a laptop), as user
`exp`: 205 seconds, each exit 0, 0 wrong results in every segment, and
nothing left behind by the cFS segment. With segment 6 (November 16, 2026),
three full runs: 246 seconds, each exit 0, 0 wrong results in every
segment.

On the SEPP, on the FlatSat and in orbit on November 20, 2026, every segment
ended as above. Segment 1: 394 correct, 3 caught, 0 wrong; replica 1 back at
cycles 63 and 303 in orbit (304 on the FlatSat), replica 2 kept out at 159.
Segment 2: 271 correct, replica 0 alone in self-check from cycle 154.
Segment 3: fixed TMR halted at cycle 153 (148 correct in orbit, 149 on the
FlatSat). Segment 4: 279 correct values, 0 wrong, 120 with no value; sensor
2 excluded at cycle 137; a plain average wrong in 261 cycles. Segment 5: the
cFS application, 318 correct, 3 caught, 0 wrong. Where an event moved by one
cycle, a cycle had closed late on the processor and the manager withheld it
(MRU-RM-023).

## Run 4, segment 6: real bit flips

Segments 1 to 5 inject each fault at its symptom. Segment 6 injects at the
cause. A thread in each replica process flips one bit at a time in that
process's own memory, as a particle would: it picks a byte at random from
the process's code, read-only data, data, heap and stacks, in proportion to
their size, on a page that is in memory, and flips one bit of it. The flips
come at random times, 2 per second per replica on average, from 2 seconds
after start (after the links are up). Code pages are the process's private
copies: the package's files are never changed. Each replica logs every flip
before it makes it (`flips-bitflip-rN.txt`: number, time, kind of memory,
address, bit), so a flip that crashes the process is still on record.

The manager runs the shrinking quorum, with no resets (a reset in these runs
cannot reload a process) and a known-answer test on the last replica every
cycle. It is told nothing about the flips.

What the flips do cannot be scheduled: it depends on where they land, and
the memory layout on the SEPP differs from the test host. In ESA's
environment image (emulated, 32-bit ARM) and in 160 native runs at up to 200
flips per second, the outcomes were: most flips changed nothing; some
crashed a replica (segmentation fault, illegal instruction, stack smashing
detected) or hung it, and it was lost; some made a replica answer wrong, and
the disagreement was caught and the replica retired; when the last replica
went, Mru halted and delivered nothing. **No run delivered a wrong
result.**

The flips found one real flaw before flight. A corrupted replica sent a
reply too short to decode, and F´'s generated port code in the manager
asserted on it and ended the manager process (1 of 64 native runs at 200
flips per second from start-up). Now the manager checks every message from a
replica on its link before the hub sees it: a message that is not exactly
one well-formed reply, or one more than four in a cycle, is dropped and
counted (MRU-DEP-008, `links: dropped=` in the summary). A replica that
sends only garbage is then silent to the manager, and is lost. In 160 runs
after the fix, the manager never crashed, and it dropped malformed messages
in several. In one of them all three replicas died in the first 50 ms,
before their links were up, and the manager declined to start (exit 1,
nothing delivered); in the package the flips start at 2 seconds.

A replica that a flip sets spinning stops at 2 seconds of CPU time
(`RLIMIT_CPU`); a replica that writes without end stops at 256 KB
(`RLIMIT_FSIZE`); no process of the run writes a core file.

Limits: the flips are made by software, in user space, one bit at a time. A
real particle can also flip a bit in a register, a cache, the kernel, or the
shared processor itself, and can flip several bits at once. None of those is
modeled here.

On the SEPP, on the FlatSat and in orbit on November 20, 2026, the flips
came out the same on both hosts: the seed fixes their times and offsets, and
only the addresses of mapped memory moved. 173 flips in all (64, 68 and 41
per replica), 149 of them in code, none refused. Replica 2 crashed
(segmentation fault) after 41 flips and was lost at cycle 229. Replicas 0
and 1 delivered all 399 results to the end, 0 caught, 0 wrong, and the
manager dropped no message on its links. Replicas 0 and 1 then crashed at
shutdown, after the last result: flips had corrupted memory that only the
exit code read. Files:
[`opssat/results`](../opssat/results).

Package and scripts: [`opssat/run4`](../opssat/run4). The CI check is
[`scripts/opssat-run4.sh`](../scripts/opssat-run4.sh).

## Not modeled

- **Causes.** Radiation physics, latch-up, thermal and power faults are not
  simulated. Only their symptoms at the interface are, except for the bit
  flips of run 4, segment 6, which are real flips in a replica process's
  memory, made by software.
- **Common-mode faults.** On PRETTY all replicas share one processor, one
  clock and one power supply. A fault there hits every replica at once. Mru
  then withholds the cycle and blames no unit (MRU-RM-023), but it cannot
  recover from it, and no run injects it.
- **Recovery that needs new hardware or new firmware.** Switching to a
  redundant board, rerouting memory, or loading a new bootloader is work
  for the ground or for the platform. Mru's part is to keep delivering
  checked results with the units left, and to take a unit back only after it
  passes a known-answer test.
- **The reset itself.** In the runs, a reset ends a simulated hang. On a
  real system, `resetOut` would drive the platform's reset line for that
  unit. Mru decides when to reset and whether to take the unit back; it does
  not reset hardware itself.
- **Input agreement for the replicas.** `SensorVoter` chooses one value from
  sensors of one quantity. The replicas' vote still needs exact agreement: a
  step that makes every replica's input equal before it computes is not
  built (see "Input agreement" in the [README](../README.md#input-agreement)).
- **Real sensors.** The readings are simulated, with a known true value.
  On PRETTY, no sensor data is used.
