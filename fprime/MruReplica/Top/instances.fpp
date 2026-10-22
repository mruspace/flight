module MruReplica {

  instance host: Mru.ReplicaHost base id 0x20001000 \
    queue size 16 \
    stack size 128 * 1024 \
    priority 45

  @ The far end of the manager's hub for this replica
  instance hub: Svc.GenericHub base id 0x20002000

  @ This end of the socket pair
  instance link: Mru.SeqPacketDriver base id 0x20003000

  instance adapter: Drv.ByteStreamBufferAdapter base id 0x20004000

  instance buffers: Svc.BufferManager base id 0x20005000

}
