// ======================================================================
// \title  ParityTest.cpp
// \brief  MRU-SYS-001: the F´ components give the demo's counts on the
//         bench schedules, for the same seed.
//
// The four real components are wired as the test topology:
//   RedundancyManager -> FaultInjector -> ReplicaHost[3] -> FaultInjector
//   -> RedundancyManager, with HealthRegistry beside the manager.
// The harness drives one cycle at a time and dispatches every queue until
// all are empty before the next cycle: the deterministic version of a
// rate group whose period is longer than the work.
//
// The classes below are named after the components' testers. F´ makes a
// component's tester a friend of its generated base class, which lets this
// harness dispatch the queues and call the command handlers directly.
// ======================================================================

#include <cstdio>
#include <cstdlib>

#include "Fw/Cmd/CmdResponsePortAc.hpp"
#include "Fw/Test/UnitTest.hpp"
#include "Fw/Tlm/TlmPortAc.hpp"
#include "Mru/Components/FaultInjector/FaultInjector.hpp"
#include "Mru/Components/HealthRegistry/HealthRegistry.hpp"
#include "Mru/Components/RedundancyManager/RedundancyManager.hpp"
#include "Mru/Components/ReplicaHost/ReplicaHost.hpp"
#include "Mru/Ports/ResultPortAc.hpp"
#include "Mru/Test/Parity/ParityCases.hpp"
#include "gtest/gtest.h"

namespace Mru {

//! Counts of one run
struct Counts {
    U64 useful;
    U64 caught;
    U64 wrong;
};

namespace {

constexpr FwSizeType MANAGER_QUEUE = 32;
constexpr FwSizeType HOST_QUEUE = 16;
constexpr F64 COMMON_MODE = 0.05;  // the demo's default share

//! The run in progress, for the port callbacks
struct Run {
    U64 ticks;
    U64 useful;
    U64 wrong;
    U64 caught;
    U64 block[ReplicaHost::BLOCK_WORDS];
};
Run g_run;

void onResult(Fw::PassiveComponentBase* comp, FwIndexType port, U64 cycle, U64 value) {
    (void)comp;
    (void)port;
    if (cycle > g_run.ticks) {
        return;
    }
    if (value == ReplicaHost::compute(g_run.block, cycle, false, 0, 0)) {
        g_run.useful++;
    } else {
        g_run.wrong++;
    }
}

void onCmdResponse(Fw::PassiveComponentBase* comp,
                   FwIndexType port,
                   FwOpcodeType opCode,
                   U32 cmdSeq,
                   const Fw::CmdResponse& response) {
    (void)comp;
    (void)port;
    (void)opCode;
    (void)cmdSeq;
    EXPECT_EQ(response, Fw::CmdResponse::OK);
}

}  // namespace

//! Dispatches a replica's queue (friend of ReplicaHostComponentBase)
class ReplicaHostTester {
  public:
    static bool dispatch(ReplicaHostComponentBase& host) {
        if (host.m_queue.getMessagesAvailable() == 0) {
            return false;
        }
        (void)host.dispatchAvailableMessages();
        return true;
    }
};

//! Calls the injector's commands (friend of FaultInjectorComponentBase)
class FaultInjectorTester {
  public:
    static void inject(FaultInjectorComponentBase& fi, const ParityFault& f) {
        fi.INJECT_cmdHandler(0, 0, FaultKind(f.kind), f.replica, f.cycle);
    }
    static void upsets(FaultInjectorComponentBase& fi, F64 rate, U64 seed) {
        fi.SET_UPSETS_cmdHandler(0, 0, rate, COMMON_MODE, seed);
    }
};

//! The harness (friend of RedundancyManagerComponentBase)
class RedundancyManagerTester {
  public:
    //! The manager's Caught telemetry: disagreements caught so far
    static void onTlm(Fw::PassiveComponentBase* comp,
                      FwIndexType port,
                      FwChanIdType id,
                      Fw::Time& time,
                      Fw::TlmBuffer& val) {
        (void)comp;
        (void)port;
        (void)time;
        if (id == RedundancyManagerComponentBase::CHANNELID_CAUGHT) {
            U64 caught = 0;
            if (val.deserializeTo(caught) == Fw::FW_SERIALIZE_OK) {
                g_run.caught = caught;
            }
        }
    }

    static Counts run(const ParityCase& c) {
        g_run = Run();
        g_run.ticks = c.ticks;

        RedundancyManager manager("manager");
        HealthRegistry health("health");
        FaultInjector injector("injector");
        ReplicaHost host0("host0");
        ReplicaHost host1("host1");
        ReplicaHost host2("host2");
        ReplicaHost* hosts[Replicas] = {&host0, &host1, &host2};

        manager.init(MANAGER_QUEUE, 0);
        health.init(0);
        injector.init(0);
        for (FwIndexType i = 0; i < Replicas; i++) {
            hosts[i]->init(HOST_QUEUE, i);
        }

        InputResultPort results;
        results.init();
        results.addCallComp(&health, onResult);
        Fw::InputTlmPort tlm;
        tlm.init();
        tlm.addCallComp(&health, onTlm);
        Fw::InputCmdResponsePort responses;
        responses.init();
        responses.addCallComp(&health, onCmdResponse);

        for (FwIndexType i = 0; i < Replicas; i++) {
            manager.set_requestOut_OutputPort(i, injector.get_requestIn_InputPort(i));
            injector.set_requestOut_OutputPort(i, hosts[i]->get_requestIn_InputPort(0));
            hosts[i]->set_replyOut_OutputPort(0, injector.get_replyIn_InputPort(i));
            injector.set_replyOut_OutputPort(i, manager.get_replyIn_InputPort(i));
            manager.set_enableOut_OutputPort(i, hosts[i]->get_enableIn_InputPort(0));
        }
        manager.set_healthStrike_OutputPort(0, health.get_strikeIn_InputPort(0));
        manager.set_healthClear_OutputPort(0, health.get_clearIn_InputPort(0));
        manager.set_resultOut_OutputPort(0, &results);
        manager.set_tlmOut_OutputPort(0, &tlm);
        manager.set_cmdResponseOut_OutputPort(0, &responses);
        injector.set_cmdResponseOut_OutputPort(0, &responses);

        manager.configure(ReplicaHost::expectedKnownAnswer(), "");
        static_cast<RedundancyManagerComponentBase&>(manager).SET_POLICY_cmdHandler(0, 0, Policy(c.policy));
        FaultInjectorTester::upsets(injector, c.rate, c.seed);
        for (U32 f = 0; f < c.nFaults; f++) {
            FaultInjectorTester::inject(injector, c.faults[f]);
        }

        // cycles 1 to ticks, then one more to close the last cycle
        for (U64 cycle = 1; cycle <= c.ticks + 1; cycle++) {
            manager.get_schedIn_InputPort(0)->invoke(0);
            if (cycle == c.ticks + 1) {
                (void)static_cast<RedundancyManagerComponentBase&>(manager).doDispatch();  // closes cycle `ticks`
                break;
            }
            bool moved = true;
            while (moved) {
                moved = false;
                if (manager.m_queue.getMessagesAvailable() > 0) {
                    (void)manager.dispatchAvailableMessages();
                    moved = true;
                }
                for (FwIndexType i = 0; i < Replicas; i++) {
                    moved = ReplicaHostTester::dispatch(*hosts[i]) || moved;
                }
            }
        }

        manager.deinit();
        for (FwIndexType i = 0; i < Replicas; i++) {
            hosts[i]->deinit();
        }
        return Counts{g_run.useful, g_run.caught, g_run.wrong};
    }
};

}  // namespace Mru

TEST(System, ParityWithTheDemo) {
    COMMENT("The F´ components give the demo's counts on the bench schedules, same seed");
    REQUIREMENT("MRU-SYS-001");
    U32 exact = 0;
    for (const Mru::ParityCase& c : Mru::PARITY_CASES) {
        const Mru::Counts got = Mru::RedundancyManagerTester::run(c);
        const I64 slack = 3 * static_cast<I64>(c.kills);
        const I64 du = static_cast<I64>(got.useful) - static_cast<I64>(c.useful);
        const I64 dc = static_cast<I64>(got.caught) - static_cast<I64>(c.caught);
        const I64 dw = static_cast<I64>(got.wrong) - static_cast<I64>(c.wrong);
        std::printf("%-16s %-6s seed %llu rate %-6g  demo %llu/%llu/%llu  F' %llu/%llu/%llu\n", c.schedule,
                    c.policy == Mru::Policy::SHRINK ? "shrink" : "tmr", static_cast<unsigned long long>(c.seed), c.rate,
                    static_cast<unsigned long long>(c.useful), static_cast<unsigned long long>(c.caught),
                    static_cast<unsigned long long>(c.wrong), static_cast<unsigned long long>(got.useful),
                    static_cast<unsigned long long>(got.caught), static_cast<unsigned long long>(got.wrong));
        EXPECT_LE(std::llabs(du), slack) << c.schedule;
        EXPECT_LE(std::llabs(dc), slack) << c.schedule;
        // wrong results must match exactly: the slack is for the cycles a death is found in,
        // which F´ withholds rather than delivers
        EXPECT_EQ(dw, 0) << c.schedule;
        exact += (du == 0 && dc == 0 && dw == 0) ? 1 : 0;
    }
    std::printf("%u of %zu cases identical to the demo\n", exact, sizeof(Mru::PARITY_CASES) / sizeof(Mru::PARITY_CASES[0]));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
