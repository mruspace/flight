// ======================================================================
// \title  RedundancyManagerTestMain.cpp
// \brief  Unit tests of RedundancyManager
// ======================================================================

#include "Fw/Test/UnitTest.hpp"
#include "RedundancyManagerTester.hpp"

namespace {
const char* const STATE = "redundancy-manager-ut.state";
}

#define MRU_TEST(group, name, method, comment, ...)  \
    TEST(group, name) {                               \
        COMMENT(comment);                             \
        const char* reqs[] = {__VA_ARGS__};           \
        for (const char* r : reqs) {                  \
            REQUIREMENT(r);                           \
        }                                             \
        Mru::RedundancyManagerTester tester(STATE);   \
        tester.method();                              \
    }

MRU_TEST(Nominal, VoteDelivers, voteDelivers, "Requests to all, the majority delivered, telemetry",
         "MRU-RM-001", "MRU-RM-002", "MRU-RM-003", "MRU-RM-014", "MRU-RM-016")
MRU_TEST(Nominal, DecidesAtNextCycle, decidesAtNextCycleWithWhatArrived,
         "A missing reply is absent when the next cycle starts", "MRU-RM-002")
MRU_TEST(Nominal, DisagreementIsCaught, disagreementIsCaught, "No majority: nothing delivered, counted", "MRU-RM-005")
MRU_TEST(Nominal, OutvoteLeadsToDiagnosis, outvoteLeadsToDiagnosis,
         "Outvotes go to HealthRegistry; a diagnosis passed clears the strikes", "MRU-RM-006", "MRU-RM-009")
MRU_TEST(OffNominal, DiagnosisFailureRetires, diagnosisFailureRetires,
         "A wrong known answer retires and disables the replica", "MRU-RM-009", "MRU-RM-011", "MRU-RM-004",
         "MRU-RM-021")
MRU_TEST(OffNominal, DiagnosisWithoutAnswerRetires, diagnosisWithoutAnswerRetires,
         "No known answer by the end of the next cycle retires the replica", "MRU-RM-009", "MRU-RM-011")
MRU_TEST(Nominal, TwoDisagreeBothAreTested, twoDisagreeBothAreTested,
         "Two disagree: both tested, the failed one retired, self-check follows", "MRU-RM-007", "MRU-RM-004")
MRU_TEST(Nominal, SelfCheck, selfCheckEveryOtherCycleWithPeriodicTest,
         "One replica: two results every other cycle, periodic known-answer test", "MRU-RM-001", "MRU-RM-008",
         "MRU-RM-015")
MRU_TEST(OffNominal, PeriodicTestFailure, periodicTestFailureWithholdsTheCycle,
         "A failed periodic test retires the last replica and withholds the cycle", "MRU-RM-008", "MRU-RM-012")
MRU_TEST(OffNominal, SilentReplicaIsLost, silentReplicaIsLost, "Three missed cycles in a row: lost",
         "MRU-RM-010", "MRU-RM-011")
MRU_TEST(OffNominal, TmrHaltsOnOne, tmrHaltsOnOne, "Fixed TMR halts on one replica, with one event",
         "MRU-RM-012", "MRU-RM-019")
MRU_TEST(Nominal, SetPolicy, setPolicy, "SET_POLICY, and TMR keeps no health record", "MRU-RM-013")
MRU_TEST(Nominal, Standby, standby, "STANDBY_REPLICA and its refusals", "MRU-RM-018", "MRU-RM-019")
MRU_TEST(Nominal, Admit, admit, "ADMIT_REPLICA: pass, fail, no answer, busy", "MRU-RM-020")
MRU_TEST(Nominal, StateSurvivesAReset, stateSurvivesAReset, "Policy, slots and cycle restored after a reset",
         "MRU-RM-022")
MRU_TEST(OffNominal, BadStateFileIsIgnored, badStateFileIsIgnored, "A corrupt state file is not used",
         "MRU-RM-022")
MRU_TEST(Nominal, StateSavedPeriodically, stateSavedPeriodically, "The state file is written every period",
         "MRU-RM-022")
MRU_TEST(Nominal, Ping, ping, "Health ping is returned", "MRU-RM-014")
MRU_TEST(OffNominal, UnloadedParameters, unloadedParametersUseDefaults,
         "Parameters never loaded: the model's defaults, not zero", "MRU-RM-015", "MRU-RM-022")

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
