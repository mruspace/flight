// ======================================================================
// \title  RedundancyManagerTester.hpp
// \brief  Unit tests of RedundancyManager
// ======================================================================

#ifndef Mru_RedundancyManagerTester_HPP
#define Mru_RedundancyManagerTester_HPP

#include "Mru/Components/RedundancyManager/RedundancyManager.hpp"
#include "Mru/Components/RedundancyManager/RedundancyManagerGTestBase.hpp"

namespace Mru {

class RedundancyManagerTester final : public RedundancyManagerGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 64;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 32;

    //! The known answer the tests configure
    static constexpr U64 KNOWN_ANSWER = 0xABCDEF;

    //! A state file of its own: removed first when `fresh`, and after the test
    //! `load`: load parameters, as a topology does at start
    explicit RedundancyManagerTester(const char* statePath, bool fresh = true, bool load = true);
    ~RedundancyManagerTester();

    void voteDelivers();
    void decidesAtNextCycleWithWhatArrived();
    void disagreementIsCaught();
    void outvoteLeadsToDiagnosis();
    void diagnosisFailureRetires();
    void diagnosisWithoutAnswerRetires();
    void twoDisagreeBothAreTested();
    void selfCheckEveryOtherCycleWithPeriodicTest();
    void periodicTestFailureWithholdsTheCycle();
    void silentReplicaIsLost();
    //! Answers queued behind the next cycle: the cycle is withheld, nobody is lost (MRU-RM-023)
    void lateAnswersAreNotSilence();
    //! A known answer queued behind the next cycle: no retirement, then it counts (MRU-RM-023)
    void lateKnownAnswerIsNotMissing();
    //! Every expected answer missing at once: withheld, nobody lost (MRU-RM-023)
    void commonSilenceIsNotLoss();
    //! A replica that answers after its cycle closed is alive, never lost (MRU-RM-023)
    void lateReplicaStaysAlive();
    void tmrHaltsOnOne();
    //! A silent replica is reset and admitted after its known-answer test (MRU-RM-024)
    void silentReplicaIsResetAndAdmitted();
    //! Still silent after every reset allowed: kept out, no more resets (MRU-RM-024)
    void silentAfterResetsIsKeptOut();
    //! A replica retired for a wrong answer is not reset (MRU-RM-024)
    void wrongAnswerIsNotReset();
    //! The last replica, lost and reset, brings the quorum back from Halted (MRU-RM-024, 012)
    void lastReplicaComesBack();
    void setPolicy();
    void standby();
    void admit();
    void stateSurvivesAReset();

    //! The first cycle after a reset: the state of stateSurvivesAReset
    void resumesFromState();
    void badStateFileIsIgnored();
    void stateSavedPeriodically();
    void ping();

    //! Parameters never loaded: the model's defaults, not zero
    void unloadedParametersUseDefaults();

  private:
    void connectPorts();
    void initComponents();

    // ----------------------------------------------------------------------
    // Ports the tests watch
    // ----------------------------------------------------------------------

    bool from_healthStrike_handler(FwIndexType portNum, FwIndexType replica, U64 cycle) override;
    void from_enableOut_handler(FwIndexType portNum, bool enabled) override;
    void from_requestOut_handler(FwIndexType portNum,
                                 U64 cycle,
                                 const Mru::RequestKind& kind,
                                 const Mru::Upset& upset) override;
    void from_resetOut_handler(FwIndexType portNum, U64 cycle) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! One rate-group cycle
    void cycle();

    //! Dispatch every queued message, in order
    void dispatchAll();

    //! One reply from a replica
    void reply(FwIndexType slot, U64 cycle, RequestKind::T kind, U64 first, U64 second = 0);

    //! Three single results
    void replyAll(U64 cycle, U64 a, U64 b, U64 c);

    //! A command, dispatched
    void standbyCmd(U8 slot);
    void admitCmd(U8 slot);
    void policyCmd(Policy::T policy);

    //! Requests sent in the last action, per replica: the kind, or -1 for none
    I32 requested(FwIndexType slot, RequestKind::T kind) const;

    //! Count of requests of a kind in the last action
    FwSizeType requestCount(RequestKind::T kind) const;

    //! Put slots 1 and 2 on standby (shrinking quorum, one replica left)
    void downToOne();

    //! Set ResetLimit and load it
    void resetLimit(U8 limit);

    //! Cycles `from` to `to`, replicas 0 and 1 answering, replica 2 silent
    void twoAnswer(U64 from, U64 to);

    RedundancyManager component;
    const char* m_statePath;
    bool m_strikeAnswer;
    I32 m_enabled[Replicas];  //!< last enable value sent per replica: -1 none, 0 or 1
    FwIndexType m_requestPort[MAX_HISTORY_SIZE];
    I32 m_resetPort;  //!< port of the last reset sent, or -1
};

}  // namespace Mru

#endif
