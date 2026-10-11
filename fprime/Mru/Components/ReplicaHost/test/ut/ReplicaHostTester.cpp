// ======================================================================
// \title  ReplicaHostTester.cpp
// \brief  Unit tests of ReplicaHost
//
// Expected payload values were computed by a separate implementation of
// the demo's task (xorshift fill, FNV-1a hash), not by this code.
// ======================================================================

#include "ReplicaHostTester.hpp"

namespace Mru {

namespace {

const Upset NO_UPSET(false, false, 0, 0);
constexpr U64 RESULT_0 = 0x712be6888391473fULL;  // input 0: the known answer
constexpr U64 RESULT_1 = 0x24b9ecfc6360a92dULL;
constexpr U64 RESULT_1_FLIP_7_3 = 0x7e798595b8e32795ULL;  // input 1, word 7, bit 3 flipped
constexpr U64 RESULT_12345 = 0x24e40e386d92b58dULL;
constexpr U64 RESULT_12345_FLIP_511_63 = 0x24e38e386d91dc0dULL;

}  // namespace

ReplicaHostTester ::ReplicaHostTester()
    : ReplicaHostGTestBase("ReplicaHostTester", ReplicaHostTester::MAX_HISTORY_SIZE), component("ReplicaHost") {
    this->initComponents();
    this->connectPorts();
}

ReplicaHostTester ::~ReplicaHostTester() {
    this->component.deinit();
}

void ReplicaHostTester ::request(U64 cycle, RequestKind::T kind, const Upset& upset) {
    this->clearHistory();
    this->invoke_to_requestIn(0, cycle, kind, upset);
    this->component.doDispatch();
}

void ReplicaHostTester ::enable(bool enabled) {
    this->clearHistory();
    this->invoke_to_enableIn(0, enabled);
    this->component.doDispatch();
}

void ReplicaHostTester ::computesTheCycle() {
    this->request(1, RequestKind::COMPUTE, NO_UPSET);
    ASSERT_from_replyOut_SIZE(1);
    ASSERT_from_replyOut(0, 1, RequestKind::COMPUTE, RESULT_1, 0);
    this->request(12345, RequestKind::COMPUTE, Upset(true, false, 511, 63));
    ASSERT_from_replyOut(0, 12345, RequestKind::COMPUTE, RESULT_12345_FLIP_511_63, 0);
    // word and bit wrap as in the demo: word 519 is word 7, bit 67 is bit 3
    this->request(1, RequestKind::COMPUTE, Upset(true, false, 519, 67));
    ASSERT_from_replyOut(0, 1, RequestKind::COMPUTE, RESULT_1_FLIP_7_3, 0);
}

void ReplicaHostTester ::computesTwice() {
    this->request(12345, RequestKind::COMPUTE_TWICE, NO_UPSET);
    ASSERT_from_replyOut(0, 12345, RequestKind::COMPUTE_TWICE, RESULT_12345, RESULT_12345);
    this->request(12345, RequestKind::COMPUTE_TWICE, Upset(true, false, 511, 63));
    ASSERT_from_replyOut(0, 12345, RequestKind::COMPUTE_TWICE, RESULT_12345_FLIP_511_63, RESULT_12345);
    this->request(12345, RequestKind::COMPUTE_TWICE, Upset(true, true, 511, 63));
    ASSERT_from_replyOut(0, 12345, RequestKind::COMPUTE_TWICE, RESULT_12345_FLIP_511_63, RESULT_12345_FLIP_511_63);
}

void ReplicaHostTester ::knownAnswer() {
    ASSERT_EQ(ReplicaHost::expectedKnownAnswer(), RESULT_0);
    // an upset in the request does not touch the known-answer test
    this->request(77, RequestKind::KNOWN_ANSWER, Upset(true, true, 3, 3));
    ASSERT_from_replyOut(0, 77, RequestKind::KNOWN_ANSWER, RESULT_0, 0);
}

void ReplicaHostTester ::disabledIsSilent() {
    this->enable(false);
    this->request(1, RequestKind::COMPUTE, NO_UPSET);
    ASSERT_from_replyOut_SIZE(0);
    this->request(1, RequestKind::KNOWN_ANSWER, NO_UPSET);
    ASSERT_from_replyOut_SIZE(0);
    this->enable(true);
    this->request(1, RequestKind::COMPUTE, NO_UPSET);
    ASSERT_from_replyOut_SIZE(1);
}

void ReplicaHostTester ::ping() {
    this->clearHistory();
    this->invoke_to_pingIn(0, 0x1234);
    this->component.doDispatch();
    ASSERT_from_pingOut_SIZE(1);
    ASSERT_from_pingOut(0, 0x1234);
}

}  // namespace Mru
