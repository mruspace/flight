/*
 * quorum.h: C interface to the quorum decision core of Mru Flight.
 *
 * Every decision comes from the Rust crate `quorum` (no_std, checked with
 * Kani). This interface converts plain values and calls it; Kani proofs show
 * each function gives the core's answer (quorum-ffi/src/lib.rs).
 *
 * Out-of-range input gets the safe answer: a policy that is not 0 or 1
 * halts, a reply kind that is not 0 to 2 counts as absent, a replica index
 * above 2 changes nothing and returns false, and a null pointer does the
 * same.
 *
 * Build the static library with:
 *   cargo rustc -p quorum-ffi --release --features c-staticlib \
 *     --crate-type staticlib -- -C panic=abort
 */
#ifndef QUORUM_H
#define QUORUM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QUORUM_REPLICAS 3
#define QUORUM_STRIKES 3

/* policy */
#define QUORUM_SHRINK 0
#define QUORUM_TMR 1

/* mode */
#define QUORUM_VOTE 0
#define QUORUM_COMPARE 1
#define QUORUM_SELF_CHECK 2
#define QUORUM_HALTED 3

/* reply kind */
#define QUORUM_ABSENT 0
#define QUORUM_SINGLE 1
#define QUORUM_PAIR 2

/* outcome */
#define QUORUM_DELIVER 0
#define QUORUM_DETECTED 1
#define QUORUM_OUTCOME_HALTED 2

/* What one replica returned. */
typedef struct {
    uint8_t kind;    /* ABSENT, SINGLE or PAIR */
    uint64_t first;  /* the result */
    uint64_t second; /* the second result, for PAIR (self-check) */
} quorum_reply;

/* The decision for one cycle. */
typedef struct {
    uint8_t mode;
    uint8_t outcome;
    uint64_t value;                /* meaningful only when outcome is DELIVER */
    bool dissent[QUORUM_REPLICAS]; /* outvoted replicas (vote mode) */
    bool diagnose;                 /* two replicas disagree: test both (shrinking quorum) */
} quorum_verdict;

/* The strike record of quorum::Health, owned by the caller. */
typedef struct {
    uint64_t strikes[QUORUM_REPLICAS][QUORUM_STRIKES];
    uint64_t count[QUORUM_REPLICAS];
} quorum_health;

/* The mode a policy runs in with `alive` live replicas. */
uint8_t quorum_mode(uint8_t policy, uint32_t alive);

/* Decide what to deliver from QUORUM_REPLICAS replies. */
quorum_verdict quorum_decide(uint8_t policy, const quorum_reply *replies);

/* Set a record to no strikes. */
void quorum_health_init(quorum_health *health);

/* Record that `replica` was outvoted at `cycle`; true when it must be diagnosed. */
bool quorum_health_strike(quorum_health *health, uint32_t replica, uint64_t cycle);

/* Forget `replica`'s strikes. */
void quorum_health_clear(quorum_health *health, uint32_t replica);

#ifdef __cplusplus
}
#endif

#endif /* QUORUM_H */
