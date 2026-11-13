// ======================================================================
// \title  SensorVoter.hpp
// \brief  Chooses one value from up to three sensors within a tolerance.
//         Every decision comes from the quorum crate (MRU-SV-001).
// ======================================================================

#ifndef Mru_SensorVoter_HPP
#define Mru_SensorVoter_HPP

#include "Mru/Components/SensorVoter/SensorVoterComponentAc.hpp"
#include "Mru/Types/FppConstantsAc.hpp"
#include "quorum.h"

namespace Mru {

class SensorVoter final : public SensorVoterComponentBase {
  public:
    //! Parameter defaults, as in the model: used when a parameter is not loaded
    static constexpr U32 DEFAULT_TOLERANCE = 0;
    static constexpr U8 DEFAULT_EXCLUDE_AFTER = 5;

    explicit SensorVoter(const char* const compName);
    ~SensorVoter() override;

    SensorVoter(const SensorVoter&) = delete;
    SensorVoter& operator=(const SensorVoter&) = delete;

  private:
    void readingsIn_handler(FwIndexType portNum,
                            U64 cycle,
                            const Mru::SensorValues& values,
                            const Mru::SensorFlags& present) override;
    void INCLUDE_SENSOR_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 sensor) override;

    //! The parameters, or their defaults when not loaded
    U32 tolerance();
    U8 excludeAfter();

    void report();

    bool m_excluded[Sensors];
    U8 m_run[Sensors];  //!< outlier cycles in a row, per sensor
    U64 m_agreed;
    U64 m_unchecked;
    U64 m_disagreed;
    U64 m_outliers;
};

}  // namespace Mru

#endif
