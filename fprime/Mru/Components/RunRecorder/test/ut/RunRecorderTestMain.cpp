// ======================================================================
// \title  RunRecorderTestMain.cpp
// \brief  Unit tests of RunRecorder
// ======================================================================

#include "Fw/Test/UnitTest.hpp"
#include "RunRecorderTester.hpp"

TEST(Nominal, CountsResults) {
    COMMENT("Correct and wrong results are counted and reported; a wrong one raises an event");
    REQUIREMENT("MRU-RR-001");
    REQUIREMENT("MRU-RR-002");
    Mru::RunRecorderTester tester;
    tester.countsResults();
}

TEST(Nominal, ReadsManagerTelemetry) {
    COMMENT("The totals carry the manager's last telemetry");
    REQUIREMENT("MRU-RR-003");
    Mru::RunRecorderTester tester;
    tester.readsManagerTelemetry();
}

TEST(Nominal, CountsCyclesAndSequence) {
    COMMENT("Cycles are counted and the end of the sequence is recorded with its status");
    REQUIREMENT("MRU-RR-003");
    REQUIREMENT("MRU-RR-004");
    Mru::RunRecorderTester tester;
    tester.countsCyclesAndSequence();
}

TEST(Nominal, CallsTheCycleHook) {
    COMMENT("The cycle hook is called with each cycle's number");
    REQUIREMENT("MRU-RR-006");
    Mru::RunRecorderTester tester;
    tester.callsTheCycleHook();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
