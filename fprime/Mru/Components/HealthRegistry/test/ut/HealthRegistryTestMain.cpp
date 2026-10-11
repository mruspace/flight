// ======================================================================
// \title  HealthRegistryTestMain.cpp
// \brief  Unit tests of HealthRegistry
// ======================================================================

#include "Fw/Test/UnitTest.hpp"
#include "HealthRegistryTester.hpp"

TEST(Nominal, ThreeStrikesInWindow) {
    COMMENT("Three outvotes within 200 cycles ask for a diagnosis");
    REQUIREMENT("MRU-HR-001");
    Mru::HealthRegistryTester tester;
    tester.threeStrikesInWindow();
}

TEST(Nominal, StrikesOutsideWindow) {
    COMMENT("Outvotes spread wider than the window do not");
    REQUIREMENT("MRU-HR-001");
    Mru::HealthRegistryTester tester;
    tester.strikesOutsideWindow();
}

TEST(Nominal, ClearForgets) {
    COMMENT("A clear forgets the strikes");
    REQUIREMENT("MRU-HR-002");
    Mru::HealthRegistryTester tester;
    tester.clearForgets();
}

TEST(Nominal, ReportsStrikes) {
    COMMENT("Strike counts are reported per replica, capped at 3");
    REQUIREMENT("MRU-HR-003");
    Mru::HealthRegistryTester tester;
    tester.reportsStrikes();
}

TEST(OffNominal, OutOfRange) {
    COMMENT("Replica indexes out of range change nothing");
    REQUIREMENT("MRU-HR-001");
    REQUIREMENT("MRU-HR-002");
    Mru::HealthRegistryTester tester;
    tester.outOfRange();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
