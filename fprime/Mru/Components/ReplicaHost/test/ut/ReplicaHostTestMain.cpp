// ======================================================================
// \title  ReplicaHostTestMain.cpp
// \brief  Unit tests of ReplicaHost
// ======================================================================

#include "Fw/Test/UnitTest.hpp"
#include "ReplicaHostTester.hpp"

TEST(Nominal, ComputesTheCycle) {
    COMMENT("One result for the cycle, equal to the demo's");
    REQUIREMENT("MRU-RH-001");
    Mru::ReplicaHostTester tester;
    tester.computesTheCycle();
}

TEST(Nominal, ComputesTwice) {
    COMMENT("Two results for self-check; an upset flips the first, or both");
    REQUIREMENT("MRU-RH-002");
    Mru::ReplicaHostTester tester;
    tester.computesTwice();
}

TEST(Nominal, KnownAnswer) {
    COMMENT("The known answer, and the expected value given to the manager");
    REQUIREMENT("MRU-RH-003");
    Mru::ReplicaHostTester tester;
    tester.knownAnswer();
}

TEST(OffNominal, DisabledIsSilent) {
    COMMENT("A disabled replica does not answer");
    REQUIREMENT("MRU-RH-004");
    Mru::ReplicaHostTester tester;
    tester.disabledIsSilent();
}

TEST(Nominal, Ping) {
    COMMENT("Health ping is returned");
    Mru::ReplicaHostTester tester;
    tester.ping();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
