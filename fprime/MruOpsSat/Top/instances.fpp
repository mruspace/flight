module MruOpsSat {

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

  instance host0: Mru.ReplicaHost base id 0x10005000 \
    queue size 16 \
    stack size Default.STACK_SIZE \
    priority 45

  instance host1: Mru.ReplicaHost base id 0x10006000 \
    queue size 16 \
    stack size Default.STACK_SIZE \
    priority 45

  instance host2: Mru.ReplicaHost base id 0x10007000 \
    queue size 16 \
    stack size Default.STACK_SIZE \
    priority 45

  # ----------------------------------------------------------------------
  # Passive component instances
  # ----------------------------------------------------------------------

  instance chronoTime: Svc.ChronoTime base id 0x10010000

  instance rateGroupDriver: Svc.RateGroupDriver base id 0x10011000

  instance timer: Svc.LinuxTimer base id 0x10012000

  instance registry: Mru.HealthRegistry base id 0x10013000

  @ Test builds only: the faults, by command
  instance injector: Mru.FaultInjector base id 0x10014000

  @ Experiment builds only: checks every delivered result
  instance recorder: Mru.RunRecorder base id 0x10015000

}
