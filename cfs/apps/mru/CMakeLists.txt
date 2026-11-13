# Mru's cFS application: three replicas as child tasks, decided by the
# quorum core through its C interface (quorum-ffi, built by cfs/build.sh).
project(MRU_APP C)

set(MRU_FLIGHT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
if (NOT DEFINED ENV{MRU_QUORUM_LIB})
  message(FATAL_ERROR "MRU_QUORUM_LIB must name libquorum_ffi.a for this target (cfs/build.sh sets it)")
endif ()

add_cfe_app(mru fsw/src/mru_app.c fsw/src/mru_payload.c fsw/src/mru_config.c)
target_include_directories(mru PRIVATE ${MRU_FLIGHT_ROOT}/quorum-ffi/include fsw/src)
# libgcc first: cFE's own 64-bit divisions then come from GCC's runtime, not
# from the Rust library's built-ins, whose ARM unwind tables name Rust's
# unwinder (the core is built with panic=abort and never unwinds)
target_link_libraries(mru gcc $ENV{MRU_QUORUM_LIB})
