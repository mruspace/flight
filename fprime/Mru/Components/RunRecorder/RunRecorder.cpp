// ======================================================================
// \title  RunRecorder.cpp
// \brief  Experiment builds only: checks delivered results, keeps the counts
// ======================================================================

#include "Mru/Components/RunRecorder/RunRecorder.hpp"
#include "Mru/Components/RedundancyManager/RedundancyManagerComponentAc.hpp"
#include "Fw/Types/String.hpp"

namespace Mru {

namespace {

//! The manager's channel IDs, relative to its base ID. The generated base
//! class keeps them protected; this class is never instantiated.
class ManagerChannels : public RedundancyManagerComponentBase {
  public:
    using RedundancyManagerComponentBase::CHANNELID_CAUGHT;
    using RedundancyManagerComponentBase::CHANNELID_CURRENTMODE;
    using RedundancyManagerComponentBase::CHANNELID_CURRENTPOLICY;
    using RedundancyManagerComponentBase::CHANNELID_LATE;
    using RedundancyManagerComponentBase::CHANNELID_LIVEREPLICAS;
    using RedundancyManagerComponentBase::CHANNELID_SLOTS;
};

}  // namespace

RunRecorder ::RunRecorder(const char* const compName)
    : RunRecorderComponentBase(compName), m_managerIdBase(0), m_cycleHook(nullptr), m_totals(), m_block() {}

RunRecorder ::~RunRecorder() {}

void RunRecorder ::configure(FwIdType managerIdBase) {
    this->m_managerIdBase = managerIdBase;
}

void RunRecorder ::resultIn_handler(FwIndexType portNum, U64 cycle, U64 value) {
    (void)portNum;
    // the ground truth: the same task, computed here without any fault
    if (value == Payload::compute(this->m_block, cycle, false, 0, 0)) {
        this->m_totals.correct++;
        this->m_totals.lastCorrect = cycle;
        this->tlmWrite_Correct(this->m_totals.correct);
    } else {
        this->m_totals.wrong++;
        this->log_WARNING_HI_WrongResult(cycle);
        this->tlmWrite_Wrong(this->m_totals.wrong);
    }
}

void RunRecorder ::setCycleHook(void (*hook)(U64 cycle)) {
    this->m_cycleHook = hook;
}

void RunRecorder ::schedIn_handler(FwIndexType portNum, U32 context) {
    (void)portNum;
    (void)context;
    this->m_totals.cycles++;
    if (this->m_cycleHook != nullptr) {
        this->m_cycleHook(this->m_totals.cycles);
    }
}

void RunRecorder ::seqDoneIn_handler(FwIndexType portNum,
                                     FwOpcodeType opCode,
                                     U32 cmdSeq,
                                     const Fw::CmdResponse& response) {
    (void)portNum;
    (void)opCode;
    (void)cmdSeq;
    this->m_totals.seqDone = true;
    this->m_totals.seqOk = (response == Fw::CmdResponse::OK);
}

bool RunRecorder ::readChannel(FwChanIdType localId, Fw::TlmBuffer& val) {
    if (!this->isConnected_tlmGet_OutputPort(0)) {
        return false;
    }
    Fw::Time time;
    val.resetSer();
    (void)this->tlmGet_out(0, static_cast<FwChanIdType>(this->m_managerIdBase + localId), time, val);
    return val.getSize() > 0;
}

void RunRecorder ::startSequence(const char* path) {
    if (this->isConnected_seqRunOut_OutputPort(0)) {
        this->seqRunOut_out(0, Fw::String(path), Svc::SeqArgs());
    }
}

RunRecorder::Totals RunRecorder ::totals() {
    // read the channels outside the lock: the telemetry database has its own
    Totals t;
    Fw::TlmBuffer val;
    using Base = ManagerChannels;
    U64 caught = 0;
    U64 late = 0;
    U8 live = 0;
    Mode mode;
    Policy policy;
    SlotStates slots;
    if (this->readChannel(Base::CHANNELID_CAUGHT, val)) {
        (void)val.deserializeTo(caught);
    }
    if (this->readChannel(Base::CHANNELID_LATE, val)) {
        (void)val.deserializeTo(late);
    }
    if (this->readChannel(Base::CHANNELID_LIVEREPLICAS, val)) {
        (void)val.deserializeTo(live);
    }
    if (this->readChannel(Base::CHANNELID_CURRENTMODE, val)) {
        (void)val.deserializeTo(mode);
    }
    if (this->readChannel(Base::CHANNELID_CURRENTPOLICY, val)) {
        (void)val.deserializeTo(policy);
    }
    if (this->readChannel(Base::CHANNELID_SLOTS, val)) {
        (void)val.deserializeTo(slots);
    }
    this->lock();
    t = this->m_totals;
    this->unLock();
    t.caught = caught;
    t.late = late;
    t.live = live;
    t.mode = mode;
    t.policy = policy;
    t.slots = slots;
    return t;
}

}  // namespace Mru
