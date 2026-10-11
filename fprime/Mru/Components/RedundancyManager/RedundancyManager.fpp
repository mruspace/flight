module Mru {

  @ Runs the shrinking quorum, or fixed TMR, over three replicas: one result per
  @ cycle. Every decision comes from the quorum crate (MRU-RM-016).
  active component RedundancyManager {

    # ----------------------------------------------------------------------
    # Ports
    # ----------------------------------------------------------------------

    @ Rate group: decide the previous cycle, then request the next (MRU-RM-001, 002)
    async input port schedIn: Svc.Sched

    @ Requests to the replicas
    output port requestOut: [Replicas] ReplicaRequest

    @ Replies from the replicas, queued on this component's thread
    async input port replyIn: [Replicas] ReplicaReply

    @ Tells a retired replica to stop (MRU-RH-004)
    output port retireOut: [Replicas] Fw.Signal

    @ Delivered results (MRU-RM-003)
    output port resultOut: Result

    @ Record an outvote in HealthRegistry (MRU-RM-006)
    output port healthStrike: HealthStrike

    @ Clear a replica's strikes in HealthRegistry (MRU-RM-009)
    output port healthClear: HealthClear

    @ Health ping
    async input port pingIn: Svc.Ping

    @ Health ping return
    output port pingOut: Svc.Ping

    match pingOut with pingIn

    # ----------------------------------------------------------------------
    # Special ports
    # ----------------------------------------------------------------------

    command recv port cmdIn
    command reg port cmdRegOut
    command resp port cmdResponseOut
    event port logOut
    text event port logTextOut
    time get port timeCaller
    telemetry port tlmOut
    param get port prmGetOut
    param set port prmSetOut

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Choose the redundancy policy (MRU-RM-013)
    async command SET_POLICY(
      policy: Policy @< SHRINK or TMR
    )

    # ----------------------------------------------------------------------
    # Parameters
    # ----------------------------------------------------------------------

    @ Cycles between known-answer tests on the last replica (MRU-RM-008, 015)
    param KatPeriod: U32 default 64

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ The policy was set by command
    event PolicySet(
      policy: Policy @< The new policy
    ) \
      severity activity high \
      format "Policy set to {}"

    @ The mode changed as replicas were lost (MRU-RM-004)
    event ModeChanged(
      previous: Mode @< The mode before
      current: Mode @< The mode now
      cycle: U64 @< The cycle of the change
    ) \
      severity activity high \
      format "Mode {} -> {} at cycle {}"

    @ A disagreement was caught; nothing was delivered for the cycle (MRU-RM-005)
    event DisagreementCaught(
      cycle: U64 @< The cycle
      mode: Mode @< The mode it was caught in
    ) \
      severity warning low \
      format "Disagreement caught at cycle {} in {}" \
      throttle 10

    @ A known-answer test was requested from a replica (MRU-RM-006, 007, 008)
    event DiagnosisRequested(
      replica: U8 @< The replica
      cycle: U64 @< The cycle
    ) \
      severity activity low \
      format "Known-answer test requested from replica {} at cycle {}"

    @ A replica passed its known-answer test; its strikes were cleared (MRU-RM-009)
    event ReplicaCleared(
      replica: U8 @< The replica
      cycle: U64 @< The cycle
    ) \
      severity activity low \
      format "Replica {} passed its known-answer test at cycle {}"

    @ A replica was retired (MRU-RM-009, 010, 011)
    event ReplicaRetired(
      replica: U8 @< The replica
      reason: RetireReason @< Why
      cycle: U64 @< The cycle
    ) \
      severity warning high \
      format "Replica {} retired ({}) at cycle {}"

    @ The policy cannot continue with the replicas left (MRU-RM-012)
    event Halted(
      cycle: U64 @< The cycle
    ) \
      severity warning high \
      format "Halted at cycle {}"

    # ----------------------------------------------------------------------
    # Telemetry (MRU-RM-014)
    # ----------------------------------------------------------------------

    @ The current policy
    telemetry CurrentPolicy: Policy update on change

    @ The current mode
    telemetry CurrentMode: Mode update on change

    @ Replicas not retired
    telemetry LiveReplicas: U8 update on change

    @ Results delivered
    telemetry Delivered: U64

    @ Disagreements caught
    telemetry Caught: U64

    @ Replicas retired
    telemetry Retired: U8 update on change

  }

}
