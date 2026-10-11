####
# This sets up the build system for the 'Mru' project, including
# components and deployments from project.cmake. In addition, it imports the core F Prime components.
####

cmake_minimum_required(VERSION 3.24.2)
project(Mru C CXX)

###
# F' Core Setup
# This includes all of the F prime core components, and imports the make-system.
###
include("${CMAKE_CURRENT_LIST_DIR}/lib/fprime/cmake/FPrime.cmake")
# NOTE: register custom targets between these two lines
fprime_setup_included_code()


# The quorum decision core, built by cargo as a no_std static library with a
# C interface (quorum-ffi). F´ v4.4.1 finds cargo but has no Rust build support.
set(MRU_FLIGHT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(QUORUM_FFI_LIB "${MRU_FLIGHT_ROOT}/target/release/libquorum_ffi.a")
add_custom_command(
    OUTPUT "${QUORUM_FFI_LIB}"
    COMMAND cargo rustc -p quorum-ffi --release --quiet --features c-staticlib
            --crate-type staticlib -- -C panic=abort
    WORKING_DIRECTORY "${MRU_FLIGHT_ROOT}"
    DEPENDS "${MRU_FLIGHT_ROOT}/quorum/src/lib.rs" "${MRU_FLIGHT_ROOT}/quorum-ffi/src/lib.rs"
    COMMENT "Building the quorum core (Rust, quorum-ffi)"
)
add_custom_target(quorum_ffi_build DEPENDS "${QUORUM_FFI_LIB}")
add_library(quorum_ffi STATIC IMPORTED GLOBAL)
set_target_properties(quorum_ffi PROPERTIES
    IMPORTED_LOCATION "${QUORUM_FFI_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${MRU_FLIGHT_ROOT}/quorum-ffi/include")
add_dependencies(quorum_ffi quorum_ffi_build)

# This includes project-wide objects
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Mru")
