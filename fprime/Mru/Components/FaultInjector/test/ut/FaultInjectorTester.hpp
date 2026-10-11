// ======================================================================
// \title  FaultInjectorTester.hpp
// \brief  Unit tests of FaultInjector
// ======================================================================

#ifndef Mru_FaultInjectorTester_HPP
#define Mru_FaultInjectorTester_HPP

#include "Mru/Components/FaultInjector/FaultInjector.hpp"
#include "Mru/Components/FaultInjector/FaultInjectorGTestBase.hpp"

namespace Mru {

class FaultInjectorTester final : public FaultInjectorGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 40;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    FaultInjectorTester();
    ~FaultInjectorTester();

    //! Without faults, requests and replies pass unchanged
    void passThrough();

    //! Upsets follow the demo's generator, draw for draw (MRU-FI-002)
    void upsetsLikeTheDemo();

    //! A killed replica gets nothing from its cycle on, and draws nothing (MRU-FI-001)
    void kill();

    //! A hung replica draws once, as the demo's last ask, then gets nothing (MRU-FI-001)
    void hang();

    //! A stuck replica's answers become the stuck value from its cycle (MRU-FI-001)
    void stuck();

    //! A corrupt fault flips a bit in exactly its cycle (MRU-FI-001)
    void corrupt();

    //! Clearing a replica's faults models new hardware
    void clearFaults();

    //! Commands with bad arguments are refused
    void refusals();

  private:
    void connectPorts();
    void initComponents();

    //! Command helpers
    void inject(FaultKind::T kind, U8 replica, U64 cycle);
    void setUpsets(F64 rate, F64 commonMode, U64 seed);

    //! Send one request on `replica`
    void request(FwIndexType replica, U64 cycle, RequestKind::T kind);

    //! Assert that the last request was forwarded with this upset
    void assertForwarded(FwIndexType replica, U64 cycle, RequestKind::T kind, const Upset& upset);

    //! Record the port of each forwarded request, then keep the history
    void from_requestOut_handler(FwIndexType portNum,
                                 U64 cycle,
                                 const Mru::RequestKind& kind,
                                 const Mru::Upset& upset) override;

    FaultInjector component;
    FwIndexType m_lastRequestPort;
};

}  // namespace Mru

#endif
