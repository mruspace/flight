module Mru {

  @ Number of replicas
  constant Replicas = 3

  @ Redundancy policy, as quorum::Policy
  enum Policy : U8 {
    SHRINK = 0 @< Vote on three, compare on two, self-check on one
    TMR = 1 @< Vote on three, compare on two, stop on one
  }

  @ Operating mode, as quorum::Mode
  enum Mode : U8 {
    VOTE = 0
    COMPARE = 1
    SELF_CHECK = 2 @< One replica computes everything twice
    HALTED = 3
  }

  @ What a request asks a replica for
  enum RequestKind : U8 {
    COMPUTE = 0 @< One result
    COMPUTE_TWICE = 1 @< Two results of the same computation (self-check)
    KNOWN_ANSWER = 2 @< The result for the known-answer input
  }

  @ Why a replica was retired
  enum RetireReason : U8 {
    KNOWN_ANSWER_FAILED = 0 @< Its known-answer result, asked for a diagnosis, was wrong
    KNOWN_ANSWER_MISSING = 1 @< Its known-answer result, asked for a diagnosis, did not arrive by the next cycle
    LOST = 2 @< It missed three cycles in a row
    PERIODIC_TEST_FAILED = 3 @< Its periodic known-answer result on one replica was wrong
    PERIODIC_TEST_MISSING = 4 @< Its periodic known-answer result on one replica did not arrive
  }

  @ A bit flip to apply to a replica's working memory during a computation.
  @ Set only by FaultInjector in test builds; all false in flight.
  struct Upset {
    first: bool @< Flip during the (first) computation
    second: bool @< The same flip during the second computation (common mode)
    word: U16 @< Word of the working block
    $bit: U8 @< Bit of that word
  }

  @ Faults FaultInjector can apply to a replica
  enum FaultKind : U8 {
    KILL = 0 @< The replica stops answering from this cycle
    HANG = 1 @< The replica stops answering from this cycle
    STUCK = 2 @< The replica returns the same wrong value from this cycle
    CORRUPT = 3 @< One bit flip in this cycle's computation
  }

  @ Strike count per replica
  array StrikeCounts = [Replicas] U32

  @ State of a replica slot (MRU-RM-018)
  enum SlotState : U8 {
    ACTIVE = 0 @< Receives requests and counts as live
    STANDBY = 1 @< Set aside to save power; can be admitted again
    RETIRED = 2 @< Failed; the slot can take a spare
  }

  @ State of every slot
  array SlotStates = [Replicas] SlotState

  @ Why a slot command was refused
  enum RefuseReason : U8 {
    NO_SUCH_SLOT = 0 @< The slot is not 0 to 2
    WRONG_STATE = 1 @< The slot is not in a state the command applies to
    WOULD_HALT = 2 @< The policy could not run on the slots left
    BUSY = 3 @< An admission is already waiting for its known answer
  }

  @ Why an admission failed
  enum AdmitFailure : U8 {
    KNOWN_ANSWER_FAILED = 0 @< The known-answer result was wrong
    KNOWN_ANSWER_MISSING = 1 @< No known-answer result by the next cycle
  }

}
