// ======================================================================
// \title  RedundancyManager.cpp
// \author wbnns
// \brief  cpp file for RedundancyManager component implementation class
// ======================================================================

#include "Mru/Components/RedundancyManager/RedundancyManager.hpp"

namespace Mru {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

RedundancyManager ::RedundancyManager(const char *const compName)
    : RedundancyManagerComponentBase(compName) {}

RedundancyManager ::~RedundancyManager() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void RedundancyManager ::pingIn_handler(FwIndexType portNum, U32 key) {
  // TODO
}

void RedundancyManager ::replyIn_handler(FwIndexType portNum, U64 cycle,
                                         const Mru::RequestKind &kind,
                                         U64 first, U64 second) {
  // TODO
}

void RedundancyManager ::schedIn_handler(FwIndexType portNum, U32 context) {
  // TODO
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void RedundancyManager ::SET_POLICY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq,
                                               const Mru::Policy &policy) {
  // TODO
  this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void RedundancyManager ::STANDBY_REPLICA_cmdHandler(FwOpcodeType opCode,
                                                    U32 cmdSeq, U8 slot) {
  // TODO
  this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void RedundancyManager ::ADMIT_REPLICA_cmdHandler(FwOpcodeType opCode,
                                                  U32 cmdSeq, U8 slot) {
  // TODO
  this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

} // namespace Mru
