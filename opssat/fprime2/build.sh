#!/bin/sh
# build.sh: build run 3, version 2, for the SEPP and compile its command
# sequences, in the F´ build container. Output in opssat/fprime2/dist/:
# MruQuorum and MruReplica (static 32-bit ARM, optimized) and shrink.bin, tmr.bin,
# restart.bin, compiled against that build's dictionary, plus the dictionary.
#
#   ./opssat/fprime2/build.sh           the ARM build for the package
#   NATIVE=1 ./opssat/fprime2/build.sh   a native Linux build, for tests
set -eu
cd "$(dirname "$0")/../.."
if [ -n "${NATIVE:-}" ]; then
    platform= cache=/build/native-rel bin=Linux out=opssat/fprime2/dist-native
else
    platform=armv7-linux-static cache=/build/arm-rel bin=armv7-linux-static out=opssat/fprime2/dist
fi
./scripts/fprime-docker.sh sh -c "
set -e
cd fprime
[ -f $cache/CMakeCache.txt ] || fprime-util generate $platform --build-cache $cache -DCMAKE_BUILD_TYPE=Release >/tmp/generate.log 2>&1 || { tail -30 /tmp/generate.log; exit 1; }
fprime-util build $platform --build-cache $cache -j8 >/tmp/build.log 2>&1 || { grep -E 'error|FAILED' /tmp/build.log | head -30; exit 1; }
cd /src
rm -rf $out && mkdir -p $out
cp $cache/bin/$bin/MruQuorum $cache/bin/$bin/MruReplica $out/
cp $cache/MruQuorum/Top/MruQuorumTopologyDictionary.json $out/
for s in shrink tmr restart; do
    fprime-seqgen --dictionary $out/MruQuorumTopologyDictionary.json opssat/fprime2/\$s.seq $out/\$s.bin >/dev/null
done
"
ls -l "$out"
