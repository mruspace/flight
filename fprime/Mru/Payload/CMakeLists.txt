# The replicas' task, shared by ReplicaHost and RunRecorder
register_fprime_module(
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/Payload.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/Payload.hpp"
  DEPENDS
    Fw_Types
)
