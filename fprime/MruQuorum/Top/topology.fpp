module MruQuorum {

  enum Ports_RateGroups {
    rateGroupFast
    rateGroupSlow
  }

  deployment topology MruQuorum {

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
    instance recorder
    instance hub0
    instance hub1
    instance hub2
    instance link0
    instance link1
    instance link2
    instance adapter0
    instance adapter1
    instance adapter2
    instance buffers

    command connections instance CdhCore.cmdDisp
    event connections instance CdhCore.events
    telemetry connections instance CdhCore.tlmSend
    text event connections instance CdhCore.textLogger
    health connections instance CdhCore.$health
    time connections instance chronoTime

    connections RateGroups {
      timer.CycleOut -> rateGroupDriver.CycleIn

      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroupFast] -> rateGroupFast.CycleIn
      # the recorder first: a real fault for a cycle lands before its requests
      rateGroupFast.RateGroupMemberOut[0] -> recorder.schedIn
      rateGroupFast.RateGroupMemberOut[1] -> manager.schedIn

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

    @ The test topology of the design note, with each replica in a process
    @ of its own behind a hub: serial port 0 carries requests, 1 enables,
    @ and the reply comes back on 0 (MruReplica's hub has the same numbers)
    connections Quorum {
      manager.requestOut[0] -> injector.requestIn[0]
      manager.requestOut[1] -> injector.requestIn[1]
      manager.requestOut[2] -> injector.requestIn[2]
      injector.requestOut[0] -> hub0.serialIn[0]
      injector.requestOut[1] -> hub1.serialIn[0]
      injector.requestOut[2] -> hub2.serialIn[0]
      manager.enableOut[0] -> hub0.serialIn[1]
      manager.enableOut[1] -> hub1.serialIn[1]
      manager.enableOut[2] -> hub2.serialIn[1]
      hub0.serialOut[0] -> injector.replyIn[0]
      hub1.serialOut[0] -> injector.replyIn[1]
      hub2.serialOut[0] -> injector.replyIn[2]
      injector.replyOut[0] -> manager.replyIn[0]
      injector.replyOut[1] -> manager.replyIn[1]
      injector.replyOut[2] -> manager.replyIn[2]

      manager.healthStrike -> registry.strikeIn
      manager.healthClear -> registry.clearIn

      manager.resultOut -> recorder.resultIn
      recorder.tlmGet -> CdhCore.tlmSend.TlmGet
    }

    @ Hub, adapter and driver per replica: F´'s hub pattern
    connections Links {
      hub0.allocate -> buffers.bufferGetCallee
      hub0.deallocate -> buffers.bufferSendIn
      hub0.toBufferDriver -> adapter0.bufferIn
      adapter0.bufferInReturn -> hub0.toBufferDriverReturn
      adapter0.bufferOut -> hub0.fromBufferDriver
      hub0.fromBufferDriverReturn -> adapter0.bufferOutReturn
      adapter0.toByteStreamDriver -> link0.$send
      link0.ready -> adapter0.byteStreamDriverReady
      link0.$recv -> adapter0.fromByteStreamDriver
      adapter0.fromByteStreamDriverReturn -> link0.recvReturnIn
      link0.allocate -> buffers.bufferGetCallee
      link0.deallocate -> buffers.bufferSendIn
      hub1.allocate -> buffers.bufferGetCallee
      hub1.deallocate -> buffers.bufferSendIn
      hub1.toBufferDriver -> adapter1.bufferIn
      adapter1.bufferInReturn -> hub1.toBufferDriverReturn
      adapter1.bufferOut -> hub1.fromBufferDriver
      hub1.fromBufferDriverReturn -> adapter1.bufferOutReturn
      adapter1.toByteStreamDriver -> link1.$send
      link1.ready -> adapter1.byteStreamDriverReady
      link1.$recv -> adapter1.fromByteStreamDriver
      adapter1.fromByteStreamDriverReturn -> link1.recvReturnIn
      link1.allocate -> buffers.bufferGetCallee
      link1.deallocate -> buffers.bufferSendIn
      hub2.allocate -> buffers.bufferGetCallee
      hub2.deallocate -> buffers.bufferSendIn
      hub2.toBufferDriver -> adapter2.bufferIn
      adapter2.bufferInReturn -> hub2.toBufferDriverReturn
      adapter2.bufferOut -> hub2.fromBufferDriver
      hub2.fromBufferDriverReturn -> adapter2.bufferOutReturn
      adapter2.toByteStreamDriver -> link2.$send
      link2.ready -> adapter2.byteStreamDriverReady
      link2.$recv -> adapter2.fromByteStreamDriver
      adapter2.fromByteStreamDriverReturn -> link2.recvReturnIn
      link2.allocate -> buffers.bufferGetCallee
      link2.deallocate -> buffers.bufferSendIn
    }

  }

}
