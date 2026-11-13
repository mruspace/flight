// ======================================================================
// \title  SensorVoterTestMain.cpp
// \brief  Unit tests of SensorVoter
// ======================================================================

#include "Fw/Test/UnitTest.hpp"
#include "SensorVoterTester.hpp"

#define SV_TEST(group, name, method, comment, ...) \
    TEST(group, name) {                           \
        COMMENT(comment);                         \
        const char* reqs[] = {__VA_ARGS__};       \
        for (const char* r : reqs) {              \
            REQUIREMENT(r);                       \
        }                                         \
        Mru::SensorVoterTester tester;            \
        tester.method();                          \
    }

SV_TEST(Nominal, AgreedMedian, agreedMedian, "Three readings within the tolerance: the median, checked",
        "MRU-SV-001", "MRU-SV-002", "MRU-SV-003", "MRU-SV-007")
SV_TEST(OffNominal, SpikeIsMasked, spikeIsMaskedNotExcluded,
        "A spike is not chosen and does not exclude its sensor", "MRU-SV-004")
SV_TEST(OffNominal, PersistentOutlierIsExcluded, persistentOutlierIsExcluded,
        "An outlier ExcludeAfter cycles in a row is excluded and ignored", "MRU-SV-004", "MRU-SV-007")
SV_TEST(OffNominal, DisagreementDeliversNothing, disagreementDeliversNothing,
        "Two that disagree: nothing delivered, outlier counts kept", "MRU-SV-003")
SV_TEST(OffNominal, SingleReadingIsUnchecked, singleReadingIsUnchecked, "One reading: delivered unchecked",
        "MRU-SV-005")
SV_TEST(Nominal, IncludeSensor, includeSensor, "INCLUDE_SENSOR and its refusals", "MRU-SV-006")

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
