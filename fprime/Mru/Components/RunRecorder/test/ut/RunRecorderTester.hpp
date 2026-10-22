// ======================================================================
// \title  RunRecorderTester.hpp
// \brief  Unit tests of RunRecorder
// ======================================================================

#ifndef Mru_RunRecorderTester_HPP
#define Mru_RunRecorderTester_HPP

#include "Mru/Components/RunRecorder/RunRecorder.hpp"
#include "Mru/Components/RunRecorder/RunRecorderGTestBase.hpp"

namespace Mru {

class RunRecorderTester final : public RunRecorderGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 10;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    //! The base ID the tests give the manager
    static const FwIdType MANAGER_BASE = 0x4000;

    RunRecorderTester();
    ~RunRecorderTester();

    //! Correct and wrong results are counted and reported (MRU-RR-001, 002)
    void countsResults();

    //! The manager's telemetry is read for the totals (MRU-RR-003)
    void readsManagerTelemetry();

    //! Cycles are counted, the sequence's end is recorded (MRU-RR-003, 004)
    void countsCyclesAndSequence();

  private:
    void connectPorts();
    void initComponents();

    //! Answers reads of the telemetry database with the manager's channels
    Fw::TlmValid from_tlmGet_handler(FwIndexType portNum,
                                     FwChanIdType id,
                                     Fw::Time& timeTag,
                                     Fw::TlmBuffer& val) override;

    //! The result the payload gives for a cycle
    static U64 correctFor(U64 cycle);

    RunRecorder component;
};

}  // namespace Mru

#endif
