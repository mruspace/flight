// ======================================================================
// \title  RunRecorderTester.cpp
// \brief  Unit tests of RunRecorder
// ======================================================================

#include "RunRecorderTester.hpp"
#include "Mru/Components/RedundancyManager/RedundancyManagerComponentAc.hpp"

namespace Mru {

namespace {

//! The manager's channel IDs, as the component reads them
class ManagerChannels : public RedundancyManagerComponentBase {
  public:
    using RedundancyManagerComponentBase::CHANNELID_CAUGHT;
    using RedundancyManagerComponentBase::CHANNELID_CURRENTMODE;
    using RedundancyManagerComponentBase::CHANNELID_CURRENTPOLICY;
    using RedundancyManagerComponentBase::CHANNELID_LIVEREPLICAS;
    using RedundancyManagerComponentBase::CHANNELID_SLOTS;
};

}  // namespace

RunRecorderTester ::RunRecorderTester()
    : RunRecorderGTestBase("RunRecorderTester", RunRecorderTester::MAX_HISTORY_SIZE), component("RunRecorder") {
    this->initComponents();
    this->connectPorts();
    this->component.configure(MANAGER_BASE);
}

RunRecorderTester ::~RunRecorderTester() {
    this->component.deinit();
}

U64 RunRecorderTester ::correctFor(U64 cycle) {
    U64 block[ReplicaHost::BLOCK_WORDS];
    return ReplicaHost::compute(block, cycle, false, 0, 0);
}

Fw::TlmValid RunRecorderTester ::from_tlmGet_handler(FwIndexType portNum,
                                                     FwChanIdType id,
                                                     Fw::Time& timeTag,
                                                     Fw::TlmBuffer& val) {
    (void)portNum;
    (void)timeTag;
    val.resetSer();
    const FwChanIdType local = id - static_cast<FwChanIdType>(MANAGER_BASE);
    if (local == ManagerChannels::CHANNELID_CAUGHT) {
        (void)val.serializeFrom(static_cast<U64>(7));
    } else if (local == ManagerChannels::CHANNELID_LIVEREPLICAS) {
        (void)val.serializeFrom(static_cast<U8>(1));
    } else if (local == ManagerChannels::CHANNELID_CURRENTMODE) {
        (void)val.serializeFrom(Mode(Mode::SELF_CHECK));
    } else if (local == ManagerChannels::CHANNELID_CURRENTPOLICY) {
        (void)val.serializeFrom(Policy(Policy::TMR));
    } else if (local == ManagerChannels::CHANNELID_SLOTS) {
        SlotStates slots;
        slots[0] = SlotState::RETIRED;
        slots[1] = SlotState::RETIRED;
        slots[2] = SlotState::ACTIVE;
        (void)val.serializeFrom(slots);
    } else {
        return Fw::TlmValid::INVALID;
    }
    return Fw::TlmValid::VALID;
}

void RunRecorderTester ::countsResults() {
    this->invoke_to_resultIn(0, 5, correctFor(5));
    ASSERT_TLM_Correct_SIZE(1);
    ASSERT_TLM_Correct(0, 1);
    ASSERT_EVENTS_SIZE(0);
    this->invoke_to_resultIn(0, 6, correctFor(6) ^ 1);
    ASSERT_EVENTS_WrongResult_SIZE(1);
    ASSERT_EVENTS_WrongResult(0, 6);
    ASSERT_TLM_Wrong_SIZE(1);
    ASSERT_TLM_Wrong(0, 1);
    this->invoke_to_resultIn(0, 7, correctFor(7));
    const RunRecorder::Totals t = this->component.totals();
    ASSERT_EQ(t.correct, 2u);
    ASSERT_EQ(t.wrong, 1u);
    ASSERT_EQ(t.lastCorrect, 7u);
}

void RunRecorderTester ::readsManagerTelemetry() {
    const RunRecorder::Totals t = this->component.totals();
    ASSERT_EQ(t.caught, 7u);
    ASSERT_EQ(t.live, 1u);
    ASSERT_EQ(t.mode, Mode::SELF_CHECK);
    ASSERT_EQ(t.policy, Policy::TMR);
    ASSERT_EQ(t.slots[0], SlotState::RETIRED);
    ASSERT_EQ(t.slots[1], SlotState::RETIRED);
    ASSERT_EQ(t.slots[2], SlotState::ACTIVE);
}

void RunRecorderTester ::countsCyclesAndSequence() {
    for (U32 i = 0; i < 3; i++) {
        this->invoke_to_schedIn(0, 0);
    }
    RunRecorder::Totals t = this->component.totals();
    ASSERT_EQ(t.cycles, 3u);
    ASSERT_FALSE(t.seqDone);
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::EXECUTION_ERROR);
    t = this->component.totals();
    ASSERT_TRUE(t.seqDone);
    ASSERT_FALSE(t.seqOk);
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::OK);
    ASSERT_TRUE(this->component.totals().seqOk);
}

}  // namespace Mru
