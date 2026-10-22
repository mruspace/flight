// ======================================================================
// \title  MruReplicaTopologyDefs.hpp
// \brief  Definitions the MruReplica topology autocoder requires
// ======================================================================
#ifndef MRUREPLICA_MRUREPLICATOPOLOGYDEFS_HPP
#define MRUREPLICA_MRUREPLICATOPOLOGYDEFS_HPP


#include "Mru/Ports/ReplicaEnablePortAc.hpp"
#include "Mru/Ports/ReplicaReplyPortAc.hpp"
#include "Mru/Ports/ReplicaRequestPortAc.hpp"

// F´'s topology autocoder (v4.4.1, direct port calls) writes the hub's
// serial-to-typed calls inside namespace Svc, and names the port serializer
// without its module. These make the Mru serializers visible there.
namespace Svc {
using Mru::ReplicaEnablePortSerializer;
using Mru::ReplicaReplyPortSerializer;
using Mru::ReplicaRequestPortSerializer;
}  // namespace Svc

namespace MruReplica {

//! What Main.cpp passes to the topology
struct TopologyState {
    int fd;  //!< This process's end of the socket pair
};

}  // namespace MruReplica

#endif
