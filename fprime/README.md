# Mru Flight F´ components

F´ v4.4.1 components around the `quorum` decision core: `RedundancyManager`,
`HealthRegistry`, `ReplicaHost`, and `FaultInjector` for test builds. Every
decision comes from the Rust core through [`quorum-ffi`](../quorum-ffi), so no
vote, compare or self-check logic is written again in C++.

- Requirements: [docs/fprime-requirements.md](../docs/fprime-requirements.md)
- Design and results: [docs/fprime-design-fpp.md](../docs/fprime-design-fpp.md)

## Build and test

```sh
git submodule update --init --recursive    # F´ v4.4.1 in lib/fprime
python3 -m venv fprime-venv && . fprime-venv/bin/activate
pip install -r requirements.txt
fprime-util generate && fprime-util build  # components; cargo builds the core
fprime-util generate --ut && fprime-util check --all
```

`check --all` runs the unit tests of each component and the system test
`Mru/Test/Parity`, which runs the bench schedules through the four components
and compares the counts with the demo's (`../scripts/parity-cases.sh` writes
the expected counts from the demo).

On macOS 27, the CMake 3.26 that F´ v4.4.1 pins fails with a recursion error,
also on F´'s own sample project. Put CMake 4.4 first on the path
(`pip install cmake` in another environment). Linux works with the pinned
version.
