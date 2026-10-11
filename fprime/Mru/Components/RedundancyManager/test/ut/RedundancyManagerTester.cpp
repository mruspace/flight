// ======================================================================
// \title  RedundancyManagerTester.cpp
// \brief  Unit tests of RedundancyManager
// ======================================================================

#include "RedundancyManagerTester.hpp"

#include "Os/File.hpp"
#include "Os/FileSystem.hpp"

namespace Mru {

constexpr U64 RedundancyManagerTester::KNOWN_ANSWER;

namespace {
constexpr U64 GOOD = 1000;
constexpr U64 BAD = 9999;
}  // namespace

RedundancyManagerTester ::RedundancyManagerTester(const char* statePath, bool fresh, bool load)
    : RedundancyManagerGTestBase("RedundancyManagerTester", RedundancyManagerTester::MAX_HISTORY_SIZE),
      component("RedundancyManager"),
      m_statePath(statePath),
      m_strikeAnswer(false),
      m_enabled(),
      m_requestPort() {
    for (FwIndexType i = 0; i < Replicas; i++) {
        this->m_enabled[i] = -1;
    }
    if (fresh) {
        (void)Os::FileSystem::removeFile(statePath);
    }
    this->initComponents();
    this->connectPorts();
    if (load) {
        this->component.loadParameters();
    }
    this->component.configure(KNOWN_ANSWER, statePath);
}

RedundancyManagerTester ::~RedundancyManagerTester() {
    this->component.deinit();
    (void)Os::FileSystem::removeFile(this->m_statePath);
}

// ----------------------------------------------------------------------
// Watched ports
// ----------------------------------------------------------------------

bool RedundancyManagerTester ::from_healthStrike_handler(FwIndexType portNum, FwIndexType replica, U64 cycle) {
    (void)RedundancyManagerGTestBase::from_healthStrike_handler(portNum, replica, cycle);
    return this->m_strikeAnswer;
}

void RedundancyManagerTester ::from_enableOut_handler(FwIndexType portNum, bool enabled) {
    this->m_enabled[portNum] = enabled ? 1 : 0;
    RedundancyManagerGTestBase::from_enableOut_handler(portNum, enabled);
}

void RedundancyManagerTester ::from_requestOut_handler(FwIndexType portNum,
                                                       U64 cycle,
                                                       const Mru::RequestKind& kind,
                                                       const Mru::Upset& upset) {
    const FwSizeType n = this->fromPortHistory_requestOut->size();
    if (n < MAX_HISTORY_SIZE) {
        this->m_requestPort[n] = portNum;
    }
    RedundancyManagerGTestBase::from_requestOut_handler(portNum, cycle, kind, upset);
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

void RedundancyManagerTester ::cycle() {
    this->clearHistory();
    for (FwIndexType i = 0; i < Replicas; i++) {
        this->m_enabled[i] = -1;
    }
    this->invoke_to_schedIn(0, 0);
    this->component.doDispatch();
}

void RedundancyManagerTester ::reply(FwIndexType slot, U64 cycle, RequestKind::T kind, U64 first, U64 second) {
    this->clearHistory();
    this->invoke_to_replyIn(slot, cycle, kind, first, second);
    this->component.doDispatch();
}

void RedundancyManagerTester ::replyAll(U64 cycle, U64 a, U64 b, U64 c) {
    this->invoke_to_replyIn(0, cycle, RequestKind::COMPUTE, a, 0);
    this->invoke_to_replyIn(1, cycle, RequestKind::COMPUTE, b, 0);
    this->clearHistory();
    this->invoke_to_replyIn(2, cycle, RequestKind::COMPUTE, c, 0);
    this->component.doDispatch();
    this->component.doDispatch();
    this->component.doDispatch();
}

void RedundancyManagerTester ::standbyCmd(U8 slot) {
    this->clearHistory();
    this->sendCmd_STANDBY_REPLICA(TEST_INSTANCE_ID, 0, slot);
    this->component.doDispatch();
}

void RedundancyManagerTester ::admitCmd(U8 slot) {
    this->clearHistory();
    this->sendCmd_ADMIT_REPLICA(TEST_INSTANCE_ID, 0, slot);
    this->component.doDispatch();
}

void RedundancyManagerTester ::policyCmd(Policy::T policy) {
    this->clearHistory();
    this->sendCmd_SET_POLICY(TEST_INSTANCE_ID, 0, policy);
    this->component.doDispatch();
}

I32 RedundancyManagerTester ::requested(FwIndexType slot, RequestKind::T kind) const {
    for (FwSizeType n = 0; n < this->fromPortHistory_requestOut->size(); n++) {
        const FromPortEntry_requestOut& e = this->fromPortHistory_requestOut->at(n);
        if (this->m_requestPort[n] == slot && e.kind == kind) {
            return static_cast<I32>(e.cycle);
        }
    }
    return -1;
}

FwSizeType RedundancyManagerTester ::requestCount(RequestKind::T kind) const {
    FwSizeType count = 0;
    for (FwSizeType n = 0; n < this->fromPortHistory_requestOut->size(); n++) {
        count += (this->fromPortHistory_requestOut->at(n).kind == kind) ? 1 : 0;
    }
    return count;
}

void RedundancyManagerTester ::downToOne() {
    this->standbyCmd(1);
    this->standbyCmd(2);
}

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

void RedundancyManagerTester ::voteDelivers() {
    this->cycle();
    ASSERT_EVENTS_StateNotRestored_SIZE(1);
    // policy, mode and live replicas are sent on change: here, the first time
    ASSERT_TLM_CurrentMode(0, Mode::VOTE);
    ASSERT_TLM_LiveReplicas(0, 3);
    ASSERT_TLM_CurrentPolicy(0, Policy::SHRINK);
    ASSERT_EQ(this->requestCount(RequestKind::COMPUTE), 3);
    for (FwIndexType i = 0; i < Replicas; i++) {
        ASSERT_EQ(this->requested(i, RequestKind::COMPUTE), 1);
    }
    // every request carries no upset: only a test topology sets one
    ASSERT_FALSE(this->fromPortHistory_requestOut->at(0).upset.get_first());

    // two agree, one outvoted: the majority is delivered at once
    this->replyAll(1, GOOD, BAD, GOOD);
    ASSERT_from_resultOut_SIZE(1);
    ASSERT_from_resultOut(0, 1, GOOD);
    ASSERT_from_healthStrike_SIZE(1);
    ASSERT_from_healthStrike(0, 1, 1);

    this->cycle();
    ASSERT_TLM_Delivered(0, 1);
    ASSERT_TLM_Caught(0, 0);
    ASSERT_TLM_CurrentMode_SIZE(0);  // unchanged
}

void RedundancyManagerTester ::decidesAtNextCycleWithWhatArrived() {
    this->cycle();
    this->reply(0, 1, RequestKind::COMPUTE, GOOD);
    this->reply(2, 1, RequestKind::COMPUTE, GOOD);
    ASSERT_from_resultOut_SIZE(0);  // still waiting for replica 1
    this->cycle();
    ASSERT_from_resultOut_SIZE(1);
    ASSERT_from_resultOut(0, 1, GOOD);
    // a reply for a cycle already decided is ignored
    this->reply(1, 1, RequestKind::COMPUTE, BAD);
    ASSERT_from_resultOut_SIZE(0);
    ASSERT_EVENTS_SIZE(0);
}

void RedundancyManagerTester ::disagreementIsCaught() {
    this->cycle();
    this->replyAll(1, 1, 2, 3);
    ASSERT_from_resultOut_SIZE(0);
    ASSERT_EVENTS_DisagreementCaught_SIZE(1);
    ASSERT_EVENTS_DisagreementCaught(0, 1, Mode::VOTE);
    this->cycle();
    ASSERT_TLM_Caught(0, 1);
}

void RedundancyManagerTester ::outvoteLeadsToDiagnosis() {
    this->m_strikeAnswer = true;
    this->cycle();
    this->replyAll(1, GOOD, GOOD, BAD);
    ASSERT_EVENTS_DiagnosisRequested_SIZE(1);
    ASSERT_EVENTS_DiagnosisRequested(0, 2, 1);
    ASSERT_EQ(this->requested(2, RequestKind::KNOWN_ANSWER), 1);
    // the right answer clears the replica's strikes
    this->reply(2, 1, RequestKind::KNOWN_ANSWER, KNOWN_ANSWER);
    ASSERT_from_healthClear_SIZE(1);
    ASSERT_from_healthClear(0, 2);
    ASSERT_EVENTS_ReplicaCleared(0, 2, 1);
    ASSERT_EVENTS_ReplicaRetired_SIZE(0);
}

void RedundancyManagerTester ::diagnosisFailureRetires() {
    this->m_strikeAnswer = true;
    this->cycle();
    this->replyAll(1, GOOD, BAD, GOOD);
    this->reply(1, 1, RequestKind::KNOWN_ANSWER, BAD);
    ASSERT_EVENTS_ReplicaRetired_SIZE(1);
    ASSERT_EVENTS_ReplicaRetired(0, 1, RetireReason::KNOWN_ANSWER_FAILED, 1);
    ASSERT_EQ(this->m_enabled[1], 0);
    // the retired replica gets no more requests
    this->cycle();
    ASSERT_EQ(this->requested(1, RequestKind::COMPUTE), -1);
    ASSERT_EQ(this->requestCount(RequestKind::COMPUTE), 2);
    ASSERT_TLM_Retired(0, 1);
    ASSERT_TLM_LiveReplicas(0, 2);
    ASSERT_TLM_CurrentMode(0, Mode::COMPARE);
    ASSERT_EVENTS_ModeChanged(0, Mode::VOTE, Mode::COMPARE, 2);
    const SlotState slots[Replicas] = {SlotState::ACTIVE, SlotState::RETIRED, SlotState::ACTIVE};
    ASSERT_TLM_Slots(0, SlotStates(slots));
}

void RedundancyManagerTester ::diagnosisWithoutAnswerRetires() {
    this->m_strikeAnswer = true;
    this->cycle();
    this->replyAll(1, BAD, GOOD, GOOD);
    this->m_strikeAnswer = false;
    this->cycle();  // cycle 2: the test had this cycle to answer
    ASSERT_EVENTS_ReplicaRetired_SIZE(0);
    this->replyAll(2, GOOD, GOOD, GOOD);
    this->cycle();  // cycle 3: no answer, retired
    ASSERT_EVENTS_ReplicaRetired_SIZE(1);
    ASSERT_EVENTS_ReplicaRetired(0, 0, RetireReason::KNOWN_ANSWER_MISSING, 2);
}

void RedundancyManagerTester ::twoDisagreeBothAreTested() {
    this->standbyCmd(0);
    this->cycle();
    this->reply(1, 1, RequestKind::COMPUTE, GOOD);
    this->reply(2, 1, RequestKind::COMPUTE, BAD);
    ASSERT_EVENTS_DisagreementCaught(0, 1, Mode::COMPARE);
    ASSERT_EQ(this->requested(1, RequestKind::KNOWN_ANSWER), 1);
    ASSERT_EQ(this->requested(2, RequestKind::KNOWN_ANSWER), 1);
    this->reply(1, 1, RequestKind::KNOWN_ANSWER, KNOWN_ANSWER);
    this->reply(2, 1, RequestKind::KNOWN_ANSWER, BAD);
    ASSERT_EVENTS_ReplicaRetired(0, 2, RetireReason::KNOWN_ANSWER_FAILED, 1);
    // one replica left: self-check
    this->cycle();
    ASSERT_EVENTS_ModeChanged(0, Mode::COMPARE, Mode::SELF_CHECK, 2);
    ASSERT_EQ(this->requested(1, RequestKind::COMPUTE_TWICE), 2);
}

void RedundancyManagerTester ::selfCheckEveryOtherCycleWithPeriodicTest() {
    this->paramSet_KatPeriod(4, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->downToOne();
    this->cycle();  // cycle 1: odd, nothing asked
    ASSERT_from_requestOut_SIZE(0);
    this->cycle();  // cycle 2: two results asked
    ASSERT_EQ(this->requested(0, RequestKind::COMPUTE_TWICE), 2);
    ASSERT_EQ(this->requestCount(RequestKind::KNOWN_ANSWER), 0);
    this->reply(0, 2, RequestKind::COMPUTE_TWICE, GOOD, GOOD);
    ASSERT_from_resultOut(0, 2, GOOD);
    this->cycle();  // 3
    this->cycle();  // 4: the periodic known-answer test rides with the cycle
    ASSERT_EQ(this->requested(0, RequestKind::COMPUTE_TWICE), 4);
    ASSERT_EQ(this->requested(0, RequestKind::KNOWN_ANSWER), 4);
    this->reply(0, 4, RequestKind::COMPUTE_TWICE, GOOD, GOOD);
    ASSERT_from_resultOut_SIZE(0);  // waits for the test
    this->reply(0, 4, RequestKind::KNOWN_ANSWER, KNOWN_ANSWER);
    ASSERT_from_resultOut(0, 4, GOOD);
    // a self-check mismatch is caught
    this->cycle();  // 5
    this->cycle();  // 6
    this->reply(0, 6, RequestKind::COMPUTE_TWICE, GOOD, BAD);
    ASSERT_EVENTS_DisagreementCaught(0, 6, Mode::SELF_CHECK);
}

void RedundancyManagerTester ::periodicTestFailureWithholdsTheCycle() {
    this->paramSet_KatPeriod(2, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->downToOne();
    this->cycle();  // 1
    this->cycle();  // 2
    // stuck: both runs agree on a wrong value, and the known answer is wrong
    this->reply(0, 2, RequestKind::COMPUTE_TWICE, BAD, BAD);
    this->reply(0, 2, RequestKind::KNOWN_ANSWER, BAD);
    ASSERT_from_resultOut_SIZE(0);
    ASSERT_EVENTS_ReplicaRetired(0, 0, RetireReason::PERIODIC_TEST_FAILED, 2);
    ASSERT_EVENTS_DisagreementCaught(0, 2, Mode::SELF_CHECK);
    this->cycle();
    ASSERT_EVENTS_Halted(0, 3);
}

void RedundancyManagerTester ::silentReplicaIsLost() {
    for (U64 c = 1; c <= 3; c++) {
        this->cycle();
        this->reply(0, c, RequestKind::COMPUTE, GOOD);
        this->reply(1, c, RequestKind::COMPUTE, GOOD);
    }
    ASSERT_EVENTS_ReplicaRetired_SIZE(0);
    this->cycle();  // the third miss is counted as cycle 3 closes
    ASSERT_EVENTS_ReplicaRetired_SIZE(1);
    ASSERT_EVENTS_ReplicaRetired(0, 2, RetireReason::LOST, 3);
    ASSERT_EQ(this->m_enabled[2], 0);
    ASSERT_EQ(this->requestCount(RequestKind::COMPUTE), 2);
}

void RedundancyManagerTester ::tmrHaltsOnOne() {
    this->policyCmd(Policy::TMR);
    this->standbyCmd(0);
    this->standbyCmd(1);  // refused: TMR cannot run on one
    ASSERT_EVENTS_SlotCommandRefused(0, 1, RefuseReason::WOULD_HALT);
    // lose replica 1 by silence
    for (U64 c = 1; c <= 3; c++) {
        this->cycle();
        this->reply(2, c, RequestKind::COMPUTE, GOOD);
    }
    this->cycle();
    ASSERT_EVENTS_ReplicaRetired(0, 1, RetireReason::LOST, 3);
    ASSERT_EVENTS_Halted_SIZE(1);
    ASSERT_EVENTS_Halted(0, 4);
    ASSERT_FROM_PORT_HISTORY_SIZE(1);  // only the enable(false) of the lost replica
    this->cycle();
    ASSERT_EVENTS_Halted_SIZE(0);  // once
    ASSERT_EQ(this->requestCount(RequestKind::COMPUTE), 0);
}

void RedundancyManagerTester ::setPolicy() {
    this->policyCmd(Policy::TMR);
    ASSERT_CMD_RESPONSE(0, RedundancyManagerComponentBase::OPCODE_SET_POLICY, 0, Fw::CmdResponse::OK);
    ASSERT_EVENTS_PolicySet(0, Policy::TMR);
    ASSERT_TLM_CurrentPolicy(0, Policy::TMR);
    // fixed TMR keeps no health record
    this->m_strikeAnswer = true;
    this->cycle();
    this->replyAll(1, GOOD, GOOD, BAD);
    ASSERT_from_healthStrike_SIZE(0);
    ASSERT_from_resultOut(0, 1, GOOD);
}

void RedundancyManagerTester ::standby() {
    this->standbyCmd(3);
    ASSERT_EVENTS_SlotCommandRefused(0, 3, RefuseReason::NO_SUCH_SLOT);
    ASSERT_CMD_RESPONSE(0, RedundancyManagerComponentBase::OPCODE_STANDBY_REPLICA, 0,
                        Fw::CmdResponse::EXECUTION_ERROR);
    this->standbyCmd(2);
    ASSERT_CMD_RESPONSE(0, RedundancyManagerComponentBase::OPCODE_STANDBY_REPLICA, 0, Fw::CmdResponse::OK);
    ASSERT_EVENTS_ReplicaStandby(0, 2, 0);
    ASSERT_EQ(this->m_enabled[2], 0);
    this->standbyCmd(2);
    ASSERT_EVENTS_SlotCommandRefused(0, 2, RefuseReason::WRONG_STATE);
    this->cycle();
    ASSERT_EQ(this->requested(2, RequestKind::COMPUTE), -1);
    ASSERT_TLM_CurrentMode(0, Mode::COMPARE);
    // on standby during a cycle: its reply no longer counts, and the cycle is
    // decided with what is left: one single result, which self-check cannot accept
    this->reply(0, 1, RequestKind::COMPUTE, GOOD);
    this->standbyCmd(1);
    ASSERT_from_resultOut_SIZE(0);
    ASSERT_EVENTS_DisagreementCaught(0, 1, Mode::SELF_CHECK);
    // the shrinking quorum refuses to set the last replica aside
    this->standbyCmd(0);
    ASSERT_EVENTS_SlotCommandRefused(0, 0, RefuseReason::WOULD_HALT);
}

void RedundancyManagerTester ::admit() {
    this->admitCmd(0);
    ASSERT_EVENTS_SlotCommandRefused(0, 0, RefuseReason::WRONG_STATE);
    this->standbyCmd(2);
    this->standbyCmd(1);
    this->cycle();
    // admission: enable, then the known-answer test
    this->admitCmd(2);
    ASSERT_CMD_RESPONSE(0, RedundancyManagerComponentBase::OPCODE_ADMIT_REPLICA, 0, Fw::CmdResponse::OK);
    ASSERT_EVENTS_AdmitRequested(0, 2, 1);
    ASSERT_EQ(this->m_enabled[2], 1);
    ASSERT_EQ(this->requested(2, RequestKind::KNOWN_ANSWER), 1);
    this->admitCmd(1);
    ASSERT_EVENTS_SlotCommandRefused(0, 1, RefuseReason::BUSY);
    this->reply(2, 1, RequestKind::KNOWN_ANSWER, KNOWN_ANSWER);
    ASSERT_EVENTS_ReplicaAdmitted(0, 2, 1);
    ASSERT_from_healthClear(0, 2);
    this->cycle();
    ASSERT_EQ(this->requested(2, RequestKind::COMPUTE), 2);
    ASSERT_TLM_LiveReplicas(0, 2);

    // a wrong answer: disabled again, still on standby
    this->admitCmd(1);
    this->reply(1, 2, RequestKind::KNOWN_ANSWER, BAD);
    ASSERT_EVENTS_AdmitFailed(0, 1, AdmitFailure::KNOWN_ANSWER_FAILED, 2);
    ASSERT_EQ(this->m_enabled[1], 0);
    // no answer by the end of the next cycle
    this->admitCmd(1);
    this->cycle();  // 3
    ASSERT_EVENTS_AdmitFailed_SIZE(0);
    this->cycle();  // 4
    ASSERT_EVENTS_AdmitFailed(0, 1, AdmitFailure::KNOWN_ANSWER_MISSING, 3);
}

void RedundancyManagerTester ::stateSurvivesAReset() {
    this->paramSet_StateSavePeriod(1, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->policyCmd(Policy::TMR);
    this->standbyCmd(1);
    for (U64 c = 1; c <= 5; c++) {
        this->cycle();
        this->reply(0, c, RequestKind::COMPUTE, GOOD);
        this->reply(2, c, RequestKind::COMPUTE, GOOD);
    }
    // a second manager on the same file, as after a reset
    RedundancyManagerTester again(this->m_statePath, false);
    again.resumesFromState();
}

void RedundancyManagerTester ::resumesFromState() {
    this->cycle();
    const SlotState slots[Replicas] = {SlotState::ACTIVE, SlotState::STANDBY, SlotState::ACTIVE};
    ASSERT_EVENTS_StateRestored_SIZE(1);
    ASSERT_EVENTS_StateRestored(0, Policy::TMR, SlotStates(slots), 6);
    ASSERT_EQ(this->m_enabled[1], 0);
    ASSERT_EQ(this->requested(0, RequestKind::COMPUTE), 6);
    ASSERT_EQ(this->requested(2, RequestKind::COMPUTE), 6);
    ASSERT_EQ(this->requested(1, RequestKind::COMPUTE), -1);
    ASSERT_TLM_CurrentPolicy(0, Policy::TMR);
}

void RedundancyManagerTester ::badStateFileIsIgnored() {
    Os::File file;
    ASSERT_EQ(file.open(this->m_statePath, Os::File::OPEN_CREATE, Os::File::OVERWRITE), Os::File::OP_OK);
    const U8 junk[RedundancyManager::STATE_SIZE] = {1, 2, 3};
    FwSizeType size = sizeof(junk);
    ASSERT_EQ(file.write(junk, size), Os::File::OP_OK);
    file.close();
    this->component.configure(KNOWN_ANSWER, this->m_statePath);
    this->cycle();
    ASSERT_EVENTS_StateNotRestored_SIZE(1);
    ASSERT_EQ(this->requestCount(RequestKind::COMPUTE), 3);
}

void RedundancyManagerTester ::stateSavedPeriodically() {
    this->paramSet_StateSavePeriod(2, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->cycle();
    FwSizeType size = 0;
    ASSERT_NE(Os::FileSystem::getFileSize(this->m_statePath, size), Os::FileSystem::OP_OK);  // not yet
    this->cycle();
    ASSERT_EQ(Os::FileSystem::getFileSize(this->m_statePath, size), Os::FileSystem::OP_OK);
    ASSERT_EQ(size, RedundancyManager::STATE_SIZE);
}

void RedundancyManagerTester ::unloadedParametersUseDefaults() {
    RedundancyManagerTester unloaded(this->m_statePath, true, false);
    unloaded.downToOne();
    for (U64 c = 1; c <= RedundancyManager::DEFAULT_KAT_PERIOD; c++) {
        unloaded.cycle();
        // the known-answer test only on the default period, not every cycle
        const I32 expected = (c == RedundancyManager::DEFAULT_KAT_PERIOD) ? static_cast<I32>(c) : -1;
        ASSERT_EQ(unloaded.requested(0, RequestKind::KNOWN_ANSWER), expected) << c;
        if (c % 2 == 0) {
            unloaded.reply(0, c, RequestKind::COMPUTE_TWICE, GOOD, GOOD);
        }
    }
}

void RedundancyManagerTester ::ping() {
    this->clearHistory();
    this->invoke_to_pingIn(0, 0x55);
    this->component.doDispatch();
    ASSERT_from_pingOut(0, 0x55);
}

}  // namespace Mru
