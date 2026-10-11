# MRU-SYS-001: the F´ components against the quorum demo on the bench schedules
register_fprime_ut(
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/ParityTest.cpp"
  DEPENDS
    Mru_Components_RedundancyManager
    Mru_Components_HealthRegistry
    Mru_Components_ReplicaHost
    Mru_Components_FaultInjector
    STest
)
