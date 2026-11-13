/*
 * mru_payload.h: the replicas' task, the same as the F´ deployment's
 * (fprime/Mru/Payload) and the quorum demo's: the demo's hash over a 4 KiB
 * block, with an optional bit flip in the block, as radiation would make.
 */
#ifndef MRU_PAYLOAD_H
#define MRU_PAYLOAD_H

#include "common_types.h"

#define MRU_BLOCK_WORDS 512 /* 4 KiB, as in the demo */
#define MRU_KNOWN_ANSWER_INPUT 0

uint64 MRU_Compute(uint64 *block, uint64 input, bool flip, uint16 word, uint8 bit);
uint64 MRU_ExpectedKnownAnswer(void);

#endif
