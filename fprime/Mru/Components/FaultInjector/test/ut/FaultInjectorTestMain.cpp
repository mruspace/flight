// ======================================================================
// \title  FaultInjectorTestMain.cpp
// \brief  Unit tests of FaultInjector
// ======================================================================

#include "FaultInjectorTester.hpp"
#include "Fw/Test/UnitTest.hpp"

TEST(Nominal, PassThrough) {
    COMMENT("Without faults, requests and replies pass unchanged");
    Mru::FaultInjectorTester tester;
    tester.passThrough();
}

TEST(Nominal, UpsetsLikeTheDemo) {
    COMMENT("Upsets follow the demo's generator, draw for draw");
    REQUIREMENT("MRU-FI-002");
    Mru::FaultInjectorTester tester;
    tester.upsetsLikeTheDemo();
}

TEST(Nominal, Kill) {
    COMMENT("A killed replica gets nothing from its cycle on, and draws nothing");
    REQUIREMENT("MRU-FI-001");
    Mru::FaultInjectorTester tester;
    tester.kill();
}

TEST(Nominal, Hang) {
    COMMENT("A hung replica draws once, as the demo's last ask, then gets nothing");
    REQUIREMENT("MRU-FI-001");
    Mru::FaultInjectorTester tester;
    tester.hang();
}

TEST(Nominal, Stuck) {
    COMMENT("A stuck replica's answers become the stuck value from its cycle");
    REQUIREMENT("MRU-FI-001");
    Mru::FaultInjectorTester tester;
    tester.stuck();
}

TEST(Nominal, Corrupt) {
    COMMENT("A corrupt fault flips a bit in exactly its cycle");
    REQUIREMENT("MRU-FI-001");
    Mru::FaultInjectorTester tester;
    tester.corrupt();
}

TEST(Nominal, ClearFaults) {
    COMMENT("Clearing a replica's faults models new hardware");
    Mru::FaultInjectorTester tester;
    tester.clearFaults();
}

TEST(OffNominal, Refusals) {
    COMMENT("Commands with bad arguments are refused");
    REQUIREMENT("MRU-FI-001");
    REQUIREMENT("MRU-FI-002");
    Mru::FaultInjectorTester tester;
    tester.refusals();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
