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

    @ Enables or disables a replica (MRU-RM-011, 019, 020)
    output port enableOut: [Replicas] ReplicaEnable

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

    @ Set an ACTIVE slot aside to save power (MRU-RM-019)
    async command STANDBY_REPLICA(
      slot: U8 @< The slot, 0 to 2
    )

    @ Admit a STANDBY slot, or a spare in a RETIRED slot, after a known-answer test (MRU-RM-020)
    async command ADMIT_REPLICA(
      slot: U8 @< The slot, 0 to 2
    )

    # ----------------------------------------------------------------------
    # Parameters
    # ----------------------------------------------------------------------

    @ Cycles between known-answer tests on the last replica (MRU-RM-008, 015)
    param KatPeriod: U32 default 64

    @ Cycles between saves of the state file; 0 saves on change only (MRU-RM-022)
    param StateSavePeriod: U32 default 1000

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

    @ A slot was set aside by command (MRU-RM-019)
    event ReplicaStandby(
      slot: U8 @< The slot
      cycle: U64 @< The cycle
    ) \
      severity activity high \
      format "Replica {} on standby at cycle {}"

    @ An admission was started by command (MRU-RM-020)
    event AdmitRequested(
      slot: U8 @< The slot
      cycle: U64 @< The cycle
    ) \
      severity activity high \
      format "Admitting replica {} at cycle {}: known-answer test requested"

    @ A slot passed its known-answer test and is ACTIVE (MRU-RM-020)
    event ReplicaAdmitted(
      slot: U8 @< The slot
      cycle: U64 @< The cycle
    ) \
      severity activity high \
      format "Replica {} admitted at cycle {}"

    @ An admission failed; the slot keeps its state (MRU-RM-020)
    event AdmitFailed(
      slot: U8 @< The slot
      reason: AdmitFailure @< Why
      cycle: U64 @< The cycle
    ) \
      severity warning low \
      format "Replica {} not admitted ({}) at cycle {}"

    @ A slot command was refused (MRU-RM-019, 020)
    event SlotCommandRefused(
      slot: U8 @< The slot asked for
      reason: RefuseReason @< Why
    ) \
      severity warning low \
      format "Command on slot {} refused: {}"

    @ State restored from the state file at start (MRU-RM-022)
    event StateRestored(
      policy: Policy @< The policy
      slots: SlotStates @< The slot states
      cycle: U64 @< The cycle it resumes from
    ) \
      severity activity high \
      format "State restored: policy {}, slots {}, cycle {}"

    @ No valid state file at start; all slots ACTIVE (MRU-RM-022)
    event StateNotRestored \
      severity warning low \
      format "No valid state file; starting with all slots active"

    @ The state file could not be written (MRU-RM-022)
    event StateSaveFailed(
      cycle: U64 @< The cycle
    ) \
      severity warning high \
      format "State file not written at cycle {}" \
      throttle 5

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

    @ State of each slot (MRU-RM-021)
    telemetry Slots: SlotStates update on change

  }

}
