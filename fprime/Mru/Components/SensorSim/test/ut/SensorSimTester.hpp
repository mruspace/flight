// ======================================================================
// \title  SensorSimTester.hpp
// \brief  Unit tests of SensorSim
// ======================================================================

#ifndef Mru_SensorSimTester_HPP
#define Mru_SensorSimTester_HPP

#include "Mru/Components/SensorSim/SensorSim.hpp"
#include "Mru/Components/SensorSim/SensorSimGTestBase.hpp"

namespace Mru {

class SensorSimTester final : public SensorSimGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 16;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    SensorSimTester();
    ~SensorSimTester();

    //! Off until SENSORS_ON; then readings within the noise of the truth (MRU-SS-001)
    void offUntilOn();
    //! Spike, drift, stuck and drop act on their sensor and cycles (MRU-SS-002)
    void faults();
    //! Values are scored against the truth; the plain average too (MRU-SS-003, 004)
    void scoring();

  private:
    void connectPorts();
    void initComponents();

    //! One cycle; returns the readings sent
    void cycle();
    void on(U32 noise, U32 accuracy);
    void fault(SensorFault::T kind, U8 sensor, U64 cycle, U32 cycles, I32 magnitude);

    //! The last readings sent
    const FromPortEntry_readingsOut& last() const;

    SensorSim component;
};

}  // namespace Mru

#endif
