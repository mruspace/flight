// ======================================================================
// \title  MruQuorumTopology.hpp
// \brief  Set up, run and tear down the MruQuorum topology
// ======================================================================
#ifndef MRUQUORUM_MRUQUORUMTOPOLOGY_HPP
#define MRUQUORUM_MRUQUORUMTOPOLOGY_HPP

#include "Mru/Components/RunRecorder/RunRecorder.hpp"
#include "MruQuorum/Top/MruQuorumTopologyDefs.hpp"

namespace MruQuorum {

//! Initialize, connect, configure and start the components
void setupTopology(const TopologyState& state);

//! Ask the command sequencer to run a sequence file
void runSequence(const char* path);

//! True when the links to all three replica processes are open
bool linksOpen();

//! Called with the cycle number at the start of every cycle, before the manager
void setCycleHook(void (*hook)(U64 cycle));

//! The counts and the manager's last telemetry
Mru::RunRecorder::Totals totals();

//! Drive the rate groups from the Linux timer; blocks until stopRateGroups()
void startRateGroups(const TopologyState& state);

//! Stop the timer; startRateGroups() returns after the current cycle
void stopRateGroups();

//! Stop and join the tasks, free their resources
void teardownTopology(const TopologyState& state);

}  // namespace MruQuorum

#endif
