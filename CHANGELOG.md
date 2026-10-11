# Changelog

All notable changes to Mru Flight. Versions follow [Semantic Versioning](https://semver.org).
Release packages are on the [releases page](https://github.com/mruspace/flight/releases).

## Unreleased

### Added

- Demo: `stuckbit` fault, a bit of the input, the working block, the hash accumulator or the result forced to 0 or 1 from a chosen tick.
- Demo: `kat_tests` count at the end of the second summary line.
- `scripts/kat-coverage.sh`: a known-answer test coverage campaign on the last replica, with results in `bench/results/*-kat.{csv,json}`.
- `scripts/check-claims.sh`: fails when a headline number in the README differs from the bench results files.
- `docs/qualification.md`: what exists today, mapped to NASA and ECSS software standards. No compliance claim.
- CHANGELOG.md, CONTRIBUTING.md and SECURITY.md.

### Fixed

- Demo: a `corrupt` fault scheduled on an odd tick in self-check mode did nothing. It now hits the next computation.

### Changed

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
