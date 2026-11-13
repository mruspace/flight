/*
 * mru_payload.c: the replicas' task, as fprime/Mru/Payload/Payload.cpp.
 */
#include "mru_payload.h"

#define MRU_FILL_MULTIPLIER 0x9E3779B97F4A7C15ULL /* golden-ratio constant, as in the demo */
#define MRU_FNV_OFFSET 0xcbf29ce484222325ULL      /* FNV-1a 64-bit offset basis */
#define MRU_FNV_PRIME 0x00000100000001b3ULL       /* FNV-1a 64-bit prime */

static uint64 MRU_Xorshift(uint64 *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

uint64 MRU_Compute(uint64 *block, uint64 input, bool flip, uint16 word, uint8 bit)
{
    uint64 s = (input * MRU_FILL_MULTIPLIER) + 1;
    uint64 h = MRU_FNV_OFFSET;
    uint32 i;
    uint32 b;

    for (i = 0; i < MRU_BLOCK_WORDS; i++)
    {
        block[i] = MRU_Xorshift(&s);
    }
    if (flip)
    {
        block[word % MRU_BLOCK_WORDS] ^= ((uint64)1 << (bit % 64));
    }
    for (i = 0; i < MRU_BLOCK_WORDS; i++)
    {
        for (b = 0; b < 8; b++)
        {
            h ^= (block[i] >> (8 * b)) & 0xff;
            h *= MRU_FNV_PRIME;
        }
    }
    return h;
}

uint64 MRU_ExpectedKnownAnswer(void)
{
    static uint64 block[MRU_BLOCK_WORDS];
    return MRU_Compute(block, MRU_KNOWN_ANSWER_INPUT, false, 0, 0);
}
