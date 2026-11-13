// ======================================================================
// \title  SensorVoter.cpp
// \brief  Chooses one value from up to three sensors within a tolerance.
//         Every decision comes from the quorum crate (MRU-SV-001): the
//         choice (quorum_sensor_select) and when a sensor that keeps
//         disagreeing is excluded (quorum_sensor_observe).
// ======================================================================

#include "Mru/Components/SensorVoter/SensorVoter.hpp"

namespace Mru {

constexpr U32 SensorVoter::DEFAULT_TOLERANCE;
constexpr U8 SensorVoter::DEFAULT_EXCLUDE_AFTER;

static_assert(Sensors == QUORUM_SENSORS, "the model and the core count the same sensors");

SensorVoter ::SensorVoter(const char* const compName)
    : SensorVoterComponentBase(compName),
      m_excluded(),
      m_run(),
      m_agreed(0),
      m_unchecked(0),
      m_disagreed(0),
      m_outliers(0) {
    for (FwIndexType i = 0; i < Sensors; i++) {
        this->m_excluded[i] = false;
        this->m_run[i] = 0;
    }
}

SensorVoter ::~SensorVoter() {}

U32 SensorVoter ::tolerance() {
    Fw::ParamValid valid;
    const U32 t = this->paramGet_Tolerance(valid);
    return (valid == Fw::ParamValid::VALID || valid == Fw::ParamValid::DEFAULT) ? t : DEFAULT_TOLERANCE;
}

U8 SensorVoter ::excludeAfter() {
    Fw::ParamValid valid;
    const U8 n = this->paramGet_ExcludeAfter(valid);
    return (valid == Fw::ParamValid::VALID || valid == Fw::ParamValid::DEFAULT) ? n : DEFAULT_EXCLUDE_AFTER;
}

void SensorVoter ::readingsIn_handler(FwIndexType portNum,
                                      U64 cycle,
                                      const Mru::SensorValues& values,
                                      const Mru::SensorFlags& present) {
    (void)portNum;
    // an excluded sensor's reading is not used (MRU-SV-004)
    quorum_reading readings[Sensors];
    for (FwIndexType i = 0; i < Sensors; i++) {
        readings[i].present = present[i] && !this->m_excluded[i];
        readings[i].value = values[i];
    }
    const quorum_sensor_selection s = quorum_sensor_select(readings, this->tolerance());

    switch (s.outcome) {
        case QUORUM_SENSOR_AGREED: {
            this->m_agreed++;
            const U8 limit = this->excludeAfter();
            // outlier counts move only in cycles with an agreed value
            for (FwIndexType i = 0; i < Sensors; i++) {
                if (!readings[i].present) {
                    continue;
                }
                if (s.outlier[i]) {
                    this->m_outliers++;
                    this->log_WARNING_LO_SensorOutlier(static_cast<U8>(i), cycle, values[i], s.value);
                }
                const quorum_sensor_run r = quorum_sensor_observe(this->m_run[i], s.outlier[i], limit);
                this->m_run[i] = r.run;
                if (r.exclude) {
                    this->m_excluded[i] = true;
                    this->log_WARNING_HI_SensorExcluded(static_cast<U8>(i), cycle, r.run);
                }
            }
            if (this->isConnected_selectedOut_OutputPort(0)) {
                this->selectedOut_out(0, cycle, s.value, true);
            }
            break;
        }
        case QUORUM_SENSOR_UNCHECKED:
            this->m_unchecked++;
            this->log_WARNING_LO_UncheckedValue(cycle);
            if (this->isConnected_selectedOut_OutputPort(0)) {
                this->selectedOut_out(0, cycle, s.value, false);
            }
            break;
        case QUORUM_SENSOR_DISAGREE:
            this->m_disagreed++;
            this->log_WARNING_LO_SensorsDisagree(cycle);
            break;
        default:
            break;  // no reading: nothing to choose
    }
    this->report();
}

void SensorVoter ::INCLUDE_SENSOR_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 sensor) {
    if (sensor >= Sensors || !this->m_excluded[sensor]) {
        this->log_WARNING_LO_IncludeRefused(sensor);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->m_excluded[sensor] = false;
    this->m_run[sensor] = 0;
    this->log_ACTIVITY_HI_SensorIncluded(sensor);
    this->report();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SensorVoter ::report() {
    this->tlmWrite_Agreed(this->m_agreed);
    this->tlmWrite_Unchecked(this->m_unchecked);
    this->tlmWrite_Disagreed(this->m_disagreed);
    this->tlmWrite_Outliers(this->m_outliers);
    this->tlmWrite_Excluded(SensorFlags(this->m_excluded));
}

}  // namespace Mru
