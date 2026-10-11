// ======================================================================
// \title  FaultInjector.cpp
// \brief  Test builds only: applies faults between RedundancyManager and
//         the replicas, with the same upsets as the quorum demo for a seed
// ======================================================================

#include "Mru/Components/FaultInjector/FaultInjector.hpp"

namespace Mru {

// definitions of the class constants (C++14)
constexpr FwSizeType FaultInjector::MAX_FAULTS;
constexpr U64 FaultInjector::STUCK_VALUE;

namespace {

constexpr U64 SEED_MULTIPLIER = 0x9E3779B97F4A7C15ULL;  // as the demo seeds its generator
constexpr U64 SEED_OFFSET = 7;
constexpr U32 UNIFORM_SHIFT = 11;                        // keep the top 53 bits
constexpr F64 UNIFORM_SCALE = 1.0 / 9007199254740992.0;  // 2^-53
constexpr U64 BIT_MASK = 63;                             // bit within a 64-bit word
constexpr U32 WORD_SHIFT = 6;
constexpr U64 BLOCK_WORDS = 512;  // the replica's working block, as ReplicaHost::BLOCK_WORDS

}  // namespace

FaultInjector ::FaultInjector(const char* const compName)
    : FaultInjectorComponentBase(compName), m_faults(), m_rng(SEED_OFFSET), m_rate(0.0), m_commonMode(0.0), m_hangDrawn() {
    for (FwSizeType i = 0; i < MAX_FAULTS; i++) {
        this->m_faults[i] = Fault{FaultKind(), 0, 0, false, false};
    }
    for (FwIndexType i = 0; i < Replicas; i++) {
        this->m_hangDrawn[i] = false;
    }
}

FaultInjector ::~FaultInjector() {}

U64 FaultInjector ::next() {
    this->m_rng ^= this->m_rng << 13;
    this->m_rng ^= this->m_rng >> 7;
    this->m_rng ^= this->m_rng << 17;
    return this->m_rng;
}

F64 FaultInjector ::uniform() {
    return static_cast<F64>(this->next() >> UNIFORM_SHIFT) * UNIFORM_SCALE;
}

bool FaultInjector ::active(FaultKind::T kind, FwIndexType replica, U64 cycle) {
    bool found = false;
    for (FwSizeType i = 0; i < MAX_FAULTS; i++) {
        Fault& f = this->m_faults[i];
        if (f.used && f.kind.e == kind && f.replica == replica && f.cycle <= cycle) {
            if (!f.announced) {
                f.announced = true;
                this->log_WARNING_LO_FaultApplied(f.kind, f.replica, cycle);
            }
            found = true;
        }
    }
    return found;
}

bool FaultInjector ::corruptAt(FwIndexType replica, U64 cycle) {
    bool found = false;
    for (FwSizeType i = 0; i < MAX_FAULTS; i++) {
        Fault& f = this->m_faults[i];
        if (f.used && f.kind.e == FaultKind::CORRUPT && f.replica == replica && f.cycle == cycle) {
            if (!f.announced) {
                f.announced = true;
                this->log_WARNING_LO_FaultApplied(f.kind, f.replica, cycle);
            }
            found = true;
        }
    }
    return found;
}

void FaultInjector ::requestIn_handler(FwIndexType portNum,
                                       U64 cycle,
                                       const Mru::RequestKind& kind,
                                       const Mru::Upset& upset) {
    (void)upset;  // the manager never sets one; this component does
    const FwIndexType i = portNum;
    const bool killed = this->active(FaultKind::KILL, i, cycle);
    const bool hung = this->active(FaultKind::HANG, i, cycle);

    // known-answer tests draw nothing, as in the demo
    if (kind.e == RequestKind::KNOWN_ANSWER) {
        if (!killed && !hung) {
            this->requestOut_out(i, cycle, kind, Upset(false, false, 0, 0));
        }
        return;
    }
    // a killed replica is gone: no draws. A hung one is asked once more
    // (with its draws), as in the demo, and never answers.
    if (killed) {
        return;
    }
    if (hung && this->m_hangDrawn[i]) {
        return;
    }

    // the demo's draws for one ask: position, then whether, then common mode
    const U64 draw = this->next();
    const U8 bit = static_cast<U8>(draw & BIT_MASK);
    const U16 word = static_cast<U16>((draw >> WORD_SHIFT) % BLOCK_WORDS);
    const bool corrupt = this->corruptAt(i, cycle);
    const bool upsetNow = corrupt || ((this->m_rate > 0.0) && (this->uniform() < this->m_rate));
    bool second = false;
    if (upsetNow && (kind.e == RequestKind::COMPUTE_TWICE)) {
        second = this->uniform() < this->m_commonMode;
    }

    if (hung) {
        this->m_hangDrawn[i] = true;
        return;
    }
    this->requestOut_out(i, cycle, kind, Upset(upsetNow, second, word, bit));
}

void FaultInjector ::replyIn_handler(FwIndexType portNum,
                                     U64 cycle,
                                     const Mru::RequestKind& kind,
                                     U64 first,
                                     U64 second) {
    if (this->active(FaultKind::STUCK, portNum, cycle)) {
        first = STUCK_VALUE;
        second = STUCK_VALUE;
    }
    this->replyOut_out(portNum, cycle, kind, first, second);
}

void FaultInjector ::INJECT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Mru::FaultKind& kind, U8 replica, U64 cycle) {
    if (replica < Replicas) {
        for (FwSizeType i = 0; i < MAX_FAULTS; i++) {
            if (!this->m_faults[i].used) {
                this->m_faults[i] = Fault{kind, replica, cycle, true, false};
                this->log_ACTIVITY_HI_FaultScheduled(kind, replica, cycle);
                this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
                return;
            }
        }
    }
    this->log_WARNING_LO_FaultRefused(replica);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
}

void FaultInjector ::CLEAR_FAULTS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 replica) {
    if (replica >= Replicas) {
        this->log_WARNING_LO_FaultRefused(replica);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    for (FwSizeType i = 0; i < MAX_FAULTS; i++) {
        if (this->m_faults[i].replica == replica) {
            this->m_faults[i].used = false;
        }
    }
    this->m_hangDrawn[replica] = false;
    this->log_ACTIVITY_HI_FaultsCleared(replica);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void FaultInjector ::SET_UPSETS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, F64 rate, F64 commonMode, U64 seed) {
    if (!(rate >= 0.0 && rate <= 1.0 && commonMode >= 0.0 && commonMode <= 1.0)) {
        this->log_WARNING_LO_UpsetsRefused(rate, commonMode);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_rate = rate;
    this->m_commonMode = commonMode;
    this->m_rng = (seed * SEED_MULTIPLIER) + SEED_OFFSET;
    this->log_ACTIVITY_HI_UpsetsSet(rate, commonMode, seed);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Mru
