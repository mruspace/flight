// ======================================================================
// \title  SensorSimTestMain.cpp
// \brief  Unit tests of SensorSim
// ======================================================================

#include "Fw/Test/UnitTest.hpp"
#include "SensorSimTester.hpp"

TEST(Nominal, OffUntilOn) {
    COMMENT("Off until SENSORS_ON; then readings within the noise of the truth");
    REQUIREMENT("MRU-SS-001");
    Mru::SensorSimTester tester;
    tester.offUntilOn();
}

TEST(Nominal, Faults) {
    COMMENT("Spike, drift, stuck and drop act on their sensor and cycles");
    REQUIREMENT("MRU-SS-002");
    Mru::SensorSimTester tester;
    tester.faults();
}

TEST(Nominal, Scoring) {
    COMMENT("Values are scored against the truth, and the plain average too");
    REQUIREMENT("MRU-SS-003");
    REQUIREMENT("MRU-SS-004");
    Mru::SensorSimTester tester;
    tester.scoring();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
