// ======================================================================
// \title  SensorVoterTester.cpp
// \brief  Unit tests of SensorVoter
// ======================================================================

#include "SensorVoterTester.hpp"

namespace Mru {

SensorVoterTester ::SensorVoterTester()
    : SensorVoterGTestBase("SensorVoterTester", SensorVoterTester::MAX_HISTORY_SIZE), component("SensorVoter") {
    this->initComponents();
    this->connectPorts();
}

SensorVoterTester ::~SensorVoterTester() {
    this->component.deinit();
}

void SensorVoterTester ::readings(U64 cycle, I32 a, I32 b, I32 c, bool pa, bool pb, bool pc) {
    this->clearHistory();
    SensorValues v;
    v[0] = a;
    v[1] = b;
    v[2] = c;
    SensorFlags p;
    p[0] = pa;
    p[1] = pb;
    p[2] = pc;
    this->invoke_to_readingsIn(0, cycle, v, p);
}

void SensorVoterTester ::params(U32 tolerance, U8 excludeAfter) {
    this->paramSet_Tolerance(tolerance, Fw::ParamValid::VALID);
    this->paramSet_ExcludeAfter(excludeAfter, Fw::ParamValid::VALID);
    this->component.loadParameters();
}

void SensorVoterTester ::agreedMedian() {
    this->params(100, 5);
    this->readings(1, 20010, 19990, 20040);
    ASSERT_from_selectedOut_SIZE(1);
    ASSERT_from_selectedOut(0, 1, 20010, true);
    ASSERT_EVENTS_SensorOutlier_SIZE(0);
    ASSERT_TLM_Agreed(0, 1);
}

void SensorVoterTester ::spikeIsMaskedNotExcluded() {
    this->params(100, 5);
    for (U64 c = 1; c <= 4; c++) {
        this->readings(c, 20000, 25000, 20010);
        ASSERT_from_selectedOut(0, c, 20010, true);
        ASSERT_EVENTS_SensorOutlier(0, 1, c, 25000, 20010);
        ASSERT_EVENTS_SensorExcluded_SIZE(0);
    }
    // back in line: the count starts again
    this->readings(5, 20000, 20020, 20010);
    ASSERT_from_selectedOut(0, 5, 20010, true);
    for (U64 c = 6; c <= 9; c++) {
        this->readings(c, 20000, 25000, 20010);
        ASSERT_EVENTS_SensorExcluded_SIZE(0);
    }
    ASSERT_TLM_Outliers(0, 8);
}

void SensorVoterTester ::persistentOutlierIsExcluded() {
    this->params(100, 3);
    this->readings(1, 20000, 20010, 23000);
    this->readings(2, 20000, 20010, 23000);
    ASSERT_EVENTS_SensorExcluded_SIZE(0);
    this->readings(3, 20000, 20010, 23000);
    ASSERT_EVENTS_SensorExcluded_SIZE(1);
    ASSERT_EVENTS_SensorExcluded(0, 2, 3, 3);
    const bool excluded[Sensors] = {false, false, true};
    ASSERT_TLM_Excluded(0, SensorFlags(excluded));
    // its readings are no longer used: two left, their midpoint
    this->readings(4, 20000, 20010, 50000);
    ASSERT_from_selectedOut(0, 4, 20005, true);
    ASSERT_EVENTS_SensorOutlier_SIZE(0);
}

void SensorVoterTester ::disagreementDeliversNothing() {
    this->params(100, 3);
    // sensor 2 twice an outlier, then a disagreement between 0 and 1 alone
    this->readings(1, 20000, 20010, 23000);
    this->readings(2, 20000, 20010, 23000);
    this->readings(3, 20000, 20500, 0, true, true, false);
    ASSERT_from_selectedOut_SIZE(0);
    ASSERT_EVENTS_SensorsDisagree(0, 3);
    ASSERT_TLM_Disagreed(0, 1);
    // the disagreement did not reset sensor 2's count: one more excludes it
    this->readings(4, 20000, 20010, 23000);
    ASSERT_EVENTS_SensorExcluded(0, 2, 4, 3);
}

void SensorVoterTester ::singleReadingIsUnchecked() {
    this->params(100, 3);
    this->readings(1, 0, 20000, 0, false, true, false);
    ASSERT_from_selectedOut(0, 1, 20000, false);
    ASSERT_EVENTS_UncheckedValue(0, 1);
    ASSERT_TLM_Unchecked(0, 1);
    this->readings(2, 0, 0, 0, false, false, false);
    ASSERT_from_selectedOut_SIZE(0);
}

void SensorVoterTester ::includeSensor() {
    this->params(100, 1);
    this->sendCmd_INCLUDE_SENSOR(TEST_INSTANCE_ID, 0, 2);
    ASSERT_EVENTS_IncludeRefused(0, 2);
    this->clearHistory();
    this->sendCmd_INCLUDE_SENSOR(TEST_INSTANCE_ID, 0, 3);
    ASSERT_EVENTS_IncludeRefused(0, 3);
    this->readings(1, 20000, 20010, 23000);
    ASSERT_EVENTS_SensorExcluded(0, 2, 1, 1);
    this->clearHistory();
    this->sendCmd_INCLUDE_SENSOR(TEST_INSTANCE_ID, 0, 2);
    ASSERT_CMD_RESPONSE(0, SensorVoterComponentBase::OPCODE_INCLUDE_SENSOR, 0, Fw::CmdResponse::OK);
    ASSERT_EVENTS_SensorIncluded(0, 2);
    this->readings(2, 20000, 20010, 20020);
    ASSERT_from_selectedOut(0, 2, 20010, true);
}

}  // namespace Mru
