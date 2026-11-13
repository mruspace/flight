// ======================================================================
// \title  SensorSim.cpp
// \brief  Test builds only: three sensors of one known quantity, with
//         noise and faults, scored against the true value
// ======================================================================

#include "Mru/Components/SensorSim/SensorSim.hpp"

namespace Mru {

constexpr FwSizeType SensorSim::MAX_FAULTS;

namespace {

constexpr U64 SEED_MULTIPLIER = 0x9E3779B97F4A7C15ULL;  // as FaultInjector
constexpr U64 SEED_OFFSET = 7;
constexpr U64 PERIOD = 400;  // cycles per triangle
constexpr I32 BASE = 20000;
constexpr I32 SLOPE = 10;    // per cycle

I32 clamp(I64 v) {
    const I64 lo = -2147483647LL - 1;
    const I64 hi = 2147483647LL;
    return static_cast<I32>(v < lo ? lo : (v > hi ? hi : v));
}

}  // namespace

SensorSim ::SensorSim(const char* const compName)
    : SensorSimComponentBase(compName),
      m_faults(),
      m_rng(SEED_OFFSET),
      m_noise(0),
      m_accuracy(0),
      m_cycle(0),
      m_valueThisCycle(false),
      m_totals() {
    for (FwSizeType i = 0; i < MAX_FAULTS; i++) {
        this->m_faults[i] = Fault{SensorFault(), 0, 0, 0, 0, false, false, 0};
    }
    this->m_totals = Totals{false, 0, 0, 0, 0, 0, 0};
}

SensorSim ::~SensorSim() {}

I32 SensorSim ::truth(U64 cycle) {
    const U64 t = cycle % PERIOD;
    const U64 up = (t < PERIOD / 2) ? t : (PERIOD - t);
    return BASE + SLOPE * static_cast<I32>(up);
}

SensorSim::Totals SensorSim ::totals() const {
    return this->m_totals;
}

U64 SensorSim ::next() {
    this->m_rng ^= this->m_rng << 13;
    this->m_rng ^= this->m_rng >> 7;
    this->m_rng ^= this->m_rng << 17;
    return this->m_rng;
}

bool SensorSim ::within(I64 a, I64 b, U32 limit) {
    const I64 d = a - b;
    return (d < 0 ? -d : d) <= static_cast<I64>(limit);
}

void SensorSim ::schedIn_handler(FwIndexType portNum, U32 context) {
    (void)portNum;
    (void)context;
    if (!this->m_totals.on) {
        return;
    }
    this->m_cycle++;
    const U64 c = this->m_cycle;
    const I32 truth = SensorSim::truth(c);

    I64 reading[Sensors];
    bool present[Sensors];
    for (FwIndexType i = 0; i < Sensors; i++) {
        // the same draw for every sensor, in order, every cycle: the noise
        // does not depend on the faults
        const U64 span = 2ULL * this->m_noise + 1ULL;
        const I64 noise = static_cast<I64>(this->next() % span) - static_cast<I64>(this->m_noise);
        reading[i] = static_cast<I64>(truth) + noise;
        present[i] = true;
    }
    for (FwSizeType n = 0; n < MAX_FAULTS; n++) {
        Fault& f = this->m_faults[n];
        if (!f.used || c < f.cycle) {
            continue;
        }
        const U64 age = c - f.cycle;  // 0 in the first cycle
        const FwIndexType i = f.sensor;
        bool active = true;
        switch (f.kind.e) {
            case SensorFault::SPIKE:
                active = age < f.cycles;
                if (active) {
                    reading[i] += f.magnitude;
                }
                break;
            case SensorFault::DRIFT: {
                const U64 ramp = (f.cycles == 0) ? 1 : f.cycles;
                const U64 k = (age + 1 < ramp) ? (age + 1) : ramp;
                reading[i] += static_cast<I64>(f.magnitude) * static_cast<I64>(k) / static_cast<I64>(ramp);
                break;
            }
            case SensorFault::STUCK:
                if (age == 0) {
                    f.frozen = reading[i];
                }
                reading[i] = f.frozen;
                break;
            case SensorFault::DROP:
                active = age < f.cycles;
                if (active) {
                    present[i] = false;
                }
                break;
            default:
                active = false;
                break;
        }
        if (active && !f.announced) {
            f.announced = true;
            this->log_WARNING_LO_SensorFaultApplied(f.kind, f.sensor, c);
        }
    }

    SensorValues values;
    SensorFlags flags;
    I64 sum = 0;
    I64 count = 0;
    for (FwIndexType i = 0; i < Sensors; i++) {
        values[i] = clamp(reading[i]);
        flags[i] = present[i];
        if (present[i]) {
            sum += values[i];
            count++;
        }
    }
    this->m_totals.cycles++;
    // the baseline: a plain average of every reading, with no checks
    if (count > 0 && !within(sum / count, truth, this->m_accuracy)) {
        this->m_totals.averageWrong++;
    }
    this->m_valueThisCycle = false;
    if (this->isConnected_readingsOut_OutputPort(0)) {
        this->readingsOut_out(0, c, values, flags);
    }
    if (!this->m_valueThisCycle) {
        this->m_totals.withheld++;
    }
    this->report();
}

void SensorSim ::selectedIn_handler(FwIndexType portNum, U64 cycle, I32 value, bool checked) {
    (void)portNum;
    this->m_valueThisCycle = true;
    if (!checked) {
        this->m_totals.unchecked++;
        return;
    }
    const I32 truth = SensorSim::truth(cycle);
    if (within(value, truth, this->m_accuracy)) {
        this->m_totals.correct++;
    } else {
        this->m_totals.wrong++;
        this->log_WARNING_HI_WrongValue(cycle, value, truth);
    }
}

void SensorSim ::SENSORS_ON_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 noise, U32 accuracy, U64 seed) {
    this->m_noise = noise;
    this->m_accuracy = accuracy;
    this->m_rng = (seed * SEED_MULTIPLIER) + SEED_OFFSET;
    this->m_totals.on = true;
    this->log_ACTIVITY_HI_SensorsOn(noise, accuracy, seed);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SensorSim ::SENSOR_FAULT_cmdHandler(FwOpcodeType opCode,
                                         U32 cmdSeq,
                                         const Mru::SensorFault& kind,
                                         U8 sensor,
                                         U64 cycle,
                                         U32 cycles,
                                         I32 magnitude) {
    if (sensor < Sensors && kind.isValid()) {
        for (FwSizeType i = 0; i < MAX_FAULTS; i++) {
            if (!this->m_faults[i].used) {
                this->m_faults[i] = Fault{kind, sensor, cycle, cycles, magnitude, true, false, 0};
                this->log_ACTIVITY_HI_SensorFaultScheduled(kind, sensor, cycle, cycles, magnitude);
                this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
                return;
            }
        }
    }
    this->log_WARNING_LO_SensorFaultRefused(sensor);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
}

void SensorSim ::report() {
    this->tlmWrite_Correct(this->m_totals.correct);
    this->tlmWrite_Wrong(this->m_totals.wrong);
    this->tlmWrite_UncheckedValues(this->m_totals.unchecked);
    this->tlmWrite_Withheld(this->m_totals.withheld);
    this->tlmWrite_AverageWrong(this->m_totals.averageWrong);
}

}  // namespace Mru
