module Mru {

  @ Runs the payload for one replica and answers requests and known-answer
  @ tests. One instance per replica.
  active component ReplicaHost {

    @ A request for one cycle (MRU-RH-001, 002, 003)
    async input port requestIn: ReplicaRequest

    @ The answer
    output port replyOut: ReplicaReply

    @ Retirement: stop answering (MRU-RH-004)
    async input port retireIn: Fw.Signal

    @ Health ping
    async input port pingIn: Svc.Ping

    @ Health ping return
    output port pingOut: Svc.Ping

    match pingOut with pingIn

  }

}
