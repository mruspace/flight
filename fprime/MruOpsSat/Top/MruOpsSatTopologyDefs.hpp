// ======================================================================
// \title  MruOpsSatTopologyDefs.hpp
// \brief  Definitions the MruOpsSat topology autocoder requires
// ======================================================================
#ifndef MRUOPSSAT_MRUOPSSATTOPOLOGYDEFS_HPP
#define MRUOPSSAT_MRUOPSSATTOPOLOGYDEFS_HPP

#include "MruOpsSat/Top/FppConstantsAc.hpp"
#include "MruOpsSat/Top/Ports_RateGroupsEnumAc.hpp"
#include "Svc/Subtopologies/CdhCore/PingEntries.hpp"
#include "Svc/Subtopologies/CdhCore/SubtopologyTopologyDefs.hpp"

//! Missed health pings before a warning and before a fatal event, per active
//! component of the main topology. A replica that a fault stops answering
//! keeps answering pings: the fault drops its requests, not its thread.
namespace PingEntries {
namespace MruOpsSat_rateGroupFast {
enum { WARN = 3, FATAL = 5 };
}
namespace MruOpsSat_rateGroupSlow {
enum { WARN = 3, FATAL = 5 };
}
namespace MruOpsSat_cmdSeq {
enum { WARN = 3, FATAL = 5 };
}
namespace MruOpsSat_manager {
enum { WARN = 3, FATAL = 5 };
}
namespace MruOpsSat_host0 {
enum { WARN = 3, FATAL = 5 };
}
namespace MruOpsSat_host1 {
enum { WARN = 3, FATAL = 5 };
}
namespace MruOpsSat_host2 {
enum { WARN = 3, FATAL = 5 };
}
}  // namespace PingEntries

namespace MruOpsSat {

//! What Main.cpp passes to the topology
struct TopologyState {
    const char* statePath;          //!< The manager's state file, "" for none
    U32 cycleMs;                    //!< The cycle period in milliseconds
    CdhCore::SubtopologyState cdhCore;
};

namespace PingEntries = ::PingEntries;

}  // namespace MruOpsSat

#endif
