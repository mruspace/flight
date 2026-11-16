// ======================================================================
// \title  MruQuorumTopology.cpp
// \brief  Set up, run and tear down the MruQuorum topology
// ======================================================================
#include "MruQuorum/Top/MruQuorumTopologyAc.hpp"
#include "Fw/Types/MallocAllocator.hpp"
#include "Fw/Types/String.hpp"
#include "Mru/Payload/Payload.hpp"

#include <atomic>
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

//! Replies a replica can send in one cycle: its result, a known-answer
//! test, and one late from the cycle before, with room to spare
constexpr U32 MESSAGES_PER_CYCLE = 4;

//! What one link let through since the start of the cycle
std::atomic<U32> g_linkMessages[3];

//! The manager's check on every message from a replica (link0 to link2).
//! A replica sends one kind of message: a ReplicaReply on hub port 0. A
//! message that is not exactly that, or one too many this cycle, is dropped
//! on the link: to the manager the replica is silent, and it is retired as
//! lost. Without this check, a reply that a corrupted replica malformed
//! reached F´'s generated port code, which asserts on it and ends the
//! manager (found by the bit flips of run 4, segment 6).
bool validReply(void* context, const U8* data, FwSizeType size) {
    std::atomic<U32>& count = *static_cast<std::atomic<U32>*>(context);
    if (data == nullptr || size == 0 || ++count > MESSAGES_PER_CYCLE) {
        return false;
    }
    Fw::ExternalSerializeBuffer in(const_cast<U8*>(data), size);
    if (in.setBuffLen(size) != Fw::FW_SERIALIZE_OK) {
        return false;
    }
    U32 type = 0;
    U32 port = 0;
    FwBuffSizeType length = 0;
    U64 cycle = 0;
    U64 first = 0;
    U64 second = 0;
    Mru::RequestKind kind;
    const FwSizeType header = sizeof(U32) + sizeof(U32) + sizeof(FwBuffSizeType);
    return (in.deserializeTo(type) == Fw::FW_SERIALIZE_OK) && (type == Svc::GenericHub::HUB_TYPE_PORT) &&
           (in.deserializeTo(port) == Fw::FW_SERIALIZE_OK) && (port == 0) &&
           (in.deserializeTo(length) == Fw::FW_SERIALIZE_OK) && (size >= header) &&
           (static_cast<FwSizeType>(length) == size - header) && (in.deserializeTo(cycle) == Fw::FW_SERIALIZE_OK) &&
           (in.deserializeTo(kind) == Fw::FW_SERIALIZE_OK) && kind.isValid() &&
           (in.deserializeTo(first) == Fw::FW_SERIALIZE_OK) && (in.deserializeTo(second) == Fw::FW_SERIALIZE_OK) &&
           (in.getDeserializeSizeLeft() == 0);
}

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
    // check every message from a replica before the hub sees it
    link0.setFilter(validReply, &g_linkMessages[0]);
    link1.setFilter(validReply, &g_linkMessages[1]);
    link2.setFilter(validReply, &g_linkMessages[2]);
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

void newCycleOnLinks() {
    for (auto& count : g_linkMessages) {
        count = 0;
    }
}

U32 linkDropped(FwIndexType replica) {
    switch (replica) {
        case 0:
            return link0.dropped();
        case 1:
            return link1.dropped();
        case 2:
            return link2.dropped();
        default:
            return 0;
    }
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

Mru::SensorSim::Totals sensorTotals() {
    return sensorSim.totals();
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
