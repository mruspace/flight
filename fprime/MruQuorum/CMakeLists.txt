#####
# MruQuorum: the manager process of Mru Flight for OPS-SAT PRETTY (exp4051,
# run 3, version 2). RedundancyManager, HealthRegistry, FaultInjector and
# RunRecorder with F´'s own command, event, telemetry, health and sequencing
# services, driven by a Linux timer. Each replica is a process of its own
# (MruReplica), linked through F´'s GenericHub over a Unix socket pair. No
# communications stack and no network. Events are printed as text lines.
#####

add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Top/")

register_fprime_deployment(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/Main.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../MruOpsSat/RustSupport.cpp"
    DEPENDS
        ${FPRIME_CURRENT_MODULE}_Top
)
