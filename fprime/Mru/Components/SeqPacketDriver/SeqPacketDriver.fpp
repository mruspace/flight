module Mru {

  @ Bytes over one end of a Unix socket pair that keeps message boundaries
  @ (SOCK_SEQPACKET): one send is one receive. Links a replica process to the
  @ manager process through F´'s GenericHub, with no framing and no network.
  @ The manager makes the pair and passes one end to each replica process.
  passive component SeqPacketDriver {

    import Drv.ByteStreamDriver

    @ Allocation for received data
    output port allocate: Fw.BufferGet

    @ Deallocation of allocated buffers
    output port deallocate: Fw.BufferSend

  }

}
