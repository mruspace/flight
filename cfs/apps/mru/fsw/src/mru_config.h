/*
 * mru_config.h: the run's settings and fault schedule, read from a text file
 * (/cf/mru.cfg) at start, so a run is set like the F´ deployment's command
 * sequence: from a published schedule, not from code.
 *
 *   cycles 400            cycles to run, then end the run
 *   cycle_ms 100          cycle period
 *   corrupt 2 60          one bit flip in replica 2's computation at cycle 60
 *   stuck 1 120           replica 1 returns a fixed wrong value from cycle 120
 *   hang 0 240            replica 0 gets no requests from cycle 240 (silent)
 *
 * Lines starting with '#' are comments.
 */
#ifndef MRU_CONFIG_H
#define MRU_CONFIG_H

#include "common_types.h"

#define MRU_MAX_FAULTS 8

typedef enum
{
    MRU_FAULT_CORRUPT = 0,
    MRU_FAULT_STUCK   = 1,
    MRU_FAULT_HANG    = 2
} MRU_FaultKind_t;

typedef struct
{
    MRU_FaultKind_t kind;
    uint8           replica;
    uint64          cycle;
} MRU_Fault_t;

typedef struct
{
    uint32      cycles;
    uint32      cycleMs;
    uint32      nFaults;
    MRU_Fault_t faults[MRU_MAX_FAULTS];
} MRU_Config_t;

/* Defaults, then the file if it exists. Returns the number of lines that
   could not be read (0 when all were fine). */
uint32 MRU_ReadConfig(const char *path, MRU_Config_t *cfg);

#endif
