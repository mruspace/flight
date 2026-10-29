# Contributing to Mru Flight

Thank you for your interest. Mru Flight has no qualification evidence; it
has run three times in orbit as a user-space experiment (the demo twice, the
F´ deployment once), which is not qualification.
Pull requests are welcome. For questions or bug reports, email
contact@mru.space.

## Before you open a pull request

Run the same checks as CI:

```sh
cargo test --workspace --release
./scripts/scenarios.sh
quorum-ffi/tests/c.sh
cargo build -p quorum --target thumbv7em-none-eabihf
cargo kani -p quorum -p quorum-ffi
./scripts/check-claims.sh
```

Kani needs a one-time install: `cargo install --locked kani-verifier && cargo kani setup`.
The F´ components build with `fprime-util`; see [docs/fprime-design.md](docs/fprime-design.md).

## Rules for changes

- `main` is protected. Make every change through a branch and a pull request.
- Keep the core (`quorum`) `no_std`, free of allocation and free of dependencies.
- A change to the core's behavior needs a test that shows the old behavior was wrong.
- Every number in the README comes from a real run or a results file. If a change
  moves a number, regenerate it with the script that produced it and commit the new
  results file. Do not edit numbers by hand.
- Write for engineers: short sentences, US spelling, limits next to claims.

## Questions

Email [contact@mru.space](mailto:contact@mru.space).

## License

By contributing, you agree that your contributions are licensed under the
[Apache License 2.0](LICENSE).
