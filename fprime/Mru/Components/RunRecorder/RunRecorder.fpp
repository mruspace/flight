module Mru {

  @ Experiment builds only, like FaultInjector: checks every delivered result
  @ against the known computation and keeps the counts for the run's summary.
  @ The manager never sees this check; it stands in for the ground.
  passive component RunRecorder {

    @ Delivered results from RedundancyManager
    guarded input port resultIn: Result

    @ Rate group: counts cycles
    guarded input port schedIn: Svc.Sched

    @ The command sequence ended, with its status
    guarded input port seqDoneIn: Fw.CmdResponse

    @ Reads the manager's channels from the telemetry database
    output port tlmGet: Fw.TlmGet

    @ Asks the command sequencer to run a sequence file
    output port seqRunOut: Svc.CmdSeqIn

    event port logOut
    text event port logTextOut
    time get port timeCaller
    telemetry port tlmOut

    @ A delivered result was wrong
    event WrongResult(
      cycle: U64 @< The cycle
    ) \
      severity warning high \
      format "Wrong result delivered for cycle {}" \
      throttle 10

    @ Delivered results that were correct
    telemetry Correct: U64 update on change

    @ Delivered results that were wrong
    telemetry Wrong: U64 update on change

  }

}
