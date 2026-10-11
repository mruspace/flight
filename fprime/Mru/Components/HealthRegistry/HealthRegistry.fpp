module Mru {

  @ Strike record per replica: decides when a replica that keeps being outvoted
  @ must be diagnosed. The record and the rule are quorum::Health.
  passive component HealthRegistry {

    @ Record an outvote; returns true when the replica must be diagnosed (MRU-HR-001)
    guarded input port strikeIn: HealthStrike

    @ Forget a replica's strikes (MRU-HR-002)
    guarded input port clearIn: HealthClear

    time get port timeCaller
    telemetry port tlmOut

    @ Strikes in the current window, per replica (MRU-HR-003)
    telemetry Strikes: StrikeCounts update on change

  }

}
