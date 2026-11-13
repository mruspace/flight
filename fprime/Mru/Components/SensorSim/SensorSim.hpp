// ======================================================================
// \title  SensorSim.hpp
// \brief  Test builds only: three sensors of one known quantity, with
//         noise and faults, scored against the true value
// ======================================================================

#ifndef Mru_SensorSim_HPP
#define Mru_SensorSim_HPP

#include "Mru/Components/SensorSim/SensorSimComponentAc.hpp"
#include "Mru/Types/FppConstantsAc.hpp"

namespace Mru {

class SensorSim final : public SensorSimComponentBase {
  public:
    //! Faults that can be scheduled at once
    static constexpr FwSizeType MAX_FAULTS = 8;

    //! The counts of a run
    struct Totals {
        bool on;          //!< SENSORS_ON was received
        U64 cycles;       //!< Cycles with readings
        U64 correct;      //!< Checked values within the accuracy
        U64 wrong;        //!< Checked values outside it
        U64 unchecked;    //!< Unchecked values
        U64 withheld;     //!< Cycles with no value
        U64 averageWrong; //!< Cycles the plain average was outside the accuracy
    };

    explicit SensorSim(const char* const compName);
    ~SensorSim() override;

    SensorSim(const SensorSim&) = delete;
    SensorSim& operator=(const SensorSim&) = delete;

    //! The true value at a cycle: a triangle wave, 20,000 to 22,000 and back
    //! every 400 cycles (in hundredths of a unit: 200.00 to 220.00)
    static I32 truth(U64 cycle);

    //! The counts so far
    Totals totals() const;

  private:
    void schedIn_handler(FwIndexType portNum, U32 context) override;
    void selectedIn_handler(FwIndexType portNum, U64 cycle, I32 value, bool checked) override;
    void SENSORS_ON_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 noise, U32 accuracy, U64 seed) override;
    void SENSOR_FAULT_cmdHandler(FwOpcodeType opCode,
                                 U32 cmdSeq,
                                 const Mru::SensorFault& kind,
                                 U8 sensor,
                                 U64 cycle,
                                 U32 cycles,
                                 I32 magnitude) override;

    struct Fault {
        SensorFault kind;
        U8 sensor;
        U64 cycle;
        U32 cycles;
        I32 magnitude;
        bool used;
        bool announced;
        I64 frozen;  //!< a stuck sensor's reading, set in its first cycle
    };

    //! The demo's generator: xorshift64
    U64 next();

    //! Whether |a - b| <= limit, with no overflow
    static bool within(I64 a, I64 b, U32 limit);

    void report();

    Fault m_faults[MAX_FAULTS];
    U64 m_rng;
    U32 m_noise;
    U32 m_accuracy;
    U64 m_cycle;
    bool m_valueThisCycle;
    Totals m_totals;
};

}  // namespace Mru

#endif
