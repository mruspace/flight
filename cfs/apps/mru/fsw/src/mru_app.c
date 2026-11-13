/*
 * mru_app.c: Mru's shrinking quorum as a cFS application.
 *
 * Three replicas run as cFE child tasks. Each cycle the app sends each live
 * replica a request through an OSAL queue, collects the answers, and asks
 * Mru's decision core (the Rust crate `quorum`, through quorum.h) what to
 * deliver: vote on three, compare on two, self-check on one. The core
 * decides; this file moves data, as the F´ components do (MRU-RM-016).
 *
 * The rules around the core are those of the F´ RedundancyManager: three
 * outvotes within the strike window ask for a known-answer test, a wrong or
 * missing known answer retires the replica, three silent cycles in a row
 * lose it, the last replica self-checks every other cycle with a periodic
 * known-answer test, and a cycle in which every expected answer is missing
 * is withheld with no replica blamed. Faults come from a schedule in
 * /cf/mru.cfg (mru_config.h) and are applied here, between the app and the
 * replicas, as F´'s FaultInjector does. Each delivered result is checked
 * against the payload computed without a fault.
 *
 * After the configured number of cycles the app prints a summary and ends
 * cFE with a power-on reset, so the PSP deletes its shared memory.
 */
#include <string.h>

#include "cfe.h"
#include "cfe_psp.h"
#include "quorum.h"

#include "mru_config.h"
#include "mru_payload.h"

#define MRU_REPLICAS       3
#define MRU_MISSES_TO_LOSE 3
#define MRU_KAT_PERIOD     64
#define MRU_STUCK_VALUE    0xDEADBEEFDEADBEEFULL
#define MRU_FLIP_WORD      7
#define MRU_FLIP_BIT       3
#define MRU_CAUGHT_EVENTS  10 /* caught disagreements reported as events; all are counted */
#define MRU_CONFIG_PATH    "/cf/mru.cfg"

/* what a request asks for */
#define MRU_COMPUTE       0
#define MRU_COMPUTE_TWICE 1
#define MRU_KNOWN_ANSWER  2

enum
{
    MRU_INIT_EID = 1,
    MRU_CONFIG_EID,
    MRU_FAULT_EID,
    MRU_MODE_EID,
    MRU_RETIRED_EID,
    MRU_CAUGHT_EID,
    MRU_DIAG_EID,
    MRU_CLEARED_EID,
    MRU_WRONG_EID,
    MRU_HALTED_EID,
    MRU_WITHHELD_EID,
    MRU_SUMMARY_EID,
    MRU_ERROR_EID
};

typedef struct
{
    uint64 cycle;
    uint16 word;
    uint8  kind;
    uint8  flip;
    uint8  bit;
} MRU_Request_t;

typedef struct
{
    uint64 cycle;
    uint64 first;
    uint64 second;
    uint8  replica;
    uint8  kind;
} MRU_Reply_t;

/* one cycle's answers */
typedef struct
{
    bool   got[MRU_REPLICAS];
    uint64 first[MRU_REPLICAS];
    uint64 second[MRU_REPLICAS];
    bool   katGot[MRU_REPLICAS];
    uint64 kat[MRU_REPLICAS];
} MRU_Answers_t;

static struct
{
    osal_id_t     reqQ[MRU_REPLICAS];
    osal_id_t     replyQ;
    MRU_Config_t  cfg;
    bool          announced[MRU_MAX_FAULTS];
    bool          active[MRU_REPLICAS];
    uint8         missed[MRU_REPLICAS];
    quorum_health health;
    uint8         lastMode;
    bool          halted;
    uint64        useful;
    uint64        detected;
    uint64        wrong;
    uint64        withheld;
    uint64        lastUseful;
    uint64        knownAnswer;
    uint64        truthBlock[MRU_BLOCK_WORDS];
} MRU;

static uint64 MRU_ReplicaBlock[MRU_REPLICAS][MRU_BLOCK_WORDS];

static const char *const MRU_ModeName[] = {"vote", "compare", "self-check", "halted"};

/* ---------------------------------------------------------------------- */
/* Replicas: child tasks that compute what they are asked                  */
/* ---------------------------------------------------------------------- */

static void MRU_ReplicaLoop(uint8 i)
{
    MRU_Request_t rq;
    MRU_Reply_t   rp;
    size_t        copied = 0;

    for (;;)
    {
        if (OS_QueueGet(MRU.reqQ[i], &rq, sizeof(rq), &copied, OS_PEND) != OS_SUCCESS || copied != sizeof(rq))
        {
            OS_TaskDelay(10);
            continue;
        }
        memset(&rp, 0, sizeof(rp));
        rp.cycle   = rq.cycle;
        rp.replica = i;
        rp.kind    = rq.kind;
        if (rq.kind == MRU_KNOWN_ANSWER)
        {
            rp.first = MRU_Compute(MRU_ReplicaBlock[i], MRU_KNOWN_ANSWER_INPUT, false, 0, 0);
        }
        else
        {
            rp.first = MRU_Compute(MRU_ReplicaBlock[i], rq.cycle, rq.flip != 0, rq.word, rq.bit);
            if (rq.kind == MRU_COMPUTE_TWICE)
            {
                rp.second = MRU_Compute(MRU_ReplicaBlock[i], rq.cycle, false, 0, 0);
            }
        }
        (void)OS_QueuePut(MRU.replyQ, &rp, sizeof(rp), 0);
    }
}

static void MRU_Replica0(void)
{
    MRU_ReplicaLoop(0);
}
static void MRU_Replica1(void)
{
    MRU_ReplicaLoop(1);
}
static void MRU_Replica2(void)
{
    MRU_ReplicaLoop(2);
}

/* ---------------------------------------------------------------------- */
/* Faults, applied between the app and the replicas                        */
/* ---------------------------------------------------------------------- */

static bool MRU_Fault(MRU_FaultKind_t kind, uint8 replica, uint64 cycle)
{
    uint32 n;
    bool   found = false;

    for (n = 0; n < MRU.cfg.nFaults; n++)
    {
        const MRU_Fault_t *f = &MRU.cfg.faults[n];
        bool on = (f->kind == kind) && (f->replica == replica) &&
                  ((kind == MRU_FAULT_CORRUPT) ? (f->cycle == cycle) : (f->cycle <= cycle));
        if (on && !MRU.announced[n])
        {
            static const char *const names[] = {"corrupt", "stuck", "hang"};
            MRU.announced[n] = true;
            CFE_EVS_SendEvent(MRU_FAULT_EID, CFE_EVS_EventType_INFORMATION, "%s applied to replica %u at cycle %llu",
                              names[kind], (unsigned)replica, (unsigned long long)cycle);
        }
        found = found || on;
    }
    return found;
}

static void MRU_Send(uint8 i, uint64 cycle, uint8 kind)
{
    MRU_Request_t rq;

    if (MRU_Fault(MRU_FAULT_HANG, i, cycle))
    {
        return; /* a hung replica gets nothing and so answers nothing */
    }
    memset(&rq, 0, sizeof(rq));
    rq.cycle = cycle;
    rq.kind  = kind;
    if (kind != MRU_KNOWN_ANSWER && MRU_Fault(MRU_FAULT_CORRUPT, i, cycle))
    {
        rq.flip = 1;
        rq.word = MRU_FLIP_WORD;
        rq.bit  = MRU_FLIP_BIT;
    }
    (void)OS_QueuePut(MRU.reqQ[i], &rq, sizeof(rq), 0);
}

/* ---------------------------------------------------------------------- */
/* Time                                                                    */
/* ---------------------------------------------------------------------- */

static int64 MRU_NowMs(void)
{
    OS_time_t t;
    OS_GetLocalTime(&t);
    return OS_TimeGetTotalMilliseconds(t);
}

/* Collect answers for `cycle` until every one in `need` and `needKat` is in,
   or `deadline` (ms) passes. A stuck replica's answers are replaced here. */
static void MRU_Collect(uint64 cycle, const bool *need, const bool *needKat, MRU_Answers_t *a, int64 deadline)
{
    MRU_Reply_t rp;
    size_t      copied = 0;

    for (;;)
    {
        bool  done = true;
        uint8 i;
        int64 left;

        for (i = 0; i < MRU_REPLICAS; i++)
        {
            done = done && (!need[i] || a->got[i]) && (!needKat[i] || a->katGot[i]);
        }
        left = deadline - MRU_NowMs();
        if (done || left <= 0)
        {
            return;
        }
        if (OS_QueueGet(MRU.replyQ, &rp, sizeof(rp), &copied, (int32)left) != OS_SUCCESS || copied != sizeof(rp))
        {
            continue;
        }
        if (rp.cycle != cycle || rp.replica >= MRU_REPLICAS)
        {
            continue; /* late, from an earlier cycle */
        }
        if (MRU_Fault(MRU_FAULT_STUCK, rp.replica, cycle))
        {
            rp.first  = MRU_STUCK_VALUE;
            rp.second = MRU_STUCK_VALUE;
        }
        if (rp.kind == MRU_KNOWN_ANSWER)
        {
            a->katGot[rp.replica] = true;
            a->kat[rp.replica]    = rp.first;
        }
        else
        {
            a->got[rp.replica]    = true;
            a->first[rp.replica]  = rp.first;
            a->second[rp.replica] = rp.second;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Decisions                                                               */
/* ---------------------------------------------------------------------- */

static uint32 MRU_ActiveCount(void)
{
    uint32 n = 0;
    uint8  i;
    for (i = 0; i < MRU_REPLICAS; i++)
    {
        n += MRU.active[i] ? 1 : 0;
    }
    return n;
}

static void MRU_Retire(uint8 i, const char *reason, uint64 cycle)
{
    MRU.active[i] = false;
    CFE_EVS_SendEvent(MRU_RETIRED_EID, CFE_EVS_EventType_ERROR, "Replica %u retired (%s) at cycle %llu", (unsigned)i,
                      reason, (unsigned long long)cycle);
}

/* A known-answer test asked for a diagnosis: the right answer clears the
   replica's strikes, a wrong or missing one retires it */
static void MRU_Diagnose(uint8 i, uint64 cycle, int64 deadline)
{
    MRU_Answers_t a;
    bool          none[MRU_REPLICAS] = {false, false, false};
    bool          kat[MRU_REPLICAS]  = {false, false, false};

    memset(&a, 0, sizeof(a));
    kat[i] = true;
    CFE_EVS_SendEvent(MRU_DIAG_EID, CFE_EVS_EventType_INFORMATION, "Known-answer test requested from replica %u at cycle %llu",
                      (unsigned)i, (unsigned long long)cycle);
    MRU_Send(i, cycle, MRU_KNOWN_ANSWER);
    MRU_Collect(cycle, none, kat, &a, deadline);
    if (!a.katGot[i])
    {
        MRU_Retire(i, "KNOWN_ANSWER_MISSING", cycle);
    }
    else if (a.kat[i] != MRU.knownAnswer)
    {
        MRU_Retire(i, "KNOWN_ANSWER_FAILED", cycle);
    }
    else
    {
        quorum_health_clear(&MRU.health, i);
        CFE_EVS_SendEvent(MRU_CLEARED_EID, CFE_EVS_EventType_INFORMATION, "Replica %u passed its known-answer test at cycle %llu",
                          (unsigned)i, (unsigned long long)cycle);
    }
}

static void MRU_Cycle(uint64 c, int64 deadline)
{
    MRU_Answers_t   a;
    bool            expect[MRU_REPLICAS]  = {false, false, false};
    bool            periodic[MRU_REPLICAS] = {false, false, false};
    quorum_reply    qr[MRU_REPLICAS];
    quorum_verdict  v;
    uint8           mode;
    uint8           kind;
    uint8           outcome;
    uint8           i;
    uint32          expected = 0;
    uint32          got      = 0;

    memset(&a, 0, sizeof(a));
    mode = quorum_mode(QUORUM_SHRINK, MRU_ActiveCount());
    if (mode != MRU.lastMode)
    {
        CFE_EVS_SendEvent(MRU_MODE_EID, CFE_EVS_EventType_INFORMATION, "Mode %s -> %s at cycle %llu",
                          MRU_ModeName[MRU.lastMode], MRU_ModeName[mode], (unsigned long long)c);
        MRU.lastMode = mode;
    }
    if (mode == QUORUM_HALTED)
    {
        if (!MRU.halted)
        {
            MRU.halted = true;
            CFE_EVS_SendEvent(MRU_HALTED_EID, CFE_EVS_EventType_ERROR, "Halted at cycle %llu", (unsigned long long)c);
        }
        return;
    }
    /* one replica computes everything twice, so it delivers every other cycle */
    if (mode == QUORUM_SELF_CHECK && (c % 2) == 1)
    {
        return;
    }
    kind = (mode == QUORUM_SELF_CHECK) ? MRU_COMPUTE_TWICE : MRU_COMPUTE;
    for (i = 0; i < MRU_REPLICAS; i++)
    {
        if (MRU.active[i])
        {
            expect[i] = true;
            expected++;
            MRU_Send(i, c, kind);
            if (mode == QUORUM_SELF_CHECK && (c % MRU_KAT_PERIOD) == 0)
            {
                periodic[i] = true;
                MRU_Send(i, c, MRU_KNOWN_ANSWER);
            }
        }
    }
    MRU_Collect(c, expect, periodic, &a, deadline);

    for (i = 0; i < MRU_REPLICAS; i++)
    {
        got += (expect[i] && a.got[i]) ? 1 : 0;
    }
    /* every one of two or more answers missing: the shared processor, not
       each replica; withhold the cycle and blame no one (as MRU-RM-023) */
    if (expected >= 2 && got == 0)
    {
        MRU.withheld++;
        CFE_EVS_SendEvent(MRU_WITHHELD_EID, CFE_EVS_EventType_INFORMATION, "Cycle %llu withheld: every answer missing",
                          (unsigned long long)c);
        return;
    }

    for (i = 0; i < MRU_REPLICAS; i++)
    {
        memset(&qr[i], 0, sizeof(qr[i]));
        if (expect[i] && a.got[i])
        {
            qr[i].kind   = (kind == MRU_COMPUTE_TWICE) ? QUORUM_PAIR : QUORUM_SINGLE;
            qr[i].first  = a.first[i];
            qr[i].second = a.second[i];
        }
    }
    v       = quorum_decide(QUORUM_SHRINK, qr);
    outcome = v.outcome;

    /* three outvotes in the window: test the replica */
    for (i = 0; i < MRU_REPLICAS; i++)
    {
        if (v.dissent[i] && quorum_health_strike(&MRU.health, i, c))
        {
            MRU_Diagnose(i, c, deadline);
        }
    }
    /* two disagree and nothing says which: test both */
    if (v.diagnose)
    {
        for (i = 0; i < MRU_REPLICAS; i++)
        {
            if (MRU.active[i])
            {
                MRU_Diagnose(i, c, deadline);
            }
        }
    }
    /* the periodic test on the last replica: a failure withholds this cycle */
    for (i = 0; i < MRU_REPLICAS; i++)
    {
        if (periodic[i] && MRU.active[i] && (!a.katGot[i] || a.kat[i] != MRU.knownAnswer))
        {
            MRU_Retire(i, a.katGot[i] ? "PERIODIC_TEST_FAILED" : "PERIODIC_TEST_MISSING", c);
            outcome = QUORUM_DETECTED;
        }
    }

    if (outcome == QUORUM_DELIVER)
    {
        if (v.value == MRU_Compute(MRU.truthBlock, c, false, 0, 0))
        {
            MRU.useful++;
            MRU.lastUseful = c;
        }
        else
        {
            MRU.wrong++;
            CFE_EVS_SendEvent(MRU_WRONG_EID, CFE_EVS_EventType_ERROR, "Wrong result delivered at cycle %llu",
                              (unsigned long long)c);
        }
    }
    else if (outcome == QUORUM_DETECTED)
    {
        MRU.detected++;
        if (MRU.detected <= MRU_CAUGHT_EVENTS)
        {
            CFE_EVS_SendEvent(MRU_CAUGHT_EID, CFE_EVS_EventType_INFORMATION, "Disagreement caught at cycle %llu in %s",
                              (unsigned long long)c, MRU_ModeName[v.mode]);
        }
    }

    /* three silent cycles in a row: lost */
    for (i = 0; i < MRU_REPLICAS; i++)
    {
        if (!expect[i] || !MRU.active[i])
        {
            continue;
        }
        if (a.got[i])
        {
            MRU.missed[i] = 0;
        }
        else if (++MRU.missed[i] >= MRU_MISSES_TO_LOSE)
        {
            MRU_Retire(i, "LOST", c);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Start, run, end                                                         */
/* ---------------------------------------------------------------------- */

/* Before cycle 1: every replica gives its known answer, so the run starts
   with tasks that are up and right; one that fails starts retired */
static void MRU_StartupCheck(void)
{
    MRU_Answers_t a;
    bool          none[MRU_REPLICAS] = {false, false, false};
    bool          all[MRU_REPLICAS]  = {true, true, true};
    uint8         i;

    memset(&a, 0, sizeof(a));
    for (i = 0; i < MRU_REPLICAS; i++)
    {
        MRU_Send(i, 0, MRU_KNOWN_ANSWER);
    }
    MRU_Collect(0, none, all, &a, MRU_NowMs() + 2000);
    for (i = 0; i < MRU_REPLICAS; i++)
    {
        if (!a.katGot[i])
        {
            MRU_Retire(i, "KNOWN_ANSWER_MISSING", 0);
        }
        else if (a.kat[i] != MRU.knownAnswer)
        {
            MRU_Retire(i, "KNOWN_ANSWER_FAILED", 0);
        }
        else
        {
            CFE_EVS_SendEvent(MRU_CLEARED_EID, CFE_EVS_EventType_INFORMATION,
                              "Replica %u passed its start-up known-answer test", (unsigned)i);
        }
    }
}

static bool MRU_Init(void)
{
    static const char *const reqNames[] = {"MRU_REQ0", "MRU_REQ1", "MRU_REQ2"};
    static const char *const taskNames[] = {"MRU_REPLICA0", "MRU_REPLICA1", "MRU_REPLICA2"};
    static const CFE_ES_ChildTaskMainFuncPtr_t mains[] = {MRU_Replica0, MRU_Replica1, MRU_Replica2};
    CFE_ES_TaskId_t                            task;
    uint32                                     bad;
    uint8                                      i;

    memset(&MRU, 0, sizeof(MRU));
    if (CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY) != CFE_SUCCESS)
    {
        return false;
    }
    bad             = MRU_ReadConfig(MRU_CONFIG_PATH, &MRU.cfg);
    MRU.knownAnswer = MRU_ExpectedKnownAnswer();
    quorum_health_init(&MRU.health);
    MRU.lastMode = QUORUM_VOTE;
    CFE_EVS_SendEvent(MRU_CONFIG_EID, bad ? CFE_EVS_EventType_ERROR : CFE_EVS_EventType_INFORMATION,
                      "Config: %lu cycles of %lu ms, %lu faults, %lu bad lines", (unsigned long)MRU.cfg.cycles,
                      (unsigned long)MRU.cfg.cycleMs, (unsigned long)MRU.cfg.nFaults, (unsigned long)bad);
    if (OS_QueueCreate(&MRU.replyQ, "MRU_REPLY", 10, sizeof(MRU_Reply_t), 0) != OS_SUCCESS)
    {
        return false;
    }
    for (i = 0; i < MRU_REPLICAS; i++)
    {
        MRU.active[i] = true;
        if (OS_QueueCreate(&MRU.reqQ[i], reqNames[i], 4, sizeof(MRU_Request_t), 0) != OS_SUCCESS ||
            CFE_ES_CreateChildTask(&task, taskNames[i], mains[i], CFE_ES_TASK_STACK_ALLOCATE, 16384, 60, 0) !=
                CFE_SUCCESS)
        {
            return false;
        }
    }
    CFE_EVS_SendEvent(MRU_INIT_EID, CFE_EVS_EventType_INFORMATION,
                      "Mru started: shrinking quorum over three replica tasks, decided by the quorum core");
    return true;
}

void MRU_AppMain(void)
{
    uint32 run = CFE_ES_RunStatus_APP_RUN;
    uint64 c;
    int64  start;

    if (!MRU_Init())
    {
        CFE_EVS_SendEvent(MRU_ERROR_EID, CFE_EVS_EventType_ERROR, "Mru could not start");
        CFE_ES_ExitApp(CFE_ES_RunStatus_APP_ERROR);
        return;
    }
    /* every cFE application up, then every replica checked */
    (void)CFE_ES_WaitForStartupSync(10000);
    MRU_StartupCheck();
    start = MRU_NowMs();
    for (c = 1; c <= MRU.cfg.cycles && CFE_ES_RunLoop(&run); c++)
    {
        const int64 cycleEnd = start + (int64)(c * MRU.cfg.cycleMs);
        int64       now;

        /* answers are due by half the cycle; the rest is idle */
        MRU_Cycle(c, cycleEnd - (int64)(MRU.cfg.cycleMs / 2));
        now = MRU_NowMs();
        if (cycleEnd > now)
        {
            OS_TaskDelay((uint32)(cycleEnd - now));
        }
    }
    OS_printf("MRU SUMMARY: policy=shrink cycles=%llu cycle_ms=%lu useful=%llu detected=%llu wrong=%llu "
              "withheld=%llu last_useful=%llu live_at_end=%lu mode=%s slots=%s,%s,%s\n",
              (unsigned long long)(c - 1), (unsigned long)MRU.cfg.cycleMs, (unsigned long long)MRU.useful,
              (unsigned long long)MRU.detected, (unsigned long long)MRU.wrong, (unsigned long long)MRU.withheld,
              (unsigned long long)MRU.lastUseful, (unsigned long)MRU_ActiveCount(), MRU_ModeName[MRU.lastMode],
              MRU.active[0] ? "active" : "retired", MRU.active[1] ? "active" : "retired",
              MRU.active[2] ? "active" : "retired");
    CFE_EVS_SendEvent(MRU_SUMMARY_EID, CFE_EVS_EventType_INFORMATION, "Run ended after %llu cycles: power-on reset",
                      (unsigned long long)(c - 1));
    /* a power-on reset makes the PSP delete its shared memory before exit */
    CFE_ES_ResetCFE(CFE_PSP_RST_TYPE_POWERON);
    CFE_ES_ExitApp(run);
}
