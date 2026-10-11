// ======================================================================
// \title  ReplicaHost.cpp
// \brief  Runs the payload for one replica and answers requests
// ======================================================================

#include "Mru/Components/ReplicaHost/ReplicaHost.hpp"

namespace Mru {

// definitions of the class constants (C++14)
constexpr FwSizeType ReplicaHost::BLOCK_WORDS;
constexpr U64 ReplicaHost::KNOWN_ANSWER_INPUT;

namespace {

constexpr U64 FILL_MULTIPLIER = 0x9E3779B97F4A7C15ULL;  // golden-ratio constant, as in the demo
constexpr U64 FNV_OFFSET = 0xcbf29ce484222325ULL;        // FNV-1a 64-bit offset basis
constexpr U64 FNV_PRIME = 0x00000100000001b3ULL;         // FNV-1a 64-bit prime
constexpr U32 BITS_PER_WORD = 64;
constexpr U32 BYTES_PER_WORD = 8;
constexpr U32 BITS_PER_BYTE = 8;
constexpr U64 BYTE_MASK = 0xff;

//! xorshift64 (13, 7, 17), as in the demo
U64 xorshift(U64& s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

}  // namespace

ReplicaHost ::ReplicaHost(const char* const compName) : ReplicaHostComponentBase(compName), m_block(), m_enabled(true) {}

ReplicaHost ::~ReplicaHost() {}

U64 ReplicaHost ::compute(U64 (&block)[BLOCK_WORDS], U64 input, bool flip, U16 word, U8 bit) {
    U64 s = (input * FILL_MULTIPLIER) + 1;
    for (FwSizeType i = 0; i < BLOCK_WORDS; i++) {
        block[i] = xorshift(s);
    }
    if (flip) {
        block[word % BLOCK_WORDS] ^= (static_cast<U64>(1) << (bit % BITS_PER_WORD));
    }
    U64 h = FNV_OFFSET;
    for (FwSizeType i = 0; i < BLOCK_WORDS; i++) {
        for (U32 b = 0; b < BYTES_PER_WORD; b++) {
            h ^= (block[i] >> (BITS_PER_BYTE * b)) & BYTE_MASK;
            h *= FNV_PRIME;
        }
    }
    return h;
}

U64 ReplicaHost ::expectedKnownAnswer() {
    U64 block[BLOCK_WORDS] = {};
    return compute(block, KNOWN_ANSWER_INPUT, false, 0, 0);
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
