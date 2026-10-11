// ======================================================================
// \title  HealthRegistryTester.hpp
// \brief  Unit tests of HealthRegistry
// ======================================================================

#ifndef Mru_HealthRegistryTester_HPP
#define Mru_HealthRegistryTester_HPP

#include "Mru/Components/HealthRegistry/HealthRegistry.hpp"
#include "Mru/Components/HealthRegistry/HealthRegistryGTestBase.hpp"

namespace Mru {

class HealthRegistryTester final : public HealthRegistryGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 10;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    HealthRegistryTester();
    ~HealthRegistryTester();

    //! Three outvotes within 200 cycles ask for a diagnosis (MRU-HR-001)
    void threeStrikesInWindow();

    //! Outvotes spread wider than the window do not (MRU-HR-001)
    void strikesOutsideWindow();

    //! A clear forgets the strikes (MRU-HR-002)
    void clearForgets();

    //! Strike counts are reported per replica (MRU-HR-003)
    void reportsStrikes();

    //! Replica indexes out of range change nothing
    void outOfRange();

  private:
    void connectPorts();
    void initComponents();

    //! Record an outvote and return the answer
    bool strike(FwIndexType replica, U64 cycle);

    //! Clear a replica
    void clear(FwIndexType replica);

    //! Assert the last strike telemetry
    void assertStrikes(U32 r0, U32 r1, U32 r2);

    HealthRegistry component;
};

}  // namespace Mru

#endif
