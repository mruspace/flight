// ======================================================================
// \title  Main.cpp
// \brief  One replica of Mru Flight as its own F´ process
//
// Usage: MruReplica -f fd [-r replica] [-c core] [-m memory-MB]
//
// The manager process (MruQuorum) starts each replica with one end of a
// SOCK_SEQPACKET socket pair as descriptor `fd`. The replica pins itself to
// `core` (a processor of its own where there is one), answers the manager's
// requests through F´'s GenericHub, and exits when the link closes: when the
// manager stops, or is killed. A replica killed by a fault simply ends.
// ======================================================================
#include <getopt.h>
#include <sched.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

#include "MruReplica/Top/MruReplicaTopology.hpp"
#include "Os/Os.hpp"
#include "Os/Task.hpp"

namespace {

volatile sig_atomic_t g_stop = 0;

void onSignal(int) {
    g_stop = 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    int fd = -1;
    int replica = -1;
    int core = -1;
    unsigned memoryMb = 128;
    int option = 0;
    while ((option = getopt(argc, argv, "f:r:c:m:")) != -1) {
        switch (option) {
            case 'f':
                fd = atoi(optarg);
                break;
            case 'r':
                replica = atoi(optarg);
                break;
            case 'c':
                core = atoi(optarg);
                break;
            case 'm':
                memoryMb = static_cast<unsigned>(atoi(optarg));
                break;
            default:
                std::fprintf(stderr, "Usage: %s -f fd [-r replica] [-c core] [-m memory-MB]\n", argv[0]);
                return 1;
        }
    }
    if (fd < 0) {
        std::fprintf(stderr, "MruReplica: no socket descriptor (-f)\n");
        return 1;
    }
    if (memoryMb > 0) {
        struct rlimit limit;
        limit.rlim_cur = static_cast<rlim_t>(memoryMb) * 1024 * 1024;
        limit.rlim_max = limit.rlim_cur;
        (void)setrlimit(RLIMIT_AS, &limit);
    }
    // pin before any thread starts: every thread of the process inherits it
    const char* pinned = "not pinned";
    if (core >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(core, &set);
        pinned = (sched_setaffinity(0, sizeof(set), &set) == 0) ? "pinned" : "pin refused";
    }
    std::printf("replica %d: pid %d, %s, on core %d\n", replica, static_cast<int>(getpid()), pinned, sched_getcpu());
    std::fflush(stdout);

    struct sigaction action;
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    (void)sigaction(SIGTERM, &action, nullptr);
    (void)sigaction(SIGINT, &action, nullptr);

    Os::init();
    MruReplica::TopologyState state;
    state.fd = fd;
    MruReplica::setupTopology(state);

    // run until the link has opened and closed again, or a stop signal
    bool opened = false;
    for (U32 waited = 0; g_stop == 0; waited += 50) {
        const bool open = MruReplica::linkOpen();
        opened = opened || open;
        if ((opened && !open) || (!opened && waited > 10000)) {
            break;
        }
        Os::Task::delay(Fw::TimeInterval(0, 50000));
    }
    MruReplica::teardownTopology(state);
    return 0;
}
