// ======================================================================
// \title  MruOpsSatTopology.cpp
// \brief  Set up, run and tear down the MruOpsSat topology
// ======================================================================
#include "MruOpsSat/Top/MruOpsSatTopologyAc.hpp"
#include "Fw/Types/MallocAllocator.hpp"
#include "Fw/Types/String.hpp"
#include "Mru/Components/ReplicaHost/ReplicaHost.hpp"

namespace MruOpsSat {

namespace {

Fw::MallocAllocator mallocator;
Svc::ActiveRateGroup::ContextArray rateGroupFastContext(0);
Svc::ActiveRateGroup::ContextArray rateGroupSlowContext(0);

//! Room for the sequence file: a few commands
constexpr FwSizeType SEQUENCE_BUFFER = 5 * 1024;

void configureTopology(const TopologyState& state) {
    // the fast group runs every cycle, the slow one once a second
    Svc::RateGroupDriver::DividerSet dividers;
    const FwSizeType perSecond = (state.cycleMs > 0 && state.cycleMs < 1000) ? 1000 / state.cycleMs : 1;
    dividers.dividers[Ports_RateGroups::rateGroupFast] = Svc::RateGroupDriver::Divider(1, 0);
    dividers.dividers[Ports_RateGroups::rateGroupSlow] = Svc::RateGroupDriver::Divider(perSecond, 0);
    rateGroupDriver.configure(dividers);
    rateGroupFast.configure(rateGroupFastContext);
    rateGroupSlow.configure(rateGroupSlowContext);
    cmdSeq.allocateBuffer(0, mallocator, SEQUENCE_BUFFER);
    // parameters are not loaded (no parameter database): the manager uses
    // the model's defaults, a known-answer test every 64 cycles and a state
    // save every 1,000
    manager.configure(Mru::ReplicaHost::expectedKnownAnswer(), state.statePath);
    recorder.configure(manager.getIdBase());
}

}  // namespace

void setupTopology(const TopologyState& state) {
    initComponents(state);
    setBaseIds();
    connectComponents();
    regCommands();
    configComponents(state);
    configureTopology(state);
    startTasks(state);
}

void runSequence(const char* path) {
    recorder.startSequence(path);
}

Mru::RunRecorder::Totals totals() {
    return recorder.totals();
}

void startRateGroups(const TopologyState& state) {
    timer.startTimer(Fw::TimeInterval(state.cycleMs / 1000, (state.cycleMs % 1000) * 1000));
}

void stopRateGroups() {
    timer.quit();
}

void teardownTopology(const TopologyState& state) {
    stopTasks(state);
    freeThreads(state);
    cmdSeq.deallocateBuffer(mallocator);
    tearDownComponents(state);
    deinitComponents(state);
}

}  // namespace MruOpsSat
