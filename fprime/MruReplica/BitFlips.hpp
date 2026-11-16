// ======================================================================
// \title  BitFlips.hpp
// \brief  Real bit flips in a replica process's own memory (run 4,
//         segment 6)
//
// The other faults of the runs are injected at the symptom: FaultInjector
// changes what a replica returns, or the manager kills or stops a process.
// These flips are injected at the cause. A thread in the replica process
// flips one random bit at a time in the process's own memory, as a particle
// would: code, read-only data, data, heap and stacks, chosen in proportion
// to their size, on pages that are in memory. Nothing tells the manager
// where a flip landed or what it did. Most flips hit memory that is never
// read again; some change a result, stop the replica, crash it or make it
// spin. The manager sees only what comes out of the link.
//
// Settings (environment, read once at start; off when MRU_FLIPS is unset):
//   MRU_FLIPS          "rate:seed:delay_ms": flips per second (at random
//                      times, at that mean rate), the random seed (mixed
//                      with the replica number) and the time before the
//                      first flip
//   MRU_FLIPS_LOG      log path prefix: replica N writes PREFIX N .txt,
//                      one line per flip, written before the flip
//   MRU_FLIPS_MAX      flips at most, per replica (default 2000)
//   MRU_CPU_SECONDS    CPU time limit of the replica process (RLIMIT_CPU),
//                      so a replica that a flip sets spinning ends
// With flips on, files the process writes are limited to 256 KB
// (RLIMIT_FSIZE); core files are always off (RLIMIT_CORE 0).
// ======================================================================

#ifndef MruReplica_BitFlips_HPP
#define MruReplica_BitFlips_HPP

namespace MruReplica {

//! Set the limits a flipped process needs (no core file; CPU and file size
//! when flips are on) and start the flip thread if MRU_FLIPS is set.
//! Returns false only if the settings are malformed.
bool startBitFlips(int replica);

}  // namespace MruReplica

#endif
