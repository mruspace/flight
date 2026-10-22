// ======================================================================
// \title  Payload.hpp
// \brief  The replicas' task: the demo's hash over a 4 KiB block (D6)
//
// A module of its own so that a deployment can check results without the
// ReplicaHost component: the manager process of OPS-SAT run 3 has the
// replicas in other processes, and RunRecorder needs only the task.
// ======================================================================

#ifndef Mru_Payload_HPP
#define Mru_Payload_HPP

#include <Fw/FPrimeBasicTypes.hpp>

namespace Mru {
namespace Payload {

//! Words in the working block: 4 KiB, as in the quorum demo
constexpr FwSizeType BLOCK_WORDS = 512;

//! Input of the known-answer test
constexpr U64 KNOWN_ANSWER_INPUT = 0;

//! Fill the working block for `input`, apply an optional bit flip in it (as
//! radiation would), and hash it. The same task as the demo.
U64 compute(U64 (&block)[BLOCK_WORDS], U64 input, bool flip, U16 word, U8 bit);

//! The correct result of the known-answer test (D7)
U64 expectedKnownAnswer();

}  // namespace Payload
}  // namespace Mru

#endif
