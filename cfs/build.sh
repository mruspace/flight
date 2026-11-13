#!/bin/sh
# build.sh: build Mru's cFS mission (cFE, OSAL and PSP from NASA's cFS
# bundle v7.0.1, and the Mru app) in the F´ build container, with the
# quorum core's C interface for the same target. Output in cfs/dist/:
# core-cpu1 and cf/ (the startup script).
#
#   ./cfs/build.sh            a static 32-bit ARM build, for OPS-SAT PRETTY
#   NATIVE=1 ./cfs/build.sh   a native Linux build, for tests (cfs/dist-native)
set -eu
cd "$(dirname "$0")/.."
if [ -n "${NATIVE:-}" ]; then
    sim=native rust= lib=target/release/libquorum_ffi.a out=cfs/dist-native o=/build/cfs-native
else
    sim= rust="--target armv7-unknown-linux-gnueabihf" lib=target/armv7-unknown-linux-gnueabihf/release/libquorum_ffi.a
    out=cfs/dist o=/build/cfs-arm
fi
./scripts/fprime-docker.sh sh -c "
set -e
cargo rustc -p quorum-ffi --release --quiet $rust --features c-staticlib --crate-type staticlib -- -C panic=abort
export MRU_QUORUM_LIB=/src/$lib MISSIONCONFIG=mru SIMULATION=$sim
cd cfs
[ -f $o/.prep ] || make O=$o BUILDTYPE=release prep >/tmp/prep.log 2>&1 || { tail -30 /tmp/prep.log; exit 1; }
make O=$o -j8 >/tmp/make.log 2>&1 || { grep -E 'error|Error' /tmp/make.log | head -30; exit 1; }
make O=$o install >/tmp/install.log 2>&1 || { tail -20 /tmp/install.log; exit 1; }
cd /src
rm -rf $out && mkdir -p $out/cf
cp $o/exe/cpu1/core-cpu1 $out/
cp $o/exe/cpu1/cf/cfe_es_startup.scr $out/cf/
"
ls -l "$out" "$out/cf"
