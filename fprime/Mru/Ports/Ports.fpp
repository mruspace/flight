module Mru {

  @ A request to one replica for one cycle
  port ReplicaRequest(
    cycle: U64 @< The cycle the result is for
    kind: RequestKind @< What to compute
    upset: Upset @< Bit flip to apply (test builds only)
  )

  @ A replica's answer to a request
  port ReplicaReply(
    cycle: U64 @< The cycle of the request
    kind: RequestKind @< The kind of the request
    first: U64 @< The result
    second: U64 @< The second result, for COMPUTE_TWICE
  )

  @ Enable or disable a replica: a disabled replica does not answer
  port ReplicaEnable(
    enabled: bool @< True to answer requests
  )

  @ A delivered result
  port Result(
    cycle: U64 @< The cycle it belongs to
    value: U64 @< The value the replicas agreed on
  )

  @ Record that a replica was outvoted; returns true when it must be diagnosed
  port HealthStrike(
    replica: FwIndexType @< The replica
    cycle: U64 @< The cycle of the outvote
  ) -> bool

  @ Forget a replica's strikes after it passed its diagnosis
  port HealthClear(
    replica: FwIndexType @< The replica
  )

}
