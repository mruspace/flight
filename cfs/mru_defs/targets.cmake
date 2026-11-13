# Mru's cFS mission: one processor, one application, linked statically.
#
# Only cFE, OSAL and PSP from NASA's cFS (bundle v7.0.1) and Mru's own app:
# no sample or lab applications, so nothing opens a network socket. The app
# is linked into the core executable (STATIC_APPLIST), so the build needs no
# dynamic loader and can be a fully static ARM binary for OPS-SAT PRETTY.
SET(MISSION_NAME "Mru")
SET(SPACECRAFT_ID 0x42)
SET(MISSION_CPUNAMES cpu1)
SET(cpu1_PROCESSORID 1)
SET(cpu1_STATIC_APPLIST mru)
SET(cpu1_STATIC_SYMLIST MRU_AppMain,MRU)
SET(cpu1_FILELIST cfe_es_startup.scr)
# armv7-linux-static: toolchain-armv7-linux-static.cmake; SIMULATION=native
# overrides it for tests
SET(cpu1_SYSTEM armv7-linux-static)
