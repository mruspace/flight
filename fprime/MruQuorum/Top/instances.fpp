module MruQuorum {

  # Base IDs: 0x1000CCxx in the main topology; CdhCore uses 0x01000000.

  module Default {
    constant QUEUE_SIZE = 10
    constant STACK_SIZE = 128 * 1024
  }

  # ----------------------------------------------------------------------
  # Active component instances
  # ----------------------------------------------------------------------

  @ Every cycle: the manager, then the recorder
  instance rateGroupFast: Svc.ActiveRateGroup base id 0x10001000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 43

  @ Once a second: telemetry, commands, health pings, events, sequencer
  instance rateGroupSlow: Svc.ActiveRateGroup base id 0x10002000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 42

  instance cmdSeq: Svc.CmdSequencer base id 0x10003000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 40

  instance manager: Mru.RedundancyManager base id 0x10004000 \
    queue size 32 \
    stack size Default.STACK_SIZE \
    priority 44

  # ----------------------------------------------------------------------
  # Passive component instances
  # ----------------------------------------------------------------------

  instance chronoTime: Svc.ChronoTime base id 0x10010000

  instance rateGroupDriver: Svc.RateGroupDriver base id 0x10011000

  instance timer: Svc.LinuxTimer base id 0x10012000

  instance registry: Mru.HealthRegistry base id 0x10013000

  @ Test builds only: the faults, by command
  instance injector: Mru.FaultInjector base id 0x10014000

  @ Experiment builds only: checks every delivered result, and acts on the
  @ cycle for the real faults in replica processes
  instance recorder: Mru.RunRecorder base id 0x10015000

  @ One hub, link and adapter per replica process
  instance hub0: Svc.GenericHub base id 0x10016000
  instance hub1: Svc.GenericHub base id 0x10017000
  instance hub2: Svc.GenericHub base id 0x10018000
  instance link0: Mru.SeqPacketDriver base id 0x10019000
  instance link1: Mru.SeqPacketDriver base id 0x1001A000
  instance link2: Mru.SeqPacketDriver base id 0x1001B000
  instance adapter0: Drv.ByteStreamBufferAdapter base id 0x1001C000
  instance adapter1: Drv.ByteStreamBufferAdapter base id 0x1001D000
  instance adapter2: Drv.ByteStreamBufferAdapter base id 0x1001E000

  @ Hub message buffers, for all three links
  instance buffers: Svc.BufferManager base id 0x1001F000

}
