// ======================================================================
// \title  RunRecorder.hpp
// \brief  Experiment builds only: checks delivered results, keeps the counts
// ======================================================================

#ifndef Mru_RunRecorder_HPP
#define Mru_RunRecorder_HPP

#include "Mru/Payload/Payload.hpp"
#include "Mru/Components/RunRecorder/RunRecorderComponentAc.hpp"
#include "Mru/Types/ModeEnumAc.hpp"
#include "Mru/Types/PolicyEnumAc.hpp"
#include "Mru/Types/SlotStatesArrayAc.hpp"

namespace Mru {

class RunRecorder final : public RunRecorderComponentBase {
  public:
    //! The counts of a run, and the manager's last telemetry
    struct Totals {
        U64 cycles;      //!< Rate group calls
        U64 correct;     //!< Delivered and correct
        U64 wrong;       //!< Delivered and wrong
        U64 lastCorrect; //!< Cycle of the last correct result, 0 if none
        U64 caught;      //!< The manager's Caught channel
        U64 late;        //!< The manager's Late channel: cycles withheld
        U8 live;         //!< The manager's LiveReplicas channel
        Mode mode;       //!< The manager's CurrentMode channel
        Policy policy;   //!< The manager's CurrentPolicy channel
        SlotStates slots; //!< The manager's Slots channel
        bool seqDone;    //!< The command sequence ended
        bool seqOk;      //!< ... with every command OK
    };

    //! Construct RunRecorder object
    explicit RunRecorder(const char* const compName);

    //! Destroy RunRecorder object
    ~RunRecorder() override;

    RunRecorder(const RunRecorder&) = delete;
    RunRecorder& operator=(const RunRecorder&) = delete;

    //! The manager's base ID, to find its channels in the telemetry database
    void configure(FwIdType managerIdBase);

    //! The counts so far and the manager's last telemetry
    Totals totals();

    //! Ask the command sequencer to run a sequence file
    void startSequence(const char* path);

    //! A function called with the cycle number at the start of every cycle,
    //! before the manager's: a deployment acts on the cycle there, e.g. a
    //! real fault in a replica process. nullptr for none.
    void setCycleHook(void (*hook)(U64 cycle));

  private:
    void resultIn_handler(FwIndexType portNum, U64 cycle, U64 value) override;
    void schedIn_handler(FwIndexType portNum, U32 context) override;
    void seqDoneIn_handler(FwIndexType portNum,
                           FwOpcodeType opCode,
                           U32 cmdSeq,
                           const Fw::CmdResponse& response) override;

    //! Read one of the manager's channels; false if it has no value yet
    bool readChannel(FwChanIdType localId, Fw::TlmBuffer& val);

    FwIdType m_managerIdBase;
    void (*m_cycleHook)(U64 cycle);
    Totals m_totals;
    U64 m_block[Payload::BLOCK_WORDS];
};

}  // namespace Mru

#endif
