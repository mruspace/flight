// ======================================================================
// \title  FaultInjectorTester.cpp
// \brief  Unit tests of FaultInjector
//
// Expected draws were computed by a separate implementation of the demo's
// generator (xorshift64, seeded as seed * 0x9E3779B97F4A7C15 + 7).
// ======================================================================

#include "FaultInjectorTester.hpp"

namespace Mru {

namespace {
const Upset NO_UPSET(false, false, 0, 0);
}

FaultInjectorTester ::FaultInjectorTester()
    : FaultInjectorGTestBase("FaultInjectorTester", FaultInjectorTester::MAX_HISTORY_SIZE),
      component("FaultInjector"),
      m_lastRequestPort(-1) {
    this->initComponents();
    this->connectPorts();
}

FaultInjectorTester ::~FaultInjectorTester() {
    this->component.deinit();
}

void FaultInjectorTester ::from_requestOut_handler(FwIndexType portNum,
                                                   U64 cycle,
                                                   const Mru::RequestKind& kind,
                                                   const Mru::Upset& upset) {
    this->m_lastRequestPort = portNum;
    FaultInjectorGTestBase::from_requestOut_handler(portNum, cycle, kind, upset);
}

void FaultInjectorTester ::inject(FaultKind::T kind, U8 replica, U64 cycle) {
    this->clearHistory();
    this->sendCmd_INJECT(TEST_INSTANCE_ID, 0, kind, replica, cycle);
}

void FaultInjectorTester ::setUpsets(F64 rate, F64 commonMode, U64 seed) {
    this->clearHistory();
    this->sendCmd_SET_UPSETS(TEST_INSTANCE_ID, 0, rate, commonMode, seed);
}

void FaultInjectorTester ::request(FwIndexType replica, U64 cycle, RequestKind::T kind) {
    this->clearHistory();
    this->invoke_to_requestIn(replica, cycle, kind, NO_UPSET);
}

void FaultInjectorTester ::assertForwarded(FwIndexType replica, U64 cycle, RequestKind::T kind, const Upset& upset) {
    ASSERT_from_requestOut_SIZE(1);
    ASSERT_EQ(this->fromPortHistory_requestOut->at(0).cycle, cycle);
    ASSERT_EQ(this->fromPortHistory_requestOut->at(0).kind, kind);
    ASSERT_EQ(this->fromPortHistory_requestOut->at(0).upset, upset);
    // the request went out on the replica's own port
    ASSERT_EQ(this->m_lastRequestPort, replica);
}

void FaultInjectorTester ::passThrough() {
    this->request(1, 5, RequestKind::COMPUTE);
    // the demo draws a position for every ask, even with no upsets; nothing is flipped
    ASSERT_from_requestOut_SIZE(1);
    ASSERT_EQ(this->m_lastRequestPort, 1);
    ASSERT_FALSE(this->fromPortHistory_requestOut->at(0).upset.get_first());
    ASSERT_FALSE(this->fromPortHistory_requestOut->at(0).upset.get_second());
    this->clearHistory();
    this->invoke_to_replyIn(2, 5, RequestKind::COMPUTE, 11, 22);
    ASSERT_from_replyOut_SIZE(1);
    ASSERT_from_replyOut(0, 5, RequestKind::COMPUTE, 11, 22);
    ASSERT_EVENTS_SIZE(0);
}

void FaultInjectorTester ::upsetsLikeTheDemo() {
    this->setUpsets(1.0, 1.0, 1);
    ASSERT_CMD_RESPONSE(0, FaultInjectorComponentBase::OPCODE_SET_UPSETS, 0, Fw::CmdResponse::OK);
    ASSERT_EVENTS_UpsetsSet(0, 1.0, 1.0, 1);
    this->request(0, 1, RequestKind::COMPUTE_TWICE);
    this->assertForwarded(0, 1, RequestKind::COMPUTE_TWICE, Upset(true, true, 447, 36));
    this->request(1, 1, RequestKind::COMPUTE);
    this->assertForwarded(1, 1, RequestKind::COMPUTE, Upset(true, false, 53, 25));
    // known-answer tests draw nothing and get no upset
    this->request(2, 1, RequestKind::KNOWN_ANSWER);
    this->assertForwarded(2, 1, RequestKind::KNOWN_ANSWER, NO_UPSET);

    // a rate of one half, seed 42
    this->setUpsets(0.5, 0.0, 42);
    this->request(0, 1, RequestKind::COMPUTE);
    this->assertForwarded(0, 1, RequestKind::COMPUTE, Upset(true, false, 342, 15));
    this->request(1, 1, RequestKind::COMPUTE);
    this->assertForwarded(1, 1, RequestKind::COMPUTE, Upset(false, false, 448, 39));
    this->request(2, 1, RequestKind::COMPUTE);
    this->assertForwarded(2, 1, RequestKind::COMPUTE, Upset(true, false, 257, 31));
}

void FaultInjectorTester ::kill() {
    this->inject(FaultKind::KILL, 1, 5);
    ASSERT_CMD_RESPONSE(0, FaultInjectorComponentBase::OPCODE_INJECT, 0, Fw::CmdResponse::OK);
    ASSERT_EVENTS_FaultScheduled(0, FaultKind::KILL, 1, 5);
    this->request(1, 4, RequestKind::COMPUTE);
    ASSERT_from_requestOut_SIZE(1);
    this->request(1, 5, RequestKind::COMPUTE);
    ASSERT_from_requestOut_SIZE(0);
    ASSERT_EVENTS_FaultApplied(0, FaultKind::KILL, 1, 5);
    this->request(1, 6, RequestKind::KNOWN_ANSWER);
    ASSERT_from_requestOut_SIZE(0);
    ASSERT_EVENTS_SIZE(0);  // applied once
    // another replica is untouched
    this->request(0, 6, RequestKind::COMPUTE);
    ASSERT_from_requestOut_SIZE(1);

    // a killed replica draws nothing: the next replica gets the first draw
    this->setUpsets(1.0, 0.0, 1);
    this->request(1, 7, RequestKind::COMPUTE);
    this->request(0, 7, RequestKind::COMPUTE);
    this->assertForwarded(0, 7, RequestKind::COMPUTE, Upset(true, false, 447, 36));
}

void FaultInjectorTester ::hang() {
    this->setUpsets(1.0, 0.0, 1);
    this->inject(FaultKind::HANG, 0, 3);
    this->request(0, 3, RequestKind::COMPUTE);
    ASSERT_from_requestOut_SIZE(0);
    this->request(1, 3, RequestKind::COMPUTE);
    this->assertForwarded(1, 3, RequestKind::COMPUTE, Upset(true, false, 413, 23));
    this->request(0, 4, RequestKind::COMPUTE);
    ASSERT_from_requestOut_SIZE(0);
    this->request(1, 4, RequestKind::COMPUTE);
    this->assertForwarded(1, 4, RequestKind::COMPUTE, Upset(true, false, 104, 3));
}

void FaultInjectorTester ::stuck() {
    this->inject(FaultKind::STUCK, 2, 10);
    this->clearHistory();
    this->invoke_to_replyIn(2, 9, RequestKind::COMPUTE, 5, 0);
    ASSERT_from_replyOut(0, 9, RequestKind::COMPUTE, 5, 0);
    this->clearHistory();
    this->invoke_to_replyIn(2, 10, RequestKind::COMPUTE_TWICE, 5, 5);
    ASSERT_from_replyOut(0, 10, RequestKind::COMPUTE_TWICE, FaultInjector::STUCK_VALUE, FaultInjector::STUCK_VALUE);
    ASSERT_EVENTS_FaultApplied(0, FaultKind::STUCK, 2, 10);
    this->clearHistory();
    this->invoke_to_replyIn(2, 11, RequestKind::KNOWN_ANSWER, 5, 0);
    ASSERT_from_replyOut(0, 11, RequestKind::KNOWN_ANSWER, FaultInjector::STUCK_VALUE, FaultInjector::STUCK_VALUE);
}

void FaultInjectorTester ::corrupt() {
    this->inject(FaultKind::CORRUPT, 0, 7);
    this->request(0, 6, RequestKind::COMPUTE);
    ASSERT_FALSE(this->fromPortHistory_requestOut->at(0).upset.get_first());
    this->request(0, 7, RequestKind::COMPUTE);
    ASSERT_TRUE(this->fromPortHistory_requestOut->at(0).upset.get_first());
    this->request(0, 8, RequestKind::COMPUTE);
    ASSERT_FALSE(this->fromPortHistory_requestOut->at(0).upset.get_first());
}

void FaultInjectorTester ::clearFaults() {
    this->inject(FaultKind::KILL, 2, 1);
    this->request(2, 1, RequestKind::COMPUTE);
    ASSERT_from_requestOut_SIZE(0);
    this->clearHistory();
    this->sendCmd_CLEAR_FAULTS(TEST_INSTANCE_ID, 0, 2);
    ASSERT_CMD_RESPONSE(0, FaultInjectorComponentBase::OPCODE_CLEAR_FAULTS, 0, Fw::CmdResponse::OK);
    ASSERT_EVENTS_FaultsCleared(0, 2);
    this->request(2, 2, RequestKind::COMPUTE);
    ASSERT_from_requestOut_SIZE(1);
}

void FaultInjectorTester ::refusals() {
    this->inject(FaultKind::KILL, 3, 1);
    ASSERT_CMD_RESPONSE(0, FaultInjectorComponentBase::OPCODE_INJECT, 0, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_FaultRefused(0, 3);
    for (FwSizeType i = 0; i < FaultInjector::MAX_FAULTS; i++) {
        this->inject(FaultKind::CORRUPT, 0, 100 + i);
        ASSERT_CMD_RESPONSE(0, FaultInjectorComponentBase::OPCODE_INJECT, 0, Fw::CmdResponse::OK);
    }
    this->inject(FaultKind::CORRUPT, 0, 1000);
    ASSERT_CMD_RESPONSE(0, FaultInjectorComponentBase::OPCODE_INJECT, 0, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_FaultRefused(0, 0);
    this->clearHistory();
    this->sendCmd_CLEAR_FAULTS(TEST_INSTANCE_ID, 0, 5);
    ASSERT_CMD_RESPONSE(0, FaultInjectorComponentBase::OPCODE_CLEAR_FAULTS, 0, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_FaultRefused(0, 5);
    this->setUpsets(1.5, 0.0, 1);
    ASSERT_CMD_RESPONSE(0, FaultInjectorComponentBase::OPCODE_SET_UPSETS, 0, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_UpsetsRefused(0, 1.5, 0.0);
    this->setUpsets(0.1, -0.1, 1);
    ASSERT_CMD_RESPONSE(0, FaultInjectorComponentBase::OPCODE_SET_UPSETS, 0, Fw::CmdResponse::VALIDATION_ERROR);
}

}  // namespace Mru
