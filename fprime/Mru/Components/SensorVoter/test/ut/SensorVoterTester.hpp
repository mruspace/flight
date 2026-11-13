// ======================================================================
// \title  SensorVoterTester.hpp
// \brief  Unit tests of SensorVoter
// ======================================================================

#ifndef Mru_SensorVoterTester_HPP
#define Mru_SensorVoterTester_HPP

#include "Mru/Components/SensorVoter/SensorVoter.hpp"
#include "Mru/Components/SensorVoter/SensorVoterGTestBase.hpp"

namespace Mru {

class SensorVoterTester final : public SensorVoterGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    SensorVoterTester();
    ~SensorVoterTester();

    //! Three readings within the tolerance: the median, checked (MRU-SV-002, 003)
    void agreedMedian();
    //! A spike is an outlier: not chosen, reported, not excluded (MRU-SV-004)
    void spikeIsMaskedNotExcluded();
    //! An outlier for ExcludeAfter cycles in a row is excluded, and its readings ignored (MRU-SV-004)
    void persistentOutlierIsExcluded();
    //! Two that disagree: nothing delivered, outlier counts kept (MRU-SV-003)
    void disagreementDeliversNothing();
    //! One reading: delivered unchecked (MRU-SV-005)
    void singleReadingIsUnchecked();
    //! INCLUDE_SENSOR brings an excluded sensor back, and refuses others (MRU-SV-006)
    void includeSensor();

  private:
    void connectPorts();
    void initComponents();

    //! Readings for one cycle; `present` false for no reading
    void readings(U64 cycle, I32 a, I32 b, I32 c, bool pa = true, bool pb = true, bool pc = true);

    //! Set the parameters and load them
    void params(U32 tolerance, U8 excludeAfter);

    SensorVoter component;
};

}  // namespace Mru

#endif
