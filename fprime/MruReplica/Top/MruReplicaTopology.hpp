// ======================================================================
// \title  MruReplicaTopology.hpp
// \brief  Set up, run and tear down the MruReplica topology
// ======================================================================
#ifndef MRUREPLICA_MRUREPLICATOPOLOGY_HPP
#define MRUREPLICA_MRUREPLICATOPOLOGY_HPP

#include "MruReplica/Top/MruReplicaTopologyDefs.hpp"

namespace MruReplica {

//! Initialize, connect, configure and start the components and the link
void setupTopology(const TopologyState& state);

//! True once the link has opened and while it stays open
bool linkOpen();

//! Stop and join the tasks, free their resources
void teardownTopology(const TopologyState& state);

}  // namespace MruReplica

#endif
