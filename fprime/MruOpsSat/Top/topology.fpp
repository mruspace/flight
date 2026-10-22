module MruOpsSat {

  enum Ports_RateGroups {
    rateGroupFast
    rateGroupSlow
  }

  deployment topology MruOpsSat {

    import CdhCore.Subtopology

    instance chronoTime
    instance rateGroupFast
    instance rateGroupSlow
    instance rateGroupDriver
    instance timer
    instance cmdSeq
    instance manager
    instance registry
    instance injector
    instance host0
    instance host1
    instance host2
    instance recorder

    command connections instance CdhCore.cmdDisp
    event connections instance CdhCore.events
    telemetry connections instance CdhCore.tlmSend
    text event connections instance CdhCore.textLogger
    health connections instance CdhCore.$health
    time connections instance chronoTime

    connections RateGroups {
      timer.CycleOut -> rateGroupDriver.CycleIn

      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroupFast] -> rateGroupFast.CycleIn
      rateGroupFast.RateGroupMemberOut[0] -> manager.schedIn
      rateGroupFast.RateGroupMemberOut[1] -> recorder.schedIn

      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroupSlow] -> rateGroupSlow.CycleIn
      rateGroupSlow.RateGroupMemberOut[0] -> CdhCore.tlmSend.Run
      rateGroupSlow.RateGroupMemberOut[1] -> CdhCore.cmdDisp.run
      rateGroupSlow.RateGroupMemberOut[2] -> CdhCore.$health.Run
      rateGroupSlow.RateGroupMemberOut[3] -> CdhCore.events.run
      rateGroupSlow.RateGroupMemberOut[4] -> cmdSeq.schedIn
    }

    connections Sequencer {
      cmdSeq.comCmdOut -> CdhCore.cmdDisp.seqCmdBuff
      CdhCore.cmdDisp.seqCmdStatus -> cmdSeq.cmdResponseIn
      cmdSeq.seqDone -> recorder.seqDoneIn
      recorder.seqRunOut -> cmdSeq.seqRunIn
    }

    @ The test topology of the design note: manager, injector, replicas
    connections Quorum {
      manager.requestOut[0] -> injector.requestIn[0]
      manager.requestOut[1] -> injector.requestIn[1]
      manager.requestOut[2] -> injector.requestIn[2]
      injector.requestOut[0] -> host0.requestIn
      injector.requestOut[1] -> host1.requestIn
      injector.requestOut[2] -> host2.requestIn
      host0.replyOut -> injector.replyIn[0]
      host1.replyOut -> injector.replyIn[1]
      host2.replyOut -> injector.replyIn[2]
      injector.replyOut[0] -> manager.replyIn[0]
      injector.replyOut[1] -> manager.replyIn[1]
      injector.replyOut[2] -> manager.replyIn[2]
      manager.enableOut[0] -> host0.enableIn
      manager.enableOut[1] -> host1.enableIn
      manager.enableOut[2] -> host2.enableIn

      manager.healthStrike -> registry.strikeIn
      manager.healthClear -> registry.clearIn

      manager.resultOut -> recorder.resultIn
      recorder.tlmGet -> CdhCore.tlmSend.TlmGet
    }

  }

}
