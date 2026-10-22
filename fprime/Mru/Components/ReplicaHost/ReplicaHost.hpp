// ======================================================================
// \title  ReplicaHost.hpp
// \brief  Runs the payload for one replica and answers requests
// ======================================================================

#ifndef Mru_ReplicaHost_HPP
#define Mru_ReplicaHost_HPP

#include "Mru/Components/ReplicaHost/ReplicaHostComponentAc.hpp"
#include "Mru/Payload/Payload.hpp"

namespace Mru {

class ReplicaHost final : public ReplicaHostComponentBase {
  public:
    //! Words in the working block: 4 KiB, as in the quorum demo
    static constexpr FwSizeType BLOCK_WORDS = Payload::BLOCK_WORDS;

    //! Input of the known-answer test
    static constexpr U64 KNOWN_ANSWER_INPUT = Payload::KNOWN_ANSWER_INPUT;

    //! Construct ReplicaHost object
    explicit ReplicaHost(const char* const compName);

    //! Destroy ReplicaHost object
    ~ReplicaHost() override;

    ReplicaHost(const ReplicaHost&) = delete;
    ReplicaHost& operator=(const ReplicaHost&) = delete;

    //! The correct result of the known-answer test, for RedundancyManager (D7)
    static U64 expectedKnownAnswer();

    //! The payload: fill the working block for `input`, apply an optional bit
    //! flip in it (as radiation would), and hash it. The same task as the demo.
    static U64 compute(U64 (&block)[BLOCK_WORDS], U64 input, bool flip, U16 word, U8 bit);

  private:
    //! A request for one cycle (MRU-RH-001, 002, 003)
    void requestIn_handler(FwIndexType portNum, U64 cycle, const Mru::RequestKind& kind, const Mru::Upset& upset) override;

    //! Enable or disable: a disabled replica does not answer (MRU-RH-004)
    void enableIn_handler(FwIndexType portNum, bool enabled) override;

    //! Health ping
    void pingIn_handler(FwIndexType portNum, U32 key) override;

    //! Working memory, allocated with the component
    U64 m_block[BLOCK_WORDS];

    //! Whether this replica answers
    bool m_enabled;
};

}  // namespace Mru

#endif
