# Changelog

All notable changes to Mru Flight. Versions follow [Semantic Versioning](https://semver.org).
Release packages are on the [releases page](https://github.com/mruspace/flight/releases).

## Unreleased

### Added

- Mru Flight ran in orbit. `docs/opssat.md` has the results of experiment exp4051 on ESA's OPS-SAT PRETTY, 18 October 2026 (227 seconds on the SEPP, both policies exit 0, every fault on its tick, 2,745 correct results for the shrinking quorum against 239 for fixed TMR, zero wrong, no real upset), after the two ground runs of 15 and 16 October. `opssat/results/` holds the files that came down from all three runs: `run-info.txt`, the summaries, the event logs, the console output, and the journal lines for the two runs on the SEPP. The flown build is release v0.3.1 (tag `v0.3.1`, package `exp4051-mru-flight-v0.3.1.tar.gz`, binary sha256 `4124b05b911f6a9a26ef0a352dac0a6c04e884a48c2c18a7522ee861aa4138f1`). README: a new "In orbit" section; the status, roadmap, bench notes, `SECURITY.md`, `CONTRIBUTING.md`, `docs/board.md` and `docs/qualification.md` say what has run where. Scope, stated everywhere: three software replicas on one processor, in user space, faults from a schedule; not qualification evidence.

### Changed

- `docs/opssat.md`: status is "ran in orbit on 18 October 2026"; it has ESA's measured results from 15 October 2026 and TU Graz's FlatSat results from 16 October 2026 (the flight processor: about 45 ticks per second at the 5% share), and the prediction for the orbit run as stated to ESA; the dosimeter comparison is gone (PRETTY's dosimeter data is not available to experimenters). README roadmap item 4 says the same. `.gitignore`: `opssat/toGround/`.

### Fixed

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
