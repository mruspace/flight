// ======================================================================
// \title  RedundancyManager.hpp
// \author wbnns
// \brief  hpp file for RedundancyManager component implementation class
// ======================================================================

#ifndef Mru_RedundancyManager_HPP
#define Mru_RedundancyManager_HPP

#include "Mru/Components/RedundancyManager/RedundancyManagerComponentAc.hpp"

namespace Mru {

class RedundancyManager final : public RedundancyManagerComponentBase {

public:
  // ----------------------------------------------------------------------
  // Component construction and destruction
  // ----------------------------------------------------------------------

  //! Construct RedundancyManager object
  RedundancyManager(const char *const compName //!< The component name
  );

  //! Destroy RedundancyManager object
  ~RedundancyManager();

private:
  // ----------------------------------------------------------------------
  // Handler implementations for typed input ports
  // ----------------------------------------------------------------------

  //! Handler implementation for pingIn
  //!
  //! Health ping
  void pingIn_handler(FwIndexType portNum, //!< The port number
                      U32 key              //!< Value to return to pinger
                      ) override;

  //! Handler implementation for replyIn
  //!
  //! Replies from the replicas, queued on this component's thread
  void
  replyIn_handler(FwIndexType portNum,          //!< The port number
                  U64 cycle,                    //!< The cycle of the request
                  const Mru::RequestKind &kind, //!< The kind of the request
                  U64 first,                    //!< The result
                  U64 second //!< The second result, for COMPUTE_TWICE
                  ) override;

  //! Handler implementation for schedIn
  //!
  //! Rate group: decide the previous cycle, then request the next (MRU-RM-001,
  //! 002)
  void schedIn_handler(FwIndexType portNum, //!< The port number
                       U32 context          //!< The call order
                       ) override;

private:
  // ----------------------------------------------------------------------
  // Handler implementations for commands
  // ----------------------------------------------------------------------

  //! Handler implementation for command SET_POLICY
  //!
  //! Choose the redundancy policy (MRU-RM-013)
  void SET_POLICY_cmdHandler(FwOpcodeType opCode, //!< The opcode
                             U32 cmdSeq, //!< The command sequence number
                             const Mru::Policy &policy //!< SHRINK or TMR
                             ) override;

  //! Handler implementation for command STANDBY_REPLICA
  //!
  //! Set an ACTIVE slot aside to save power (MRU-RM-019)
  void STANDBY_REPLICA_cmdHandler(FwOpcodeType opCode, //!< The opcode
                                  U32 cmdSeq, //!< The command sequence number
                                  U8 slot     //!< The slot, 0 to 2
                                  ) override;

  //! Handler implementation for command ADMIT_REPLICA
  //!
  //! Admit a STANDBY slot, or a spare in a RETIRED slot, after a known-answer
  //! test (MRU-RM-020)
  void ADMIT_REPLICA_cmdHandler(FwOpcodeType opCode, //!< The opcode
                                U32 cmdSeq, //!< The command sequence number
                                U8 slot     //!< The slot, 0 to 2
                                ) override;
};

} // namespace Mru

#endif
