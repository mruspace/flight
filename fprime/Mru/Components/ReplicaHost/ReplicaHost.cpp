// ======================================================================
// \title  ReplicaHost.cpp
// \brief  Runs the payload for one replica and answers requests
// ======================================================================

#include "Mru/Components/ReplicaHost/ReplicaHost.hpp"

namespace Mru {

// definitions of the class constants (C++14)
constexpr FwSizeType ReplicaHost::BLOCK_WORDS;
constexpr U64 ReplicaHost::KNOWN_ANSWER_INPUT;

ReplicaHost ::ReplicaHost(const char* const compName) : ReplicaHostComponentBase(compName), m_block(), m_enabled(true) {}

ReplicaHost ::~ReplicaHost() {}

U64 ReplicaHost ::compute(U64 (&block)[BLOCK_WORDS], U64 input, bool flip, U16 word, U8 bit) {
    return Payload::compute(block, input, flip, word, bit);
}

U64 ReplicaHost ::expectedKnownAnswer() {
    return Payload::expectedKnownAnswer();
}

void ReplicaHost ::requestIn_handler(FwIndexType portNum,
                                     U64 cycle,
                                     const Mru::RequestKind& kind,
                                     const Mru::Upset& upset) {
    (void)portNum;
    if (!this->m_enabled) {
        return;
    }
    U64 first = 0;
    U64 second = 0;
    switch (kind.e) {
        case RequestKind::COMPUTE:
            first = compute(this->m_block, cycle, upset.get_first(), upset.get_word(), upset.get_bit());
            break;
        case RequestKind::COMPUTE_TWICE:
            first = compute(this->m_block, cycle, upset.get_first(), upset.get_word(), upset.get_bit());
            second = compute(this->m_block, cycle, upset.get_second(), upset.get_word(), upset.get_bit());
            break;
        case RequestKind::KNOWN_ANSWER:
            first = compute(this->m_block, KNOWN_ANSWER_INPUT, false, 0, 0);
            break;
        default:
            // not a kind this model defines: no answer
            return;
    }
    this->replyOut_out(0, cycle, kind, first, second);
}

void ReplicaHost ::enableIn_handler(FwIndexType portNum, bool enabled) {
    (void)portNum;
    this->m_enabled = enabled;
}

void ReplicaHost ::pingIn_handler(FwIndexType portNum, U32 key) {
    (void)portNum;
    this->pingOut_out(0, key);
}

}  // namespace Mru
