####
# armv7-linux-static: a static 32-bit ARM Linux build (hard float) for the
# SEPP of OPS-SAT PRETTY (Cortex-A9, kernel 4.9, Alpine with musl). Debian's
# armhf cross compiler links glibc statically, so the binary needs nothing
# from the SEPP's own libraries. Used in the build container:
#   fprime-util generate armv7-linux-static --build-cache /build/arm
####
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
# The Rust core's static library carries its own copy of the ARM integer
# division helpers, which libgcc also has; the two are equivalent, so the
# linker may take either.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -Wl,--allow-multiple-definition")
set(CMAKE_FIND_ROOT_PATH /usr/arm-linux-gnueabihf)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# the quorum core for the same target
set(MRU_RUST_TARGET armv7-unknown-linux-gnueabihf)
