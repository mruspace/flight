// ======================================================================
// \title  MruOpsSatTopology.hpp
// \brief  Set up, run and tear down the MruOpsSat topology
// ======================================================================
#ifndef MRUOPSSAT_MRUOPSSATTOPOLOGY_HPP
#define MRUOPSSAT_MRUOPSSATTOPOLOGY_HPP

#include "Mru/Components/RunRecorder/RunRecorder.hpp"
#include "MruOpsSat/Top/MruOpsSatTopologyDefs.hpp"

namespace MruOpsSat {

//! Initialize, connect, configure and start the components
void setupTopology(const TopologyState& state);

//! Ask the command sequencer to run a sequence file
void runSequence(const char* path);

//! The counts and the manager's last telemetry
Mru::RunRecorder::Totals totals();

//! Drive the rate groups from the Linux timer; blocks until stopRateGroups()
void startRateGroups(const TopologyState& state);

//! Stop the timer; startRateGroups() returns after the current cycle
void stopRateGroups();

//! Stop and join the tasks, free their resources
void teardownTopology(const TopologyState& state);

}  // namespace MruOpsSat

#endif
