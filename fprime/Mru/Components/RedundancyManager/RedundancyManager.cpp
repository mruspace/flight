// ======================================================================
// \title  RedundancyManager.cpp
// \brief  Runs the shrinking quorum, or fixed TMR, over three replicas.
//         Every decision comes from the quorum crate (MRU-RM-016).
//
// One cycle, in the order of the quorum demo's tick so that the same seed
// gives the same counts (MRU-SYS-001):
//   1. request a result from every ACTIVE slot (two results on the last
//      replica, on even cycles only), and the periodic known-answer test;
//   2. decide as soon as every expected answer is in, or at the next cycle
//      with what arrived; strikes, diagnosis and the periodic test follow;
//   3. resolve known-answer tests and admissions when their answers arrive,
//      or at the end of the next cycle if they do not.
// ======================================================================

#include "Mru/Components/RedundancyManager/RedundancyManager.hpp"

#include "Fw/Types/Assert.hpp"
#include "Os/File.hpp"
#include "Os/FileSystem.hpp"
#include "Utils/Hash/Hash.hpp"

namespace Mru {

namespace {

constexpr U32 STATE_MAGIC = 0x4D525553;  // "MRUS"
constexpr U8 STATE_VERSION = 1;
constexpr FwSizeType CRC_OFFSET = RedundancyManager::STATE_SIZE - 4;
constexpr U32 BITS_PER_BYTE = 8;
constexpr U64 BYTE_MASK = 0xff;

void putLe(U8* out, U64 value, FwSizeType bytes) {
    for (FwSizeType i = 0; i < bytes; i++) {
        out[i] = static_cast<U8>((value >> (BITS_PER_BYTE * i)) & BYTE_MASK);
    }
}

U64 getLe(const U8* in, FwSizeType bytes) {
    U64 value = 0;
    for (FwSizeType i = 0; i < bytes; i++) {
        value |= static_cast<U64>(in[i]) << (BITS_PER_BYTE * i);
    }
    return value;
}

U32 crcOf(const U8* data, FwSizeType size) {
    Utils::HashBuffer buffer;
    Utils::Hash::hash(data, size, buffer);
    return buffer.asBigEndianU32();
}

}  // namespace

RedundancyManager ::RedundancyManager(const char* const compName)
    : RedundancyManagerComponentBase(compName),
      m_policy(Policy::SHRINK),
      m_slots(),
      m_nextCycle(1),
      m_cycle(0),
      m_decided(true),
      m_expect(),
      m_got(),
      m_reply(),
      m_missed(),
      m_knownAnswer(0),
      m_katPurpose(),
      m_katCycle(),
      m_katGot(),
      m_katPassed(),
      m_lastMode(Mode::VOTE),
      m_halted(false),
      m_delivered(0),
      m_caught(0),
      m_retired(0),
      m_statePath(),
      m_configured(false),
      m_restored(false),
      m_announced(false),
      m_sinceSave(0) {
    for (FwIndexType i = 0; i < Replicas; i++) {
        this->m_slots[i] = SlotState::ACTIVE;
        this->m_expect[i] = false;
        this->m_got[i] = false;
        this->m_reply[i] = quorum_reply{QUORUM_ABSENT, 0, 0};
        this->m_missed[i] = 0;
        this->m_katPurpose[i] = KatPurpose::NONE;
        this->m_katCycle[i] = 0;
        this->m_katGot[i] = false;
        this->m_katPassed[i] = false;
    }
}

RedundancyManager ::~RedundancyManager() {}

void RedundancyManager ::configure(U64 knownAnswer, const char* statePath) {
    FW_ASSERT(statePath != nullptr);
    this->m_knownAnswer = knownAnswer;
    this->m_statePath = statePath;
    this->m_configured = true;
    this->m_restored = this->loadState();
}

// ----------------------------------------------------------------------
// Ports
// ----------------------------------------------------------------------

void RedundancyManager ::schedIn_handler(FwIndexType portNum, U32 context) {
    (void)portNum;
    (void)context;
    // configure() is part of the topology's setup, not input
    FW_ASSERT(this->m_configured);
    this->finishCycle();
    this->startCycle();

    Fw::ParamValid valid;
    const U32 savePeriod = this->paramGet_StateSavePeriod(valid);
    this->m_sinceSave++;
    if (savePeriod > 0 && this->m_sinceSave >= savePeriod) {
        this->saveState();
    }
    this->report();
}

void RedundancyManager ::replyIn_handler(FwIndexType portNum,
                                         U64 cycle,
                                         const Mru::RequestKind& kind,
                                         U64 first,
                                         U64 second) {
    const FwIndexType i = portNum;
    if (kind.e == RequestKind::KNOWN_ANSWER) {
        if (this->m_katPurpose[i] == KatPurpose::NONE || cycle != this->m_katCycle[i]) {
            return;  // not asked for, or too late
        }
        const bool passed = (first == this->m_knownAnswer);
        const KatPurpose purpose = this->m_katPurpose[i];
        switch (purpose) {
            case KatPurpose::DIAGNOSIS:
                this->m_katPurpose[i] = KatPurpose::NONE;
                if (passed) {
                    if (this->isConnected_healthClear_OutputPort(0)) {
                        this->healthClear_out(0, i);
                    }
                    this->log_ACTIVITY_LO_ReplicaCleared(static_cast<U8>(i), cycle);
                } else {
                    this->retire(i, RetireReason::KNOWN_ANSWER_FAILED);
                }
                break;
            case KatPurpose::ADMISSION:
                this->m_katPurpose[i] = KatPurpose::NONE;
                if (passed) {
                    this->m_slots[i] = SlotState::ACTIVE;
                    this->m_missed[i] = 0;
                    if (this->isConnected_healthClear_OutputPort(0)) {
                        this->healthClear_out(0, i);
                    }
                    this->log_ACTIVITY_HI_ReplicaAdmitted(static_cast<U8>(i), cycle);
                    this->saveState();
                } else {
                    this->enableOut_out(i, false);
                    this->log_WARNING_LO_AdmitFailed(static_cast<U8>(i), AdmitFailure::KNOWN_ANSWER_FAILED, cycle);
                }
                break;
            case KatPurpose::PERIODIC:
                this->m_katGot[i] = true;
                this->m_katPassed[i] = passed;
                this->decideIfComplete();
                break;
            default:
                break;
        }
        return;
    }

    if (cycle != this->m_cycle || this->m_decided || !this->m_expect[i] || this->m_slots[i] != SlotState::ACTIVE) {
        return;  // late, or from a replica no longer asked
    }
    this->m_reply[i] = (kind.e == RequestKind::COMPUTE_TWICE) ? quorum_reply{QUORUM_PAIR, first, second}
                                                              : quorum_reply{QUORUM_SINGLE, first, 0};
    this->m_got[i] = true;
    this->decideIfComplete();
}

void RedundancyManager ::pingIn_handler(FwIndexType portNum, U32 key) {
    (void)portNum;
    this->pingOut_out(0, key);
}

// ----------------------------------------------------------------------
// Commands
// ----------------------------------------------------------------------

void RedundancyManager ::SET_POLICY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Mru::Policy& policy) {
    this->m_policy = policy;
    this->log_ACTIVITY_HI_PolicySet(policy);
    this->saveState();
    this->report();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void RedundancyManager ::STANDBY_REPLICA_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 slot) {
    RefuseReason reason = RefuseReason::NO_SUCH_SLOT;
    bool refused = true;
    if (slot >= Replicas) {
        reason = RefuseReason::NO_SUCH_SLOT;
    } else if (this->m_slots[slot] != SlotState::ACTIVE) {
        reason = RefuseReason::WRONG_STATE;
    } else if (quorum_mode(static_cast<U8>(this->m_policy.e), this->activeCount() - 1) == QUORUM_HALTED) {
        reason = RefuseReason::WOULD_HALT;
    } else {
        refused = false;
    }
    if (refused) {
        this->log_WARNING_LO_SlotCommandRefused(slot, reason);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->m_slots[slot] = SlotState::STANDBY;
    this->m_expect[slot] = false;
    this->m_katPurpose[slot] = KatPurpose::NONE;
    this->enableOut_out(slot, false);
    this->log_ACTIVITY_HI_ReplicaStandby(slot, this->m_cycle);
    this->saveState();
    this->decideIfComplete();
    this->report();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void RedundancyManager ::ADMIT_REPLICA_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 slot) {
    RefuseReason reason = RefuseReason::NO_SUCH_SLOT;
    bool refused = true;
    bool busy = false;
    for (FwIndexType i = 0; i < Replicas; i++) {
        busy = busy || (this->m_katPurpose[i] == KatPurpose::ADMISSION);
    }
    if (slot >= Replicas) {
        reason = RefuseReason::NO_SUCH_SLOT;
    } else if (this->m_slots[slot] == SlotState::ACTIVE) {
        reason = RefuseReason::WRONG_STATE;
    } else if (busy) {
        reason = RefuseReason::BUSY;
    } else {
        refused = false;
    }
    if (refused) {
        this->log_WARNING_LO_SlotCommandRefused(slot, reason);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->enableOut_out(slot, true);
    this->log_ACTIVITY_HI_AdmitRequested(slot, this->m_cycle);
    this->requestKnownAnswer(slot, KatPurpose::ADMISSION);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Cycle steps
// ----------------------------------------------------------------------

void RedundancyManager ::finishCycle() {
    if (this->m_cycle == 0) {
        return;
    }
    if (!this->m_decided) {
        this->decide();
    }
    for (FwIndexType i = 0; i < Replicas; i++) {
        if (!this->m_expect[i] || this->m_slots[i] != SlotState::ACTIVE) {
            continue;
        }
        if (this->m_got[i]) {
            this->m_missed[i] = 0;
        } else {
            this->m_missed[i]++;
            if (this->m_missed[i] >= MISSES_TO_LOSE) {
                this->retire(i, RetireReason::LOST);
            }
        }
    }
    // known-answer tests asked before this cycle had a full cycle to answer
    for (FwIndexType i = 0; i < Replicas; i++) {
        if (this->m_katCycle[i] >= this->m_cycle) {
            continue;
        }
        if (this->m_katPurpose[i] == KatPurpose::DIAGNOSIS) {
            this->retire(i, RetireReason::KNOWN_ANSWER_MISSING);
        } else if (this->m_katPurpose[i] == KatPurpose::ADMISSION) {
            this->m_katPurpose[i] = KatPurpose::NONE;
            this->enableOut_out(i, false);
            this->log_WARNING_LO_AdmitFailed(static_cast<U8>(i), AdmitFailure::KNOWN_ANSWER_MISSING, this->m_cycle);
        }
    }
}

void RedundancyManager ::startCycle() {
    if (!this->m_announced) {
        this->m_announced = true;
        if (this->m_restored) {
            this->log_ACTIVITY_HI_StateRestored(this->m_policy, SlotStates(this->m_slots), this->m_nextCycle);
        } else {
            this->log_WARNING_LO_StateNotRestored();
        }
        for (FwIndexType i = 0; i < Replicas; i++) {
            if (this->m_slots[i] != SlotState::ACTIVE) {
                this->enableOut_out(i, false);
            }
        }
    }

    this->m_cycle = this->m_nextCycle;
    this->m_nextCycle++;
    this->m_decided = false;
    for (FwIndexType i = 0; i < Replicas; i++) {
        this->m_expect[i] = false;
        this->m_got[i] = false;
        this->m_reply[i] = quorum_reply{QUORUM_ABSENT, 0, 0};
    }

    const U8 mode = quorum_mode(static_cast<U8>(this->m_policy.e), this->activeCount());
    if (mode != static_cast<U8>(this->m_lastMode.e)) {
        const Mode current = static_cast<Mode::T>(mode);
        this->log_ACTIVITY_HI_ModeChanged(this->m_lastMode, current, this->m_cycle);
        this->m_lastMode = current;
    }
    if (mode == QUORUM_HALTED) {
        if (!this->m_halted) {
            this->m_halted = true;
            this->log_WARNING_HI_Halted(this->m_cycle);
        }
        this->m_decided = true;
        return;
    }
    this->m_halted = false;

    // one replica computes everything twice, so it delivers every other cycle
    if (mode == QUORUM_SELF_CHECK && (this->m_cycle % 2) == 1) {
        this->m_decided = true;
        return;
    }
    const RequestKind kind = (mode == QUORUM_SELF_CHECK) ? RequestKind::COMPUTE_TWICE : RequestKind::COMPUTE;
    for (FwIndexType i = 0; i < Replicas; i++) {
        if (this->m_slots[i] == SlotState::ACTIVE) {
            this->m_expect[i] = true;
            this->requestOut_out(i, this->m_cycle, kind, Upset(false, false, 0, 0));
        }
    }

    // on one replica, a periodic known-answer test catches a stuck fault
    Fw::ParamValid valid;
    U32 period = this->paramGet_KatPeriod(valid);
    period = (period == 0) ? 1 : period;
    if (this->m_policy == Policy::SHRINK && mode == QUORUM_SELF_CHECK && (this->m_cycle % period) == 0) {
        for (FwIndexType i = 0; i < Replicas; i++) {
            if (this->m_slots[i] == SlotState::ACTIVE) {
                this->requestKnownAnswer(i, KatPurpose::PERIODIC);
            }
        }
    }
}

void RedundancyManager ::decideIfComplete() {
    if (this->m_cycle == 0 || this->m_decided) {
        return;
    }
    for (FwIndexType i = 0; i < Replicas; i++) {
        if (this->m_expect[i] && this->m_slots[i] == SlotState::ACTIVE && !this->m_got[i]) {
            return;
        }
        if (this->m_katPurpose[i] == KatPurpose::PERIODIC && !this->m_katGot[i]) {
            return;
        }
    }
    this->decide();
}

void RedundancyManager ::decide() {
    this->m_decided = true;
    quorum_reply replies[Replicas];
    for (FwIndexType i = 0; i < Replicas; i++) {
        const bool counted = this->m_expect[i] && this->m_got[i] && this->m_slots[i] == SlotState::ACTIVE;
        replies[i] = counted ? this->m_reply[i] : quorum_reply{QUORUM_ABSENT, 0, 0};
    }
    const quorum_verdict v = quorum_decide(static_cast<U8>(this->m_policy.e), replies);
    U8 outcome = v.outcome;

    if (this->m_policy == Policy::SHRINK) {
        // three outvotes in the window: test the replica (MRU-RM-006)
        for (FwIndexType i = 0; i < Replicas; i++) {
            if (v.dissent[i] && this->isConnected_healthStrike_OutputPort(0) &&
                this->healthStrike_out(0, i, this->m_cycle)) {
                this->requestKnownAnswer(i, KatPurpose::DIAGNOSIS);
            }
        }
        // two disagree and nothing says which: test both (MRU-RM-007)
        if (v.diagnose) {
            for (FwIndexType i = 0; i < Replicas; i++) {
                if (this->m_slots[i] == SlotState::ACTIVE) {
                    this->requestKnownAnswer(i, KatPurpose::DIAGNOSIS);
                }
            }
        }
        // the periodic test on the last replica: a failure withholds this cycle (MRU-RM-008)
        for (FwIndexType i = 0; i < Replicas; i++) {
            if (this->m_katPurpose[i] != KatPurpose::PERIODIC) {
                continue;
            }
            this->m_katPurpose[i] = KatPurpose::NONE;
            if (!this->m_katGot[i]) {
                this->retire(i, RetireReason::PERIODIC_TEST_MISSING);
                outcome = QUORUM_DETECTED;
            } else if (!this->m_katPassed[i]) {
                this->retire(i, RetireReason::PERIODIC_TEST_FAILED);
                outcome = QUORUM_DETECTED;
            }
        }
    }

    if (outcome == QUORUM_DELIVER) {
        this->m_delivered++;
        if (this->isConnected_resultOut_OutputPort(0)) {
            this->resultOut_out(0, this->m_cycle, v.value);
        }
    } else if (outcome == QUORUM_DETECTED) {
        this->m_caught++;
        this->log_WARNING_LO_DisagreementCaught(this->m_cycle, static_cast<Mode::T>(v.mode));
    }
}

void RedundancyManager ::requestKnownAnswer(FwIndexType slot, KatPurpose purpose) {
    if (this->m_katPurpose[slot] != KatPurpose::NONE) {
        return;  // one test at a time per replica
    }
    this->m_katPurpose[slot] = purpose;
    this->m_katCycle[slot] = this->m_cycle;
    this->m_katGot[slot] = false;
    this->m_katPassed[slot] = false;
    if (purpose == KatPurpose::DIAGNOSIS) {
        this->log_ACTIVITY_LO_DiagnosisRequested(static_cast<U8>(slot), this->m_cycle);
    }
    this->requestOut_out(slot, this->m_cycle, RequestKind::KNOWN_ANSWER, Upset(false, false, 0, 0));
}

void RedundancyManager ::retire(FwIndexType slot, RetireReason reason) {
    this->m_slots[slot] = SlotState::RETIRED;
    this->m_expect[slot] = false;
    this->m_katPurpose[slot] = KatPurpose::NONE;
    this->m_retired++;
    this->enableOut_out(slot, false);
    this->log_WARNING_HI_ReplicaRetired(static_cast<U8>(slot), reason, this->m_cycle);
    this->saveState();
}

U32 RedundancyManager ::activeCount() const {
    U32 n = 0;
    for (FwIndexType i = 0; i < Replicas; i++) {
        n += (this->m_slots[i] == SlotState::ACTIVE) ? 1 : 0;
    }
    return n;
}

void RedundancyManager ::report() {
    this->tlmWrite_CurrentPolicy(this->m_policy);
    this->tlmWrite_CurrentMode(this->m_lastMode);
    this->tlmWrite_LiveReplicas(static_cast<U8>(this->activeCount()));
    this->tlmWrite_Delivered(this->m_delivered);
    this->tlmWrite_Caught(this->m_caught);
    this->tlmWrite_Retired(this->m_retired);
    this->tlmWrite_Slots(SlotStates(this->m_slots));
}

// ----------------------------------------------------------------------
// State file (MRU-RM-022)
// ----------------------------------------------------------------------

void RedundancyManager ::saveState() {
    this->m_sinceSave = 0;
    if (!this->m_configured || this->m_statePath.length() == 0) {
        return;
    }
    U8 record[STATE_SIZE] = {};
    putLe(&record[0], STATE_MAGIC, 4);
    record[4] = STATE_VERSION;
    record[5] = static_cast<U8>(this->m_policy.e);
    for (FwIndexType i = 0; i < Replicas; i++) {
        record[6 + i] = static_cast<U8>(this->m_slots[i].e);
    }
    putLe(&record[6 + Replicas], this->m_nextCycle, 8);
    putLe(&record[CRC_OFFSET], crcOf(record, CRC_OFFSET), 4);

    // write a new file, flush it, and rename it over the old one: a power
    // cut leaves either the old state or the new one
    Fw::FileNameString tmp;
    tmp.format("%s.tmp", this->m_statePath.toChar());
    Os::File file;
    bool ok = file.open(tmp.toChar(), Os::File::OPEN_CREATE, Os::File::OVERWRITE) == Os::File::OP_OK;
    if (ok) {
        FwSizeType size = STATE_SIZE;
        ok = file.write(record, size) == Os::File::OP_OK && size == STATE_SIZE;
        ok = ok && (file.flush() == Os::File::OP_OK);
        file.close();
    }
    ok = ok && (Os::FileSystem::rename(tmp.toChar(), this->m_statePath.toChar()) == Os::FileSystem::OP_OK);
    if (!ok) {
        this->log_WARNING_HI_StateSaveFailed(this->m_cycle);
    }
}

bool RedundancyManager ::loadState() {
    if (this->m_statePath.length() == 0) {
        return false;
    }
    Os::File file;
    if (file.open(this->m_statePath.toChar(), Os::File::OPEN_READ) != Os::File::OP_OK) {
        return false;
    }
    U8 record[STATE_SIZE] = {};
    FwSizeType size = STATE_SIZE;
    const bool read = file.read(record, size) == Os::File::OP_OK && size == STATE_SIZE;
    file.close();
    if (!read || getLe(&record[0], 4) != STATE_MAGIC || record[4] != STATE_VERSION ||
        getLe(&record[CRC_OFFSET], 4) != crcOf(record, CRC_OFFSET) || record[5] > Policy::TMR) {
        return false;
    }
    for (FwIndexType i = 0; i < Replicas; i++) {
        if (record[6 + i] > SlotState::RETIRED) {
            return false;
        }
    }
    this->m_policy = static_cast<Policy::T>(record[5]);
    U8 retired = 0;
    for (FwIndexType i = 0; i < Replicas; i++) {
        this->m_slots[i] = static_cast<SlotState::T>(record[6 + i]);
        retired += (this->m_slots[i] == SlotState::RETIRED) ? 1 : 0;
    }
    this->m_retired = retired;
    this->m_nextCycle = getLe(&record[6 + Replicas], 8);
    return true;
}

}  // namespace Mru
