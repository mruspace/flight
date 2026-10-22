// ======================================================================
// \title  Payload.cpp
// \brief  The replicas' task: the demo's hash over a 4 KiB block (D6)
// ======================================================================

#include "Mru/Payload/Payload.hpp"

namespace Mru {
namespace Payload {

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

U64 compute(U64 (&block)[BLOCK_WORDS], U64 input, bool flip, U16 word, U8 bit) {
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

U64 expectedKnownAnswer() {
    U64 block[BLOCK_WORDS] = {};
    return compute(block, KNOWN_ANSWER_INPUT, false, 0, 0);
}

}  // namespace Payload
}  // namespace Mru
