// ======================================================================
// \title  Main.cpp
// \brief  The MruOpsSat deployment: one run of the four Mru components in
//         an F´ topology, stopped by a time cap or a signal, with a summary
//
// Usage: MruOpsSat [-s sequence.bin] [-t seconds] [-o summary.txt]
//                  [-f state-file] [-p cycle-ms] [-m memory-MB]
//
// The run: set up the topology, run the command sequence (the policy and the
// faults, as an operator would command them) and wait for it to finish, then
// drive the rate groups from the Linux timer until the time cap or SIGTERM.
// Signals are blocked in every thread and taken by one thread with sigwait,
// so stopping the timer never runs inside a signal handler.
// ======================================================================
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Fw/Logger/Logger.hpp"
#include "MruOpsSat/Top/MruOpsSatTopology.hpp"
#include "Os/Os.hpp"
#include "Os/Task.hpp"

namespace {

//! Why the run stopped, set by the signal thread
const char* volatile g_stopped = "time-limit";

sigset_t g_signals;

//! Takes SIGTERM, SIGINT, SIGHUP and the time cap's SIGALRM, stops the timer
void* signalThread(void*) {
    int sig = 0;
    while (sigwait(&g_signals, &sig) == 0) {
        g_stopped = (sig == SIGALRM) ? "time-limit" : "signal";
        MruOpsSat::stopRateGroups();
    }
    return nullptr;
}

double seconds(const struct timespec& t) {
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) / 1e9;
}

const char* modeName(const Mru::Mode& m) {
    switch (m.e) {
        case Mru::Mode::VOTE:
            return "vote";
        case Mru::Mode::COMPARE:
            return "compare";
        case Mru::Mode::SELF_CHECK:
            return "self-check";
        case Mru::Mode::HALTED:
            return "halted";
        default:
            return "unknown";
    }
}

const char* slotName(const Mru::SlotState& s) {
    switch (s.e) {
        case Mru::SlotState::ACTIVE:
            return "active";
        case Mru::SlotState::STANDBY:
            return "standby";
        case Mru::SlotState::RETIRED:
            return "retired";
        default:
            return "unknown";
    }
}

//! The process's address space in KiB, from /proc/self/maps: what RLIMIT_AS
//! counts. (VmSize in /proc/self/status is the emulator's under qemu-user;
//! the maps file is the program's own view on hardware and in emulation.)
unsigned long addressSpaceKiB() {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (maps == nullptr) {
        return 0;
    }
    unsigned long total = 0;
    unsigned long lo = 0;
    unsigned long hi = 0;
    char line[512];
    while (fgets(line, sizeof(line), maps) != nullptr) {
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 && hi > lo) {
            total += (hi - lo) / 1024;
        }
    }
    fclose(maps);
    return total;
}

void usage(const char* app) {
    Fw::Logger::log(
        "Usage: %s [-s sequence.bin] [-t seconds] [-o summary.txt] [-f state-file] [-p cycle-ms] [-m memory-MB]\n", app);
}

}  // namespace

int main(int argc, char* argv[]) {
    struct timespec start;
    (void)clock_gettime(CLOCK_MONOTONIC, &start);

    const char* sequence = nullptr;
    const char* summary = nullptr;
    const char* statePath = "";
    unsigned maxSeconds = 0;
    unsigned cycleMs = 100;
    unsigned memoryMb = 128;
    int option = 0;
    while ((option = getopt(argc, argv, "hs:t:o:f:p:m:")) != -1) {
        switch (option) {
            case 's':
                sequence = optarg;
                break;
            case 't':
                maxSeconds = static_cast<unsigned>(atoi(optarg));
                break;
            case 'o':
                summary = optarg;
                break;
            case 'f':
                statePath = optarg;
                break;
            case 'p':
                cycleMs = static_cast<unsigned>(atoi(optarg));
                break;
            case 'm':
                memoryMb = static_cast<unsigned>(atoi(optarg));
                break;
            case 'h':
            default:
                usage(argv[0]);
                return (option == 'h') ? 0 : 1;
        }
    }
    if (cycleMs == 0 || cycleMs >= 1000) {
        usage(argv[0]);
        return 1;
    }

    // the same limit as the demo: address space per process
    if (memoryMb > 0) {
        struct rlimit limit;
        limit.rlim_cur = static_cast<rlim_t>(memoryMb) * 1024 * 1024;
        limit.rlim_max = limit.rlim_cur;
        if (setrlimit(RLIMIT_AS, &limit) != 0) {
            Fw::Logger::log("Could not set the memory limit\n");
            return 1;
        }
    }

    // block the signals here, before any task starts, so every thread
    // inherits the mask; the signal thread takes them
    sigemptyset(&g_signals);
    sigaddset(&g_signals, SIGTERM);
    sigaddset(&g_signals, SIGINT);
    sigaddset(&g_signals, SIGHUP);
    sigaddset(&g_signals, SIGALRM);
    (void)pthread_sigmask(SIG_BLOCK, &g_signals, nullptr);

    Os::init();
    MruOpsSat::TopologyState state;
    state.statePath = statePath;
    state.cycleMs = cycleMs;
    MruOpsSat::setupTopology(state);

    pthread_t sigThread;
    if (pthread_create(&sigThread, nullptr, signalThread, nullptr) != 0) {
        Fw::Logger::log("Could not start the signal thread\n");
        return 1;
    }

    // the sequence first: the policy and the faults are set before cycle 1
    const char* seqStatus = "none";
    if (sequence != nullptr) {
        MruOpsSat::runSequence(sequence);
        seqStatus = "timeout";
        for (U32 waited = 0; waited < 10000; waited += 10) {
            const Mru::RunRecorder::Totals t = MruOpsSat::totals();
            if (t.seqDone) {
                seqStatus = t.seqOk ? "ok" : "failed";
                break;
            }
            Os::Task::delay(Fw::TimeInterval(0, 10000));
        }
    }

    if (maxSeconds > 0) {
        // the cap counts from the start of the process, start-up included
        struct timespec now;
        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        const double left = static_cast<double>(maxSeconds) - (seconds(now) - seconds(start));
        struct itimerval cap;
        std::memset(&cap, 0, sizeof(cap));
        const double l = (left > 0.001) ? left : 0.001;
        cap.it_value.tv_sec = static_cast<time_t>(l);
        cap.it_value.tv_usec = static_cast<suseconds_t>((l - static_cast<double>(cap.it_value.tv_sec)) * 1e6);
        (void)setitimer(ITIMER_REAL, &cap, nullptr);
    }
    if (std::strcmp(seqStatus, "none") == 0 || std::strcmp(seqStatus, "ok") == 0) {
        MruOpsSat::startRateGroups(state);
    } else {
        g_stopped = "sequence";
    }

    // let the last cycle's replies and telemetry reach their queues' end
    Os::Task::delay(Fw::TimeInterval(0, 200000));
    const Mru::RunRecorder::Totals t = MruOpsSat::totals();
    // every thread is still running here: the largest the address space gets
    const unsigned long vmKiB = addressSpaceKiB();
    MruOpsSat::teardownTopology(state);

    struct timespec end;
    (void)clock_gettime(CLOCK_MONOTONIC, &end);
    struct rusage usage;
    (void)getrusage(RUSAGE_SELF, &usage);
    const double cpu = static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
                       static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
    const double wall = seconds(end) - seconds(start);

    char text[1024];
    const int n = snprintf(
        text, sizeof(text),
        "policy=%s cycles=%llu cycle_ms=%u sequence=%s\n"
        "useful=%llu detected=%llu wrong=%llu last_useful=%llu live_at_end=%u mode=%s "
        "slots=%s,%s,%s stopped=%s\n"
        "footprint: max_rss=%ldKiB address_space=%luKiB cpu=%.3fs wall=%.3fs\n"
        "limits: memory=%uMB/process max_run=%us\n",
        t.policy.e == Mru::Policy::TMR ? "tmr" : "shrink", static_cast<unsigned long long>(t.cycles), cycleMs,
        seqStatus, static_cast<unsigned long long>(t.correct), static_cast<unsigned long long>(t.caught),
        static_cast<unsigned long long>(t.wrong), static_cast<unsigned long long>(t.lastCorrect), static_cast<unsigned>(t.live), modeName(t.mode),
        slotName(t.slots[0]), slotName(t.slots[1]), slotName(t.slots[2]), g_stopped, usage.ru_maxrss, vmKiB, cpu, wall,
        memoryMb, maxSeconds);
    if (n > 0) {
        fputs(text, stdout);
        if (summary != nullptr) {
            FILE* f = fopen(summary, "w");
            if (f != nullptr) {
                fputs(text, f);
                fclose(f);
            }
        }
    }
    fflush(stdout);
    // the signal thread is still in sigwait; leave without joining it
    return (std::strcmp(seqStatus, "failed") == 0 || std::strcmp(seqStatus, "timeout") == 0) ? 1 : 0;
}
