// ======================================================================
// \title  MruQuorumTopologyDefs.hpp
// \brief  Definitions the MruQuorum topology autocoder requires
// ======================================================================
#ifndef MRUQUORUM_MRUQUORUMTOPOLOGYDEFS_HPP
#define MRUQUORUM_MRUQUORUMTOPOLOGYDEFS_HPP

#include "MruQuorum/Top/FppConstantsAc.hpp"
#include "MruQuorum/Top/Ports_RateGroupsEnumAc.hpp"
#include "Svc/Subtopologies/CdhCore/PingEntries.hpp"
#include "Svc/Subtopologies/CdhCore/SubtopologyTopologyDefs.hpp"

//! Missed health pings before a warning and before a fatal event, per active
//! component of the main topology. The replicas are processes of their own
//! and are not pinged: a replica that stops is found by its silence.
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

namespace PingEntries {
namespace MruQuorum_rateGroupFast {
enum { WARN = 3, FATAL = 5 };
}
namespace MruQuorum_rateGroupSlow {
enum { WARN = 3, FATAL = 5 };
}
namespace MruQuorum_cmdSeq {
enum { WARN = 3, FATAL = 5 };
}
namespace MruQuorum_manager {
enum { WARN = 3, FATAL = 5 };
}
}  // namespace PingEntries

namespace MruQuorum {

//! What Main.cpp passes to the topology
struct TopologyState {
    const char* statePath;          //!< The manager's state file, "" for none
    U32 cycleMs;                    //!< The cycle period in milliseconds
    int fds[3];                     //!< This process's end of each replica's socket pair
    CdhCore::SubtopologyState cdhCore;
};

namespace PingEntries = ::PingEntries;

}  // namespace MruQuorum

#endif
