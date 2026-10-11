// ======================================================================
// \title  ParityCase.hpp
// \brief  One case of the parity test: a bench schedule and the demo's counts
// ======================================================================

#ifndef Mru_ParityCase_HPP
#define Mru_ParityCase_HPP

#include "Mru/Types/FaultKindEnumAc.hpp"
#include "Mru/Types/PolicyEnumAc.hpp"

namespace Mru {

struct ParityFault {
    FaultKind::T kind;
    U8 replica;
    U64 cycle;
};

struct ParityCase {
    const char* schedule;
    Policy::T policy;
    U64 seed;
    F64 rate;
    U64 ticks;
    U32 kills;   //!< kill faults: each may cost up to three results (MRU-SYS-001)
    U32 nFaults;
    ParityFault faults[3];
    U64 useful;  //!< the demo's correct results
    U64 caught;  //!< the demo's caught disagreements ("detected")
    U64 wrong;   //!< the demo's wrong results
};

}  // namespace Mru

#endif
