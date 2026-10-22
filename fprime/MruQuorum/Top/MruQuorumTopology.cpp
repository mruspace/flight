// ======================================================================
// \title  MruQuorumTopology.cpp
// \brief  Set up, run and tear down the MruQuorum topology
// ======================================================================
#include "MruQuorum/Top/MruQuorumTopologyAc.hpp"
#include "Fw/Types/MallocAllocator.hpp"
#include "Fw/Types/String.hpp"
#include "Mru/Payload/Payload.hpp"

#include <cstring>

namespace MruQuorum {

namespace {

Fw::MallocAllocator mallocator;
Svc::ActiveRateGroup::ContextArray rateGroupFastContext(0);
Svc::ActiveRateGroup::ContextArray rateGroupSlowContext(0);

//! Room for the sequence file: a few commands
constexpr FwSizeType SEQUENCE_BUFFER = 5 * 1024;

//! Hub messages are small: a request or a reply is under 100 bytes
constexpr FwSizeType MESSAGE_BYTES = 1024;
constexpr U16 MESSAGE_BUFFERS = 192;
constexpr U16 BUFFER_MANAGER_ID = 100;

//! Stack of each link thread
constexpr Os::Task::ParamType LINK_STACK = 128 * 1024;

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
    manager.configure(Mru::Payload::expectedKnownAnswer(), state.statePath);
    recorder.configure(manager.getIdBase());
    Svc::BufferManager::BufferBins bins;
    std::memset(&bins, 0, sizeof(bins));
    bins.bins[0].bufferSize = MESSAGE_BYTES;
    bins.bins[0].numBuffers = MESSAGE_BUFFERS;
    buffers.setup(BUFFER_MANAGER_ID, 0, mallocator, bins);
    (void)link0.configure(state.fds[0], MESSAGE_BYTES);
    (void)link1.configure(state.fds[1], MESSAGE_BYTES);
    (void)link2.configure(state.fds[2], MESSAGE_BYTES);
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
    // the links' read and reconnect threads, with small stacks (the default
    // is the C library's 8 MB per thread)
    link0.start(Fw::String("link0"), Os::Task::TASK_PRIORITY_DEFAULT, LINK_STACK, Os::Task::TASK_DEFAULT,
                Os::Task::TASK_PRIORITY_DEFAULT, LINK_STACK);
    link1.start(Fw::String("link1"), Os::Task::TASK_PRIORITY_DEFAULT, LINK_STACK, Os::Task::TASK_DEFAULT,
                Os::Task::TASK_PRIORITY_DEFAULT, LINK_STACK);
    link2.start(Fw::String("link2"), Os::Task::TASK_PRIORITY_DEFAULT, LINK_STACK, Os::Task::TASK_DEFAULT,
                Os::Task::TASK_PRIORITY_DEFAULT, LINK_STACK);
}

bool linksOpen() {
    return link0.isOpened() && link1.isOpened() && link2.isOpened();
}

void setCycleHook(void (*hook)(U64 cycle)) {
    recorder.setCycleHook(hook);
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
    link0.stop();
    link1.stop();
    link2.stop();
    (void)link0.join();
    (void)link1.join();
    (void)link2.join();
    stopTasks(state);
    freeThreads(state);
    cmdSeq.deallocateBuffer(mallocator);
    buffers.cleanup();
    tearDownComponents(state);
    deinitComponents(state);
}

}  // namespace MruQuorum
