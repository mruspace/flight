# OPS-SAT PRETTY's processor: 32-bit ARM, hard float, Linux. Fully static,
# so the binary needs no library from the platform (as the F´ build).
SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_SYSTEM_PROCESSOR arm)
SET(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
SET(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
SET(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
SET(CFE_SYSTEM_PSPNAME pc-linux)
SET(OSAL_SYSTEM_OSTYPE posix)
