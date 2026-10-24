# Qualification path

**No qualification evidence exists yet. Mru Flight makes no compliance claim.**

This note lists the software standards a flight program would apply to Mru
Flight, and maps what exists today to the kinds of evidence those standards ask
for. It names each standard by number and title only. It does not cite clauses,
because nothing here has been assessed against one.

## Standards

| Agency | Standard | Title |
|---|---|---|
| NASA | NPR 7150.2 | NASA Software Engineering Requirements |
| NASA | NASA-STD-8739.8 | Software Assurance and Software Safety Standard |
| ESA (ECSS) | ECSS-E-ST-40C | Space engineering: Software |
| ESA (ECSS) | ECSS-Q-ST-80C | Space product assurance: Software product assurance |

## What exists today

| Evidence a standard asks for | What exists | Where | Status |
|---|---|---|---|
| Unit verification | Unit tests of the core over every combination of a small value domain; tests of the C interface | `quorum/src/lib.rs`, `quorum-ffi/src/lib.rs`, `quorum-ffi/tests/` | Exists. Not traced to requirements. |
| Formal verification of critical code | Nine Kani proofs: four of the core's decision function, five that the C interface gives the core's answer | `quorum/src/lib.rs`, `quorum-ffi/src/lib.rs` | Exists. Scope: one decision at a time. Does not cover Health in the core, diagnosis, timing or the system. |
| Integration and system test | Scenario checks against the real binary (`scripts/scenarios.sh`); a parity system test between the F´ components and the demo | `scripts/scenarios.sh`, `fprime/Mru/Test/Parity/` | Exists. Faults are injected in software on general-purpose processors. |
| Performance and resource measurement | Bench of 300 runs per host on three host types; known-answer test coverage campaign | `scripts/bench.sh`, `scripts/kat-coverage.sh`, `bench/results/` | Exists. General-purpose processors, not flight hardware. |
| Test on the target processor | The OPS-SAT packages (the demo, releases v0.3.1 and v0.4.0) on the SEPP, the payload processor of ESA's OPS-SAT PRETTY: on the FlatSat on 16 and 21 October 2026 and in orbit on 18 and 22 October 2026 | `opssat/results/`, `docs/opssat.md` | Exists. Two runs in orbit, three software replicas on one processor, in user space, faults injected from a schedule. Not qualification evidence. |
| Continuous integration and configuration control | CI on Linux x86_64, Linux ARM64 and macOS; static ARM builds tested under emulation; Kani on every change; tagged releases with checksums | `.github/workflows/` | Exists. |
| Software requirements | F´ requirements note | `docs/fprime-requirements.md` | Draft. Not baselined or reviewed by a program. |
| Software design | F´ design notes | `docs/fprime-design.md`, `docs/fprime-design-fpp.md` | Draft. |

## Not started

- Software classification and safety criticality assessment.
- A software development plan and a software assurance plan.
- Requirements traceability from mission requirements to code and tests.
- Structural coverage measurement (statement, decision, MC/DC).
- A coding standard and a check that the code meets it.
- Independent verification and validation.
- Hardware-in-the-loop testing with resets and power cuts on target-class processors. The scripts exist (`scripts/board.sh`); no board run has been done. The OPS-SAT runs above are software runs on the target processor, not hardware-in-the-loop tests.
- Radiation testing. The OPS-SAT runs of 18 and 22 October 2026 (227 and 136 seconds) saw no real upset; they are not radiation tests.
- A qualification plan for the Rust toolchain (compiler, `core` library, Kani). **TODO.**

## Questions

Email [contact@mru.space](mailto:contact@mru.space).
