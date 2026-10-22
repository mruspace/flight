// ======================================================================
// \title  Main.cpp
// \brief  The MruQuorum deployment: the manager process of one run, with
//         each replica in a process of its own (MruReplica), stopped by a
//         time cap or a signal, with a summary
//
// Usage: MruQuorum [-s sequence.bin] [-t seconds] [-o summary.txt]
//                  [-f state-file] [-p cycle-ms] [-m memory-MB]
//                  [-R replica-binary] [-C core,core,core]
//                  [-k kind:replica@cycle,...]
//
// Before F´ starts, the manager makes one SOCK_SEQPACKET socket pair per
// replica and starts the three replica processes, each pinned to its core
// (-C, default 0,1,1: replicas 0 and 1 on different cores). -k schedules
// real faults in the replica processes, sent at the start of a cycle before
// its requests: kill (SIGKILL, the process ends) and stop (SIGSTOP, it
// hangs). At the end the summary says how each replica process ended.
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
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Fw/Logger/Logger.hpp"
#include "MruQuorum/Top/MruQuorumTopology.hpp"
#include "Os/Os.hpp"
#include "Os/Task.hpp"

extern char** environ;

namespace {

constexpr int REPLICAS = 3;

//! The replica processes, started before F´
pid_t g_pids[REPLICAS] = {-1, -1, -1};

//! A real fault in a replica process at the start of a cycle
struct ProcessFault {
    U64 cycle;
    int replica;
    int signal;
    bool sent;
};
constexpr int MAX_FAULTS = 6;
ProcessFault g_faults[MAX_FAULTS];
int g_nFaults = 0;

//! Called by RunRecorder at the start of every cycle, before the manager
void onCycle(U64 cycle) {
    for (int i = 0; i < g_nFaults; i++) {
        ProcessFault& f = g_faults[i];
        if (!f.sent && f.cycle == cycle && g_pids[f.replica] > 0) {
            f.sent = true;
            const int r = kill(g_pids[f.replica], f.signal);
            std::printf("FAULT: replica %d (pid %d) sent %s at the start of cycle %llu: %s\n", f.replica,
                        static_cast<int>(g_pids[f.replica]), f.signal == SIGKILL ? "SIGKILL" : "SIGSTOP",
                        static_cast<unsigned long long>(cycle), r == 0 ? "ok" : "failed");
            std::fflush(stdout);
        }
    }
}

//! Parse "kill:0@240,stop:2@60"; false on a malformed entry
bool parseFaults(const char* text) {
    const char* p = text;
    while (*p != '\0') {
        char kind[8] = {};
        int replica = -1;
        unsigned long long cycle = 0;
        int used = 0;
        if (std::sscanf(p, "%7[a-z]:%d@%llu%n", kind, &replica, &cycle, &used) != 3 || replica < 0 ||
            replica >= REPLICAS || cycle == 0 || g_nFaults >= MAX_FAULTS) {
            return false;
        }
        int sig = 0;
        if (std::strcmp(kind, "kill") == 0) {
            sig = SIGKILL;
        } else if (std::strcmp(kind, "stop") == 0) {
            sig = SIGSTOP;
        } else {
            return false;
        }
        g_faults[g_nFaults++] = ProcessFault{cycle, replica, sig, false};
        p += used;
        if (*p == ',') {
            p++;
        }
    }
    return true;
}

//! Start replica `i` with `fd` as descriptor 100 + i, pinned to `core`
pid_t startReplica(const char* binary, int i, int fd, int core, unsigned memoryMb) {
    const int target = 100 + i;
    char fdArg[16];
    char replicaArg[16];
    char coreArg[16];
    char memArg[16];
    (void)std::snprintf(fdArg, sizeof(fdArg), "%d", target);
    (void)std::snprintf(replicaArg, sizeof(replicaArg), "%d", i);
    (void)std::snprintf(coreArg, sizeof(coreArg), "%d", core);
    (void)std::snprintf(memArg, sizeof(memArg), "%u", memoryMb);
    char* const args[] = {const_cast<char*>(binary), const_cast<char*>("-f"), fdArg, const_cast<char*>("-r"),
                          replicaArg, const_cast<char*>("-c"), coreArg, const_cast<char*>("-m"), memArg, nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // the descriptor survives the exec under its new number; every other
    // socket was made close-on-exec
    posix_spawn_file_actions_adddup2(&actions, fd, target);
    pid_t pid = -1;
    const int r = posix_spawn(&pid, binary, &actions, nullptr, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    return (r == 0) ? pid : -1;
}

//! Wait for the replica processes to end (they end when the links close),
//! then describe how each one ended
void reapReplicas(char* out, FwSizeType size) {
    int status[REPLICAS] = {0, 0, 0};
    bool done[REPLICAS] = {false, false, false};
    for (int waited = 0; waited < 3000; waited += 20) {
        bool all = true;
        for (int i = 0; i < REPLICAS; i++) {
            if (!done[i] && g_pids[i] > 0) {
                const pid_t r = waitpid(g_pids[i], &status[i], WNOHANG);
                done[i] = (r == g_pids[i]);
            }
            all = all && (done[i] || g_pids[i] <= 0);
        }
        if (all) {
            break;
        }
        // a stopped replica cannot see its link close: let it go
        for (int i = 0; i < REPLICAS; i++) {
            if (!done[i] && g_pids[i] > 0 && waited == 1000) {
                (void)kill(g_pids[i], SIGKILL);
            }
        }
        (void)usleep(20000);
    }
    FwSizeType used = 0;
    for (int i = 0; i < REPLICAS && used < size; i++) {
        char what[32] = "not started";
        if (g_pids[i] > 0 && !done[i]) {
            (void)std::snprintf(what, sizeof(what), "still running");
        } else if (g_pids[i] > 0 && WIFEXITED(status[i])) {
            (void)std::snprintf(what, sizeof(what), "exit %d", WEXITSTATUS(status[i]));
        } else if (g_pids[i] > 0 && WIFSIGNALED(status[i])) {
            (void)std::snprintf(what, sizeof(what), "signal %d", WTERMSIG(status[i]));
        }
        const int n = std::snprintf(out + used, size - used, "%s%d=%s", i == 0 ? "" : " ", i, what);
        used += (n > 0) ? static_cast<FwSizeType>(n) : 0;
    }
}

//! Why the run stopped, set by the signal thread
const char* volatile g_stopped = "time-limit";

sigset_t g_signals;

//! Takes SIGTERM, SIGINT, SIGHUP and the time cap's SIGALRM, stops the timer
void* signalThread(void*) {
    int sig = 0;
    while (sigwait(&g_signals, &sig) == 0) {
        g_stopped = (sig == SIGALRM) ? "time-limit" : "signal";
        MruQuorum::stopRateGroups();
    }
    return nullptr;
}

double seconds(const struct timespec& t) {
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) / 1e9;
}

double seconds(const struct timeval& t) {
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) / 1e6;
}

//! CPU time of the whole process (every thread) and the wall clock at one
//! moment. The summary splits the run at two of them, so a share above the
//! expected one can be placed: start-up, the timed run, or the end.
struct Sample {
    double user;    //!< CPU time in user space
    double system;  //!< CPU time in the kernel, on behalf of the process
    double wall;    //!< Monotonic clock
};

Sample sample() {
    struct rusage u;
    (void)getrusage(RUSAGE_SELF, &u);
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return Sample{seconds(u.ru_utime), seconds(u.ru_stime), seconds(now)};
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
        "Usage: %s [-s sequence.bin] [-t seconds] [-o summary.txt] [-f state-file] [-p cycle-ms] [-m memory-MB]\n"
        "          [-R replica-binary] [-C core,core,core] [-k kind:replica@cycle,...]\n",
        app);
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
    const char* replicaBinary = nullptr;
    int cores[REPLICAS] = {0, 1, 1};
    unsigned memoryMb = 128;
    int option = 0;
    while ((option = getopt(argc, argv, "hs:t:o:f:p:m:R:C:k:")) != -1) {
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
            case 'R':
                replicaBinary = optarg;
                break;
            case 'C':
                if (std::sscanf(optarg, "%d,%d,%d", &cores[0], &cores[1], &cores[2]) != 3) {
                    usage(argv[0]);
                    return 1;
                }
                break;
            case 'k':
                if (!parseFaults(optarg)) {
                    usage(argv[0]);
                    return 1;
                }
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

    // the replica processes first, before any thread or blocked signal: a
    // child starts with this process's signal mask
    char defaultReplica[512];
    if (replicaBinary == nullptr) {
        const char* slash = std::strrchr(argv[0], '/');
        const int dirLen = (slash == nullptr) ? 0 : static_cast<int>(slash - argv[0]);
        (void)std::snprintf(defaultReplica, sizeof(defaultReplica), "%.*s%sMruReplica", dirLen, argv[0],
                            (slash == nullptr) ? "./" : "/");
        replicaBinary = defaultReplica;
    }
    int ends[REPLICAS][2];
    for (int i = 0; i < REPLICAS; i++) {
        if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, ends[i]) != 0) {
            Fw::Logger::log("Could not make the socket pair for replica %d\n", i);
            return 1;
        }
    }
    for (int i = 0; i < REPLICAS; i++) {
        g_pids[i] = startReplica(replicaBinary, i, ends[i][1], cores[i], memoryMb);
        if (g_pids[i] <= 0) {
            Fw::Logger::log("Could not start replica %d (%s)\n", i, replicaBinary);
            return 1;
        }
        (void)::close(ends[i][1]);  // the replica's end lives in the replica
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
    MruQuorum::TopologyState state;
    state.statePath = statePath;
    state.cycleMs = cycleMs;
    for (int i = 0; i < REPLICAS; i++) {
        state.fds[i] = ends[i][0];
    }
    MruQuorum::setupTopology(state);
    MruQuorum::setCycleHook(onCycle);

    pthread_t sigThread;
    if (pthread_create(&sigThread, nullptr, signalThread, nullptr) != 0) {
        Fw::Logger::log("Could not start the signal thread\n");
        return 1;
    }

    // every replica linked before anything is asked of it
    const char* seqStatus = "none";
    bool linked = false;
    for (U32 waited = 0; waited < 5000 && !linked; waited += 10) {
        linked = MruQuorum::linksOpen();
        if (!linked) {
            Os::Task::delay(Fw::TimeInterval(0, 10000));
        }
    }
    if (!linked) {
        seqStatus = "no-link";
    }

    // the sequence first: the policy and the faults are set before cycle 1
    if (linked && sequence != nullptr) {
        MruQuorum::runSequence(sequence);
        seqStatus = "timeout";
        for (U32 waited = 0; waited < 10000; waited += 10) {
            const Mru::RunRecorder::Totals t = MruQuorum::totals();
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
    // start-up ends here: topology, tasks and the sequence are done
    const Sample started = sample();
    if (std::strcmp(seqStatus, "none") == 0 || std::strcmp(seqStatus, "ok") == 0) {
        MruQuorum::startRateGroups(state);
    } else {
        g_stopped = "sequence";
    }
    const Sample stopped = sample();

    // let the last cycle's replies and telemetry reach their queues' end
    Os::Task::delay(Fw::TimeInterval(0, 200000));
    const Mru::RunRecorder::Totals t = MruQuorum::totals();
    // every thread is still running here: the largest the address space gets
    const unsigned long vmKiB = addressSpaceKiB();
    MruQuorum::teardownTopology(state);
    // the links are closed: each replica sees its socket close and ends
    for (int i = 0; i < REPLICAS; i++) {
        (void)::close(ends[i][0]);
    }
    char replicas[160] = {};
    reapReplicas(replicas, sizeof(replicas));
    struct rusage children;
    (void)getrusage(RUSAGE_CHILDREN, &children);
    const double replicaCpu = seconds(children.ru_utime) + seconds(children.ru_stime);

    const Sample ended = sample();
    struct rusage usage;
    (void)getrusage(RUSAGE_SELF, &usage);
    const double cpu = ended.user + ended.system;
    const double wall = ended.wall - seconds(start);
    // the three phases: start-up (process start to the first timer cycle),
    // the timed run, and the end (draining the queues, teardown)
    const double runUser = stopped.user - started.user;
    const double runSystem = stopped.system - started.system;
    const double runWall = stopped.wall - started.wall;
    const double runShare = (runWall > 0.0) ? 100.0 * (runUser + runSystem) / runWall : 0.0;
    const double endCpu = (ended.user + ended.system) - (stopped.user + stopped.system);

    char text[2048];
    const int n = snprintf(
        text, sizeof(text),
        "policy=%s cycles=%llu cycle_ms=%u sequence=%s\n"
        "useful=%llu detected=%llu wrong=%llu late=%llu last_useful=%llu live_at_end=%u mode=%s "
        "slots=%s,%s,%s stopped=%s\n"
        "footprint: max_rss=%ldKiB address_space=%luKiB cpu=%.3fs wall=%.3fs\n"
        "cpu: startup_user=%.3fs startup_system=%.3fs startup_wall=%.3fs run_user=%.3fs run_system=%.3fs "
        "run_wall=%.3fs run_share=%.2f%% end_cpu=%.3fs\n"
        "replicas: %s cores=%d,%d,%d cpu=%.3fs experiment_share=%.2f%%\n"
        "limits: memory=%uMB/process max_run=%us\n",
        t.policy.e == Mru::Policy::TMR ? "tmr" : "shrink", static_cast<unsigned long long>(t.cycles), cycleMs,
        seqStatus, static_cast<unsigned long long>(t.correct), static_cast<unsigned long long>(t.caught),
        static_cast<unsigned long long>(t.wrong), static_cast<unsigned long long>(t.late),
        static_cast<unsigned long long>(t.lastCorrect), static_cast<unsigned>(t.live), modeName(t.mode),
        slotName(t.slots[0]), slotName(t.slots[1]), slotName(t.slots[2]), g_stopped, usage.ru_maxrss, vmKiB, cpu, wall,
        started.user, started.system, started.wall - seconds(start), runUser, runSystem, runWall, runShare, endCpu,
        replicas, cores[0], cores[1], cores[2], replicaCpu, (wall > 0.0) ? 100.0 * (cpu + replicaCpu) / wall : 0.0,
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
    return (std::strcmp(seqStatus, "failed") == 0 || std::strcmp(seqStatus, "timeout") == 0 ||
            std::strcmp(seqStatus, "no-link") == 0)
               ? 1
               : 0;
}
