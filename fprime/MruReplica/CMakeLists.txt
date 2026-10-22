#####
# MruReplica: one replica of Mru Flight as its own F´ deployment, for OPS-SAT
# run 3. A ReplicaHost behind F´'s GenericHub, linked to the manager process
# (MruQuorum) over one end of a Unix socket pair that the manager made.
#####

add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Top/")

register_fprime_deployment(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/Main.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../MruOpsSat/RustSupport.cpp"
    DEPENDS
        ${FPRIME_CURRENT_MODULE}_Top
)
