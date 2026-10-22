# Changelog

All notable changes to Mru Flight. Versions follow [Semantic Versioning](https://semver.org).
Release packages are on the [releases page](https://github.com/mruspace/flight/releases).

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
