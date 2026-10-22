module MruReplica {

  deployment topology MruReplica {

    instance host
    instance hub
    instance link
    instance adapter
    instance buffers

    @ The manager's ports, through the hub (the serial port numbers match
    @ the manager's hub for this replica)
    connections Replica {
      hub.serialOut[0] -> host.requestIn
      hub.serialOut[1] -> host.enableIn
      host.replyOut -> hub.serialIn[0]
    }

    @ Hub, adapter and driver: F´'s hub pattern over a byte-stream driver
    connections Link {
      hub.allocate -> buffers.bufferGetCallee
      hub.deallocate -> buffers.bufferSendIn
      hub.toBufferDriver -> adapter.bufferIn
      adapter.bufferInReturn -> hub.toBufferDriverReturn
      adapter.bufferOut -> hub.fromBufferDriver
      hub.fromBufferDriverReturn -> adapter.bufferOutReturn
      adapter.toByteStreamDriver -> link.$send
      link.ready -> adapter.byteStreamDriverReady
      link.$recv -> adapter.fromByteStreamDriver
      adapter.fromByteStreamDriverReturn -> link.recvReturnIn
      link.allocate -> buffers.bufferGetCallee
      link.deallocate -> buffers.bufferSendIn
    }

  }

}
