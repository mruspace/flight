#####
# MruOpsSat: the F´ deployment of Mru Flight for OPS-SAT PRETTY (exp4051, run 3).
# The four Mru components with F´'s own command, event, telemetry, health and
# sequencing services, driven by a Linux timer. No communications stack: the
# SEPP experiment has no network. Events are printed as text lines.
#####

add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Top/")

register_fprime_deployment(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/Main.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/RustSupport.cpp"
    DEPENDS
        ${FPRIME_CURRENT_MODULE}_Top
)
