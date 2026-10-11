// ======================================================================
// \title  FaultInjector.hpp
// \brief  Test builds only: applies faults between RedundancyManager and
//         the replicas, with the same upsets as the quorum demo for a seed
// ======================================================================

#ifndef Mru_FaultInjector_HPP
#define Mru_FaultInjector_HPP

#include "Mru/Components/FaultInjector/FaultInjectorComponentAc.hpp"
#include "Mru/Types/FppConstantsAc.hpp"

namespace Mru {

class FaultInjector final : public FaultInjectorComponentBase {
  public:
    //! Faults that can be scheduled at once
    static constexpr FwSizeType MAX_FAULTS = 32;

    //! The value a stuck replica returns, as in the demo
    static constexpr U64 STUCK_VALUE = 0xdeadbeefULL;

    //! Construct FaultInjector object
    explicit FaultInjector(const char* const compName);

    //! Destroy FaultInjector object
    ~FaultInjector() override;

    FaultInjector(const FaultInjector&) = delete;
    FaultInjector& operator=(const FaultInjector&) = delete;

  private:
    //! A request from RedundancyManager, on its thread, replica by replica
    void requestIn_handler(FwIndexType portNum, U64 cycle, const Mru::RequestKind& kind, const Mru::Upset& upset) override;

    //! A reply from a replica
    void replyIn_handler(FwIndexType portNum,
                         U64 cycle,
                         const Mru::RequestKind& kind,
                         U64 first,
                         U64 second) override;

    void INJECT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Mru::FaultKind& kind, U8 replica, U64 cycle) override;
    void CLEAR_FAULTS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 replica) override;
    void SET_UPSETS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, F64 rate, F64 commonMode, U64 seed) override;

    //! A scheduled fault
    struct Fault {
        FaultKind kind;
        U8 replica;
        U64 cycle;
        bool used;
        bool announced;
    };

    //! Earliest cycle of a fault of `kind` on `replica` that has started by `cycle`; false if none
    bool active(FaultKind::T kind, FwIndexType replica, U64 cycle);

    //! Whether a corrupt fault is set for exactly this cycle
    bool corruptAt(FwIndexType replica, U64 cycle);

    //! The demo's generator: xorshift64, and a uniform draw from its top 53 bits
    U64 next();
    F64 uniform();

    Fault m_faults[MAX_FAULTS];
    U64 m_rng;
    F64 m_rate;
    F64 m_commonMode;
    //! A hung replica draws once, on its first request after the hang, as in the demo
    bool m_hangDrawn[Replicas];
};

}  // namespace Mru

#endif
