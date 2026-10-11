// ======================================================================
// \title  ReplicaHostTester.hpp
// \brief  Unit tests of ReplicaHost
// ======================================================================

#ifndef Mru_ReplicaHostTester_HPP
#define Mru_ReplicaHostTester_HPP

#include "Mru/Components/ReplicaHost/ReplicaHost.hpp"
#include "Mru/Components/ReplicaHost/ReplicaHostGTestBase.hpp"

namespace Mru {

class ReplicaHostTester final : public ReplicaHostGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 10;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    ReplicaHostTester();
    ~ReplicaHostTester();

    //! One result for the cycle, the demo's value (MRU-RH-001)
    void computesTheCycle();

    //! Two results for self-check; an upset flips the first only, or both (MRU-RH-002)
    void computesTwice();

    //! The known answer, and the expected value the manager is given (MRU-RH-003)
    void knownAnswer();

    //! A disabled replica does not answer; enabled again, it does (MRU-RH-004)
    void disabledIsSilent();

    //! Health ping is returned
    void ping();

  private:
    void connectPorts();
    void initComponents();

    //! Send a request and dispatch it
    void request(U64 cycle, RequestKind::T kind, const Upset& upset);

    //! Enable or disable, and dispatch
    void enable(bool enabled);

    ReplicaHost component;
};

}  // namespace Mru

#endif
