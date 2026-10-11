// ======================================================================
// \title  RedundancyManager.hpp
// \brief  Runs the shrinking quorum, or fixed TMR, over three replicas.
//         Every decision comes from the quorum crate (MRU-RM-016).
// ======================================================================

#ifndef Mru_RedundancyManager_HPP
#define Mru_RedundancyManager_HPP

#include "Fw/Types/FileNameString.hpp"
#include "Mru/Components/RedundancyManager/RedundancyManagerComponentAc.hpp"
#include "Mru/Types/FppConstantsAc.hpp"
#include "quorum.h"

namespace Mru {

class RedundancyManager final : public RedundancyManagerComponentBase {
  public:
    //! Cycles a replica may miss in a row before it is lost (D3)
    static constexpr U8 MISSES_TO_LOSE = 3;

    //! Bytes in the state file: magic, version, policy, slots, next cycle, CRC
    static constexpr FwSizeType STATE_SIZE = 4 + 1 + 1 + Replicas + 8 + 4;

    //! Construct RedundancyManager object
    explicit RedundancyManager(const char* const compName);

    //! Destroy RedundancyManager object
    ~RedundancyManager() override;

    RedundancyManager(const RedundancyManager&) = delete;
    RedundancyManager& operator=(const RedundancyManager&) = delete;

    //! Set the known answer the payload gives (D7) and the state file
    //! (MRU-RM-022), and read the state file if it is valid. Call before the
    //! first cycle. The result is reported on the first cycle, when the ports
    //! are connected.
    void configure(U64 knownAnswer, const char* statePath);

  private:
    // ----------------------------------------------------------------------
    // Handlers
    // ----------------------------------------------------------------------

    void schedIn_handler(FwIndexType portNum, U32 context) override;
    void replyIn_handler(FwIndexType portNum,
                         U64 cycle,
                         const Mru::RequestKind& kind,
                         U64 first,
                         U64 second) override;
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void SET_POLICY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Mru::Policy& policy) override;
    void STANDBY_REPLICA_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 slot) override;
    void ADMIT_REPLICA_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 slot) override;

    // ----------------------------------------------------------------------
    // Cycle steps
    // ----------------------------------------------------------------------

    //! Why a known-answer test was asked for
    enum class KatPurpose : U8 { NONE, DIAGNOSIS, PERIODIC, ADMISSION };

    //! Close the cycle in flight: decide it if not yet decided, and resolve
    //! answers that did not arrive (MRU-RM-002, 009, 010, 020)
    void finishCycle();

    //! Start the next cycle: mode, requests, periodic test (MRU-RM-001, 004, 008, 012)
    void startCycle();

    //! Decide the cycle in flight from the replies received (MRU-RM-002 to 008)
    void decide();

    //! Decide as soon as every expected answer of the cycle is in
    void decideIfComplete();

    //! Ask a replica for its known answer
    void requestKnownAnswer(FwIndexType slot, KatPurpose purpose);

    //! Retire a replica (MRU-RM-009 to 011)
    void retire(FwIndexType slot, RetireReason reason);

    //! Number of ACTIVE slots
    U32 activeCount() const;

    //! Report telemetry (MRU-RM-014, 021)
    void report();

    //! Save the state file (MRU-RM-022)
    void saveState();

    //! Read the state file; true if it was valid and applied
    bool loadState();

    // ----------------------------------------------------------------------
    // State
    // ----------------------------------------------------------------------

    Policy m_policy;
    SlotState m_slots[Replicas];
    U64 m_nextCycle;  //!< the next cycle to request; cycles start at 1
    U64 m_cycle;      //!< the cycle in flight; 0 when none
    bool m_decided;   //!< the cycle in flight was decided

    // replies of the cycle in flight
    bool m_expect[Replicas];
    bool m_got[Replicas];
    quorum_reply m_reply[Replicas];
    U8 m_missed[Replicas];

    // known-answer tests
    U64 m_knownAnswer;
    KatPurpose m_katPurpose[Replicas];
    U64 m_katCycle[Replicas];
    bool m_katGot[Replicas];
    bool m_katPassed[Replicas];

    Mode m_lastMode;
    bool m_halted;
    U64 m_delivered;
    U64 m_caught;
    U8 m_retired;

    // state file
    Fw::FileNameString m_statePath;
    bool m_configured;
    bool m_restored;
    bool m_announced;
    U32 m_sinceSave;
};

}  // namespace Mru

#endif
