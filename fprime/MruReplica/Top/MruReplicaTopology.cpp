// ======================================================================
// \title  MruReplicaTopology.cpp
// \brief  Set up, run and tear down the MruReplica topology
// ======================================================================
#include "MruReplica/Top/MruReplicaTopologyAc.hpp"

#include <cstring>

#include "Fw/Types/MallocAllocator.hpp"
#include "Fw/Types/String.hpp"

namespace MruReplica {

namespace {

Fw::MallocAllocator mallocator;

//! Hub messages are small: a request or a reply is under 100 bytes
constexpr FwSizeType MESSAGE_BYTES = 1024;
constexpr U16 MESSAGE_BUFFERS = 64;
constexpr U16 BUFFER_MANAGER_ID = 200;

//! Stack of each link thread
constexpr Os::Task::ParamType LINK_STACK = 128 * 1024;

void configureTopology(const TopologyState& state) {
    Svc::BufferManager::BufferBins bins;
    std::memset(&bins, 0, sizeof(bins));
    bins.bins[0].bufferSize = MESSAGE_BYTES;
    bins.bins[0].numBuffers = MESSAGE_BUFFERS;
    buffers.setup(BUFFER_MANAGER_ID, 0, mallocator, bins);
    (void)link.configure(state.fd, MESSAGE_BYTES);
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
    // the link's read and reconnect threads, with small stacks (the default
    // is the C library's 8 MB per thread)
    link.start(Fw::String("link"), Os::Task::TASK_PRIORITY_DEFAULT, LINK_STACK, Os::Task::TASK_DEFAULT,
               Os::Task::TASK_PRIORITY_DEFAULT, LINK_STACK);
}

bool linkOpen() {
    return link.isOpened();
}

void teardownTopology(const TopologyState& state) {
    link.stop();
    (void)link.join();
    stopTasks(state);
    freeThreads(state);
    buffers.cleanup();
    tearDownComponents(state);
    deinitComponents(state);
}

}  // namespace MruReplica
