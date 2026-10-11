#!/bin/sh
# Build the static library and run the C test against quorum.h.
set -eu
cd "$(dirname "$0")/../.."
cargo rustc -p quorum-ffi --release --quiet --features c-staticlib \
    --crate-type staticlib -- -C panic=abort
out=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -I quorum-ffi/include quorum-ffi/tests/test_quorum.c \
    target/release/libquorum_ffi.a -o "$out/test_quorum"
"$out/test_quorum"
rm -rf "$out"
