module Mru {

  @ Test builds only (MRU-FI-003). Sits between RedundancyManager and the
  @ replicas and applies faults: silence (kill, hang), a stuck value, and bit
  @ flips in a replica's working memory. Upsets are drawn on the manager's
  @ thread, replica by replica, in the same order as the quorum demo, so the
  @ same seed gives the same upsets.
  passive component FaultInjector {

    # ----------------------------------------------------------------------
    # Ports
    # ----------------------------------------------------------------------

    @ Requests from RedundancyManager
    guarded input port requestIn: [Replicas] ReplicaRequest

    @ Requests on to the replicas
    output port requestOut: [Replicas] ReplicaRequest

    @ Replies from the replicas
    guarded input port replyIn: [Replicas] ReplicaReply

    @ Replies on to RedundancyManager
    output port replyOut: [Replicas] ReplicaReply

    # ----------------------------------------------------------------------
    # Special ports
    # ----------------------------------------------------------------------

    command recv port cmdIn
    command reg port cmdRegOut
    command resp port cmdResponseOut
    event port logOut
    text event port logTextOut
    time get port timeCaller

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Schedule a fault on a replica at a cycle (MRU-FI-001)
    guarded command INJECT(
      kind: FaultKind @< The fault
      replica: U8 @< The replica, 0 to 2
      cycle: U64 @< The cycle it takes effect
    )

    @ Remove every fault of a replica: new hardware in its slot, such as a spare
    guarded command CLEAR_FAULTS(
      replica: U8 @< The replica, 0 to 2
    )

    @ Set random upsets (MRU-FI-002)
    guarded command SET_UPSETS(
      rate: F64 @< Chance per replica per request of a bit flip
      commonMode: F64 @< Share of self-check upsets that hit both computations alike
      seed: U64 @< Seed, as the demo's --seed
    )

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ A fault was scheduled
    event FaultScheduled(
      kind: FaultKind @< The fault
      replica: U8 @< The replica
      cycle: U64 @< The cycle
    ) \
      severity activity high \
      format "{} scheduled on replica {} at cycle {}"

    @ A scheduled fault took effect
    event FaultApplied(
      kind: FaultKind @< The fault
      replica: U8 @< The replica
      cycle: U64 @< The cycle
    ) \
      severity warning low \
      format "{} applied to replica {} at cycle {}"

    @ A fault was refused: no free slot, or a replica out of range
    event FaultRefused(
      replica: U8 @< The replica asked for
    ) \
      severity warning low \
      format "Fault on replica {} refused"

    @ Every fault of a replica was removed
    event FaultsCleared(
      replica: U8 @< The replica
    ) \
      severity activity high \
      format "Faults cleared on replica {}"

    @ Upset settings were refused: rate and common-mode share must be 0 to 1
    event UpsetsRefused(
      rate: F64 @< The rate asked for
      commonMode: F64 @< The common-mode share asked for
    ) \
      severity warning low \
      format "Upsets refused: rate {}, common mode {}"

    @ Random upsets were set
    event UpsetsSet(
      rate: F64 @< The rate
      commonMode: F64 @< The common-mode share
      seed: U64 @< The seed
    ) \
      severity activity high \
      format "Upsets: rate {}, common mode {}, seed {}"

  }

}
