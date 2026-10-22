#!/bin/sh
# fprime-docker.sh COMMAND...: run a command in the F´ build container
# (fprime/toolchain/Dockerfile) with the repository mounted at /src. The
# Rust target folder and the F´ build caches (/build) live in Docker volumes,
# so Linux builds never mix with builds on the host.
#
#   ./scripts/fprime-docker.sh sh -c 'cd fprime && fprime-util generate --build-cache /build/native && fprime-util build --build-cache /build/native'
set -eu
cd "$(dirname "$0")/.."
image=${MRU_FPRIME_IMAGE:-mru-fprime-build:dev}
if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build -t "$image" -f fprime/toolchain/Dockerfile fprime
fi
set -- docker run --rm -i \
    -v "$PWD:/src" -w /src \
    -v mru-fprime-target:/src/target \
    -v mru-fprime-build:/build \
    -v mru-fprime-cargo:/usr/local/cargo/registry \
    "$image" "$@"
exec "$@"
