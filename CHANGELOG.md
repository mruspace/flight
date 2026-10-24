# Changelog

All notable changes to Mru Flight. Versions follow [Semantic Versioning](https://semver.org).
Release packages are on the [releases page](https://github.com/mruspace/flight/releases).

## Unreleased

### Added

- Run 2 ran in orbit on 22 October 2026 (v0.4.0, the package ESA validated): segment 1 shrink 698 correct, 0 wrong, halted at tick 1,281 after the known-answer test caught the stuck bit on the last replica; segment 2 fixed TMR 119, then detected; segment 3 killed at tick 900, resumed once from its checkpoint, 1,750 correct, 0 wrong. Matches the prediction. Result files in `opssat/results/2026-10-22-ops-sat-pretty-on-board-run2`, with the run 2 FlatSat files in `opssat/results/2026-10-21-tu-graz-flatsat-run2`. `docs/opssat.md` has the results; README, board, qualification, security and contributing notes say two runs.

## 0.7.0 (2026-10-22)

### Added

- Run 3, version 2: each replica in a process of its own. `MruQuorum`, the manager process (RedundancyManager, HealthRegistry, FaultInjector, RunRecorder and F´'s CdhCore services), starts three `MruReplica` processes, one ReplicaHost each, linked through F´'s GenericHub. Requirements MRU-DEP-005 to 007.
- `SeqPacketDriver`: a byte-stream driver over one end of a `SOCK_SEQPACKET` Unix socket pair, which keeps message boundaries, so the hub needs no framing and the link no network. After it opens, it turns off reopening and ends its reconnect thread, so a send to a dead replica fails at once.
- Each replica process pins itself to a core (`-C`, default 0,1,1) and reports it. The kill is real: `RunRecorder` calls the deployment at the start of each cycle (MRU-RR-006, one unit test, 45 in all) and the manager sends SIGKILL to replica 0's process at cycle 240; the summary reports how each replica process ended and the experiment's CPU share.
- `opssat/fprime2/`: the sequences, `run`, `build.sh` and `package.sh` for version 2 (`exp4051-mru-flight-fprime-processes-VERSION.tar.gz`); the release adds `mru-fprime-processes-VERSION-armv7-linux-static.tar.gz`. `VERSION=2 scripts/fprime-deployment.sh` runs it in CI. Full length in ESA's image: 148 s, the same counts and events as version 1, replica 0 killed by SIGKILL at cycle 240, three processes pinned in every segment, 19.9 MB of address space, the experiment's CPU 2.9% to 3.75% under emulation.

### Changed

- The payload (the demo's hash over a 4 KiB block) is the module `Mru/Payload`, shared by ReplicaHost and RunRecorder; ReplicaHost keeps `compute` and `expectedKnownAnswer`.

## 0.6.0 (2026-10-22)

### Fixed

- `RedundancyManager`: a manager that fell behind retired healthy replicas as lost. On one processor, when the manager's thread is held up (a slow write of the state file, a starved processor), the next cycle's tick waits in its queue in front of the replies, so each cycle closed before its answers were dispatched; three such cycles retired every replica and halted the run. Seen once in CI in the deployment's restart test (22 October 2026) and reproduced on every try under CPU starvation. New requirement MRU-RM-023: a cycle closes late when other messages wait in the queue as it closes, or when every one of two or more expected answers is missing; in such a cycle a missing answer counts against no replica (no miss, no known-answer test or admission failed), and a cycle with an answer missing is withheld (nothing delivered, no strike). A reply that arrives after its cycle closed clears the replica's misses. New event `CycleWithheld` and telemetry `Late`. Under CPU starvation, 15 runs of the deployment halted 9 times before and never after; replicas lost without cause fell from 28 to 14; no wrong result either way. Four unit tests (44 in all); MRU-RM-002, 009, 010, 014 and D3 updated; the parity test is unchanged (36 of 60 identical, wrong results identical in all 60).
- `RunRecorder` reads `Late`; the `MruOpsSat` summary reports `late=`. `scripts/fprime-deployment.sh` allows one count of slack per withheld cycle, so load on a CI runner cannot fail the comparison with the demo.

## 0.5.1 (2026-10-22)

### Changed

- `MruOpsSat`: the summary has a `cpu:` line that splits the process's CPU time into start-up (process start to the first timer cycle), the timed run and the end (draining the queues, teardown), with user and kernel time apart and the run's own share of wall time. On the run 2 FlatSat test the demo's voter showed 5.6% and 5.8% against its 5% setting, and one total per process could not say where the extra time went; the run 3 package now carries the numbers that would. `scripts/fprime-deployment.sh` checks the line is there.

## 0.5.0 (2026-10-22)

### Added

- `MruOpsSat`, the first F´ deployment of the components (`fprime/MruOpsSat`): `RedundancyManager`, `HealthRegistry`, three `ReplicaHost` threads and `FaultInjector`, with F´'s command dispatcher, event manager, telemetry database, health pings and command sequencer (`CdhCore`), driven by a Linux timer. No communications stack; events as text lines, a summary from `Main.cpp` (counts, footprint, address space). Requirements MRU-DEP-001 to 004.
- `RunRecorder`, an experiment-only F´ component: checks every delivered result against the payload computed without a fault, counts correct and wrong, and reads the manager's last telemetry for the summary (MRU-RR-001 to 005, three unit tests).
- The OPS-SAT run 3 package (`opssat/fprime/`): `run` with three segments (the run 1 faults under the shrinking quorum, the same under fixed TMR, a SIGKILL and a restore from the manager's state file), the faults and the policy as F´ command sequences (`shrink.seq`, `tmr.seq`, `restart.seq`), `build.sh` and `package.sh`. Full length in ESA's image: 147 s, 0 wrong in every segment, counts against the demo as MRU-SYS-001 allows, CPU 1.3% to 1.6%, 19 MB of address space. `docs/opssat.md` has the numbers.
- A build container (`fprime/toolchain/Dockerfile`, `scripts/fprime-docker.sh`) and a static, optimized 32-bit ARM toolchain (`fprime/cmake/toolchain/armv7-linux-static.cmake`); the F´ CMake project builds the Rust core for the toolchain's target.
- CI: `scripts/fprime-deployment.sh` runs the run 3 package against a native build and checks each segment against the demo, the events on their cycles, the restore and a second run in the same folder. The release builds the ARM deployment with its sequences (`mru-fprime-VERSION-armv7-linux-static.tar.gz`, in `SHA256SUMS`).

## 0.4.1 (2026-10-22)

### Fixed

- `opssat/run`: the restart segment resumed from a checkpoint left in the home folder by an earlier run of the package. On the PRETTY FlatSat on 21 October 2026 the folder still held `toGround/checkpoint-restart.txt` from an earlier run, so segment 3 logged three `resumed` lines (`resumes=3`) instead of one. The segment now removes an old checkpoint before its first voter starts, says so in `run-info.txt`, and starts from tick 0. Scenario check 29 runs the package twice in the same folder.
- Demo: a run of identical events still open when the voter exits now gets its closing count line, and a resume closes a run that was open at the checkpoint before it writes the `resumed` line. In the FlatSat log the stop line restored from the old checkpoint swallowed the next `stopped,time-limit` into a count that was never written. Scenario check 30.

## 0.4.0 (2026-10-20)

### Added

- The OPS-SAT run 2 package. `opssat/run` now has three segments: the shrinking quorum on the lifecycle schedule (`opssat/faults.lifecycle.txt`: a sensor self-test, a masked bit flip, a hung replica, a stuck replica, then a stuck bit in the last replica's result register on a known-answer test tick, so the quorum goes from three to two to one and halts by itself with no wrong result), fixed TMR on the same schedule, and a restart segment (`opssat/faults.restart.txt`: a replica retired by health scoring, a checkpoint every 100 ticks, the voter killed with SIGKILL after 30 seconds by the script itself, copied checkpoint, the voter started again and resumed). Caps 80, 30 and 30 plus 30 seconds. `opssat/package.sh` packages both schedules (six files) and describes the segments in `README.txt`. Scenario tests 27 (the `outvoted` line) and 28 (the entry point end to end). `docs/opssat.md` describes the run 2 package; run 1 stays as flown.
- Demo: an `outvoted` event in the log when a replica loses a vote and stays. In the first orbit run the masked bit flip at tick 60 left no line (noted to ESA on 16 October 2026). A stuck replica's run of outvotes collapses to one line plus its count, as other repeated events do.
- Mru Flight ran in orbit. `docs/opssat.md` has the results of experiment exp4051 on ESA's OPS-SAT PRETTY, 18 October 2026 (227 seconds on the SEPP, both policies exit 0, every fault on its tick, 2,745 correct results for the shrinking quorum against 239 for fixed TMR, zero wrong, no real upset), after the two ground runs of 15 and 16 October. `opssat/results/` holds the files that came down from all three runs: `run-info.txt`, the summaries, the event logs, the console output, and the journal lines for the two runs on the SEPP. The flown build is release v0.3.1 (tag `v0.3.1`, package `exp4051-mru-flight-v0.3.1.tar.gz`, binary sha256 `4124b05b911f6a9a26ef0a352dac0a6c04e884a48c2c18a7522ee861aa4138f1`). README: a new "In orbit" section; the status, roadmap, bench notes, `SECURITY.md`, `CONTRIBUTING.md`, `docs/board.md` and `docs/qualification.md` say what has run where. Scope, stated everywhere: three software replicas on one processor, in user space, faults from a schedule; not qualification evidence.

### Changed

- Demo: the fill of the sensor block at start-up is throttled under `--cpu-percent`, one 1 MiB chunk at a time, so the whole process stays under its share. Run 1 measured 5.2% voter CPU against a 5% cap on every host; the fill ran outside the throttle. The clock for `--max-seconds` and for the summary's wall time now starts before the replicas are spawned and the sensor is filled, so a cap is the whole life of the process. Costs a few seconds of wall time per start at a 5% share.
- `docs/opssat.md`: status is "ran in orbit on 18 October 2026"; it has ESA's measured results from 15 October 2026 and TU Graz's FlatSat results from 16 October 2026 (the flight processor: about 45 ticks per second at the 5% share), and the prediction for the orbit run as stated to ESA; the dosimeter comparison is gone (PRETTY's dosimeter data is not available to experimenters). README roadmap item 4 says the same. `.gitignore`: `opssat/toGround/`.

### Fixed

- Demo: the CPU-share throttle counted the wait for a replica that never answered as work. At a 5% share, the tick that lost a hung replica after the 2 s reply timeout was followed by a 38 s nap. The wait is now left out of the nap (scenario 28). Found while planning the OPS-SAT run 2 schedule, which has a hung replica; no flown run had one.
- Demo: `--max-seconds` was checked once per tick, so a tick that started a 64 MB sensor scan just under the cap ran the whole scan, and both runs in orbit overran the 110 s cap by about 3 s. The scan now looks at the deadline every 1 MiB and stops there (the run ends with `stopped,time-limit` on that tick, the scan is not counted), and the CPU-share sleep is cut at the deadline. Scenario 26 checks it.
- Demo: after a reset, the event log could hold an event twice. The voter flushes the log before it writes the checkpoint, so a reset between the two left events in the log that the resumed run wrote again (seen once in CI under emulation on 16 October 2026: two `killed,0` lines). The checkpoint now records the log's length and the open run of identical events; a resume cuts the log back to that length and continues the run's count. Scenario test 18 plants a stale line past the checkpoint and checks that it is gone.
- `opssat/package.sh`: on macOS, tar added one hidden `._NAME` metadata entry per file to the package, and the macOS tar listing hid them, so the check after packaging did not show them. ESA's software environment test of `exp4051-mru-flight-v0.3.1.tar.gz` (16 October 2026) found them; they did not affect the run. The script now sets `COPYFILE_DISABLE=1`, passes `--no-mac-metadata` to bsdtar, and lists the archive with Python's `tarfile`, which fails the build on any hidden entry, a wrong file list or an owner other than `exp`. The package ESA tested is unchanged.

## 0.3.1 (2026-10-15)

### Changed

- `opssat/run`: the voter's CPU share defaults to 5% of one core, as stated in the OPS-SAT registration, instead of 25%; the sensor scans every 250 ticks instead of every 1,000. `opssat/faults.5min.txt`: faults at ticks 20, 60, 120 and 240 instead of 50, 150, 300 and 600, so they land inside a five-minute window at the lower share on a slow processor.

## 0.3.0 (2026-10-15)

### Added

- `opssat/run`: foreground entry point for the OPS-SAT experiment runner. Runs the shrinking quorum and fixed TMR one after the other with the same seed and fault schedule, writes everything to `toGround/`, stops by itself inside a five-minute window, and forwards SIGTERM to the voter.
- `opssat/faults.5min.txt`: a fault schedule that fits a run of five minutes or less.
- `opssat/package.sh`: builds the OPS-SAT package (tar.gz, relative paths, files owned by user `exp`) from a checksum-verified release binary or a local build.
- Demo: `stuckbit` fault, a bit of the input, the working block, the hash accumulator or the result forced to 0 or 1 from a chosen tick.
- Demo: `kat_tests` count at the end of the second summary line.
- `scripts/kat-coverage.sh`: a known-answer test coverage campaign on the last replica, with results in `bench/results/*-kat.{csv,json}`.
- `scripts/check-claims.sh`: fails when a headline number in the README differs from the bench results files.
- `docs/qualification.md`: what exists today, mapped to NASA and ECSS software standards. No compliance claim.
- CHANGELOG.md, CONTRIBUTING.md and SECURITY.md.

### Fixed

- Demo: the event log writes a run of identical events on consecutive ticks (for example fixed TMR detecting a disagreement on every tick once it is left with one good and one stuck replica) as the first line plus one closing line with the count. A 110-second run that wrote 52,000 lines now writes two.
- Demo: a `corrupt` fault scheduled on an odd tick in self-check mode did nothing. It now hits the next computation.

### Changed

- Demo: `cpu=` and `wall=` print 3 decimals.
- `scripts/bench.sh` and `scripts/kat-coverage.sh` share `scripts/bench-meta.sh`; `scripts/board.sh --kat` runs the coverage campaign on a board.
- README: separates what Kani proves (per decision) from what tests check (per run), and adds limits, open questions and the qualification path.

## 0.2.0 (2026-10-11)

- Bench: the shrinking quorum against fixed TMR on the same injected faults, 300 runs per host (#4).
- Board bench: checkpoint and resume after resets and power cuts, `scripts/board.sh`, static x86_64 and bench binaries (#5).
- `quorum-ffi`: a C interface to the core, with Kani proofs that it gives the core's answer (#6).
- Four F´ v4.4.1 components: RedundancyManager, HealthRegistry, ReplicaHost and the test-only FaultInjector, with unit tests and a system test against the demo (#6).

## 0.1.1 (2026-10-07)

- Version in Cargo.toml and CITATION.cff; the release workflow stops if the tag does not match (#2).
- README chart from two real runs; `--progress-every` adds running totals to the log (#1).

## 0.1.0 (2026-10-07)

- `quorum`: a `no_std` decision core with Kani proofs.
- Demo: three replica processes, a voter, fault injection, enforced resource limits, a radiation sensor mode and a fault schedule file.
- Scenario tests, CI on Linux x86_64, Linux ARM64 and macOS, static ARM builds tested under emulation, and release packages.
