// quorum: a small Linux prototype of Mru's shrinking quorum.
//
// Three replica processes run the same deterministic task. A voter compares
// their outputs and steps down as replicas fail:
//
//   shrink: vote on three, compare on two, self-check on one (half rate)
//   tmr:    vote on three, compare on two, stop on one (classic fixed TMR)
//
// Faults are injected by the voter on a schedule or at random, so every run
// is reproducible from its seed. The program reports useful results, detected
// and undetected errors, when (if ever) it halted, and its own footprint.
//
// This is the stepping stone to F' components; see ../README.md.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr int kReplicas = 3;
constexpr std::size_t kBlockWords = 512;  // 4 KiB of work per result

// ---- the payload task: hash a pseudo-random block derived from the tick ----

uint64_t xorshift(uint64_t &s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

uint64_t task(uint64_t tick) {
    uint64_t s = tick * 0x9E3779B97F4A7C15ULL + 1;
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a over the block
    for (std::size_t i = 0; i < kBlockWords; ++i) {
        uint64_t w = xorshift(s);
        for (int b = 0; b < 8; ++b) {
            h ^= (w >> (8 * b)) & 0xff;
            h *= 0x100000001b3ULL;
        }
    }
    return h;
}

// ---- voter <-> replica protocol over pipes ----

enum Flags : uint8_t {
    kFlipFirst = 1 << 0,   // corrupt the (first) result
    kSelfCheck = 1 << 1,   // compute twice and return both results
    kFlipSecond = 1 << 2,  // corrupt the second result as well (common mode)
    kStuck = 1 << 3,       // persistent fault: the replica returns a fixed wrong value
};

struct Request {
    uint64_t tick;
    uint8_t flags;
    uint8_t bit;
};

struct Response {
    uint64_t first;
    uint64_t second;
};

bool write_all(int fd, const void *p, std::size_t n) {
    const char *c = static_cast<const char *>(p);
    while (n > 0) {
        ssize_t w = write(fd, c, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        c += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

bool read_all(int fd, void *p, std::size_t n) {
    char *c = static_cast<char *>(p);
    while (n > 0) {
        ssize_t r = read(fd, c, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        c += r;
        n -= static_cast<std::size_t>(r);
    }
    return true;
}

[[noreturn]] void replica_main(int in, int out) {
    Request q;
    while (read_all(in, &q, sizeof q)) {
        Response r{task(q.tick), 0};
        if (q.flags & kSelfCheck) r.second = task(q.tick);
        uint64_t mask = 1ULL << (q.bit % 64);
        if (q.flags & kFlipFirst) r.first ^= mask;
        if (q.flags & kFlipSecond) r.second ^= mask;
        if (q.flags & kStuck) r.first = r.second = 0xdeadbeefULL;
        if (!write_all(out, &r, sizeof r)) break;
    }
    _exit(0);
}

struct Replica {
    pid_t pid = -1;
    int to = -1;    // voter writes requests here
    int from = -1;  // voter reads responses here
    bool alive = false;
    bool stuck = false;              // persistent fault: output is wrong from now on
    std::vector<uint64_t> strikes;   // ticks where this replica was outvoted
};

Replica spawn(const std::vector<Replica> &earlier) {
    int a[2], b[2];
    if (pipe(a) != 0 || pipe(b) != 0) { perror("pipe"); std::exit(2); }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); std::exit(2); }
    if (pid == 0) {
        // drop the pipes of earlier replicas, so they see end-of-input on shutdown
        for (const Replica &e : earlier) { close(e.to); close(e.from); }
        close(a[1]);
        close(b[0]);
        replica_main(a[0], b[1]);
    }
    close(a[0]);
    close(b[1]);
    Replica r;
    r.pid = pid;
    r.to = a[1];
    r.from = b[0];
    r.alive = true;
    return r;
}

void retire(Replica &r) {
    if (!r.alive) return;
    kill(r.pid, SIGKILL);
    waitpid(r.pid, nullptr, 0);
    close(r.to);
    close(r.from);
    r.alive = false;
}

// Known-answer test: run a replica on a fixed input whose result is known in
// advance. It identifies which side is wrong when two replicas disagree, and it
// catches a replica that is stuck but self-consistent.
constexpr uint64_t kKatInput = 0;

bool known_answer_ok(Replica &x, uint64_t expected) {
    Request q{kKatInput, x.stuck ? uint8_t(kStuck) : uint8_t(0), 0};
    Response a{};
    if (!write_all(x.to, &q, sizeof q) || !read_all(x.from, &a, sizeof a)) return false;
    return a.first == expected;
}

// ---- faults ----

struct Fault {
    enum Kind { Kill, Corrupt, Stuck } kind;
    int replica;
    uint64_t tick;
};

bool parse_fault(const char *s, Fault &f) {
    char kind[16];
    int replica;
    unsigned long long tick;
    if (std::sscanf(s, "%15[a-z]:%d@%llu", kind, &replica, &tick) != 3) return false;
    if (replica < 0 || replica >= kReplicas) return false;
    f.replica = replica;
    f.tick = tick;
    if (!std::strcmp(kind, "kill")) f.kind = Fault::Kill;
    else if (!std::strcmp(kind, "corrupt")) f.kind = Fault::Corrupt;
    else if (!std::strcmp(kind, "stuck")) f.kind = Fault::Stuck;
    else return false;
    return true;
}

// ---- run ----

struct Options {
    std::string policy = "shrink";
    uint64_t ticks = 10000;
    uint64_t seed = 1;
    double upset_rate = 0.0;   // per replica per tick
    double common_mode = 0.05; // share of single-replica upsets that hit both self-check runs
    std::vector<Fault> faults;
    const char *log = nullptr;
};

struct Totals {
    uint64_t useful = 0;      // correct results delivered
    uint64_t detected = 0;    // ticks where a disagreement was caught and no result was delivered
    uint64_t wrong = 0;       // results delivered that were wrong (undetected errors)
    uint64_t halted_at = 0;   // tick at which the system stopped, 0 if it never did
    int alive_at_end = 0;
};

[[noreturn]] void usage() {
    std::fprintf(stderr,
        "usage: quorum [--policy shrink|tmr] [--ticks N] [--seed S]\n"
        "              [--upset-rate R] [--common-mode C]\n"
        "              [--fault kill:I@T] [--fault corrupt:I@T] [--fault stuck:I@T] ...\n"
        "              [--log file.csv]\n");
    std::exit(2);
}

Options parse(int argc, char **argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char * { if (i + 1 >= argc) usage(); return argv[++i]; };
        if (a == "--policy") o.policy = next();
        else if (a == "--ticks") o.ticks = std::strtoull(next(), nullptr, 10);
        else if (a == "--seed") o.seed = std::strtoull(next(), nullptr, 10);
        else if (a == "--upset-rate") o.upset_rate = std::strtod(next(), nullptr);
        else if (a == "--common-mode") o.common_mode = std::strtod(next(), nullptr);
        else if (a == "--fault") { Fault f; if (!parse_fault(next(), f)) usage(); o.faults.push_back(f); }
        else if (a == "--log") o.log = next();
        else usage();
    }
    if (o.policy != "shrink" && o.policy != "tmr") usage();
    return o;
}

double uniform(uint64_t &s) { return (xorshift(s) >> 11) * (1.0 / 9007199254740992.0); }

long max_rss_kib(int who) {
    rusage u{};
    getrusage(who, &u);
#ifdef __APPLE__
    return u.ru_maxrss / 1024;  // bytes on macOS
#else
    return u.ru_maxrss;         // KiB on Linux
#endif
}

double cpu_seconds(int who) {
    rusage u{};
    getrusage(who, &u);
    return u.ru_utime.tv_sec + u.ru_stime.tv_sec + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}

}  // namespace

int main(int argc, char **argv) {
    Options o = parse(argc, argv);
    const bool shrink = o.policy == "shrink";
    signal(SIGPIPE, SIG_IGN);

    FILE *log = o.log ? std::fopen(o.log, "w") : nullptr;
    if (log) std::fprintf(log, "tick,alive,mode,event,replica\n");

    std::vector<Replica> r;
    for (int i = 0; i < kReplicas; ++i) r.push_back(spawn(r));

    const uint64_t kat = task(kKatInput);
    uint64_t rng = o.seed * 0x9E3779B97F4A7C15ULL + 7;
    Totals t;
    auto t0 = std::chrono::steady_clock::now();

    for (uint64_t tick = 1; tick <= o.ticks; ++tick) {
        // scheduled faults
        std::vector<bool> corrupt(kReplicas, false);
        for (const Fault &f : o.faults) {
            if (f.tick != tick || !r[f.replica].alive) continue;
            if (f.kind == Fault::Kill) {
                retire(r[f.replica]);
                if (log) std::fprintf(log, "%" PRIu64 ",,,%s,%d\n", tick, "killed", f.replica);
            } else if (f.kind == Fault::Corrupt) {
                corrupt[f.replica] = true;
            } else {
                r[f.replica].stuck = true;
                if (log) std::fprintf(log, "%" PRIu64 ",,,%s,%d\n", tick, "stuck", f.replica);
            }
        }

        std::vector<int> live;
        for (int i = 0; i < kReplicas; ++i) if (r[i].alive) live.push_back(i);
        const int alive = static_cast<int>(live.size());

        // fixed TMR stops once fewer than two replicas survive
        if (alive == 0 || (!shrink && alive < 2)) {
            t.halted_at = tick;
            if (log) std::fprintf(log, "%" PRIu64 ",%d,,halted,\n", tick, alive);
            break;
        }

        // self-check on one replica computes twice, so it delivers at half rate
        const bool self_check = alive == 1;
        if (self_check && tick % 2 == 1) continue;

        const uint64_t truth = task(tick);
        std::vector<uint64_t> out(kReplicas, 0);
        std::vector<bool> ok(kReplicas, false);
        bool self_check_agrees = true;

        for (int i : live) {
            Request q{tick, 0, static_cast<uint8_t>(xorshift(rng) & 63)};
            if (r[i].stuck) q.flags |= kStuck;
            bool upset = corrupt[i] || (o.upset_rate > 0 && uniform(rng) < o.upset_rate);
            if (self_check) {
                q.flags |= kSelfCheck;
                if (upset) {
                    q.flags |= kFlipFirst;
                    if (uniform(rng) < o.common_mode) q.flags |= kFlipSecond;  // both runs hit alike
                }
            } else if (upset) {
                q.flags |= kFlipFirst;
            }
            Response a{};
            if (!write_all(r[i].to, &q, sizeof q) || !read_all(r[i].from, &a, sizeof a)) {
                retire(r[i]);  // the replica died on its own
                continue;
            }
            out[i] = a.first;
            ok[i] = true;
            if (self_check) self_check_agrees = a.first == a.second;
        }

        // decide
        bool delivered = false;
        uint64_t result = 0;
        std::vector<int> got;
        for (int i : live) if (ok[i]) got.push_back(i);
        const char *mode = got.size() >= 3 ? "vote" : got.size() == 2 ? "compare" : "self-check";

        if (got.size() >= 3) {
            for (int i : got) {
                int agree = 0;
                for (int j : got) agree += out[j] == out[i];
                if (agree >= 2) { delivered = true; result = out[i]; break; }
            }
            if (delivered && shrink) {
                // health scoring: a replica outvoted 3 times within 200 ticks is retired
                for (int i : got) {
                    if (out[i] == result) continue;
                    auto &s = r[i].strikes;
                    s.push_back(tick);
                    s.erase(std::remove_if(s.begin(), s.end(), [&](uint64_t x) { return x + 200 < tick; }), s.end());
                    if (s.size() >= 3) {
                        retire(r[i]);
                        if (log) std::fprintf(log, "%" PRIu64 ",,,%s,%d\n", tick, "retired", i);
                    }
                }
            }
        } else if (got.size() == 2) {
            if (out[got[0]] == out[got[1]]) {
                delivered = true;
                result = out[got[0]];
            } else if (shrink) {
                // disagreement: a known-answer test tells which side failed
                for (int i : got) {
                    if (!known_answer_ok(r[i], kat)) {
                        retire(r[i]);
                        if (log) std::fprintf(log, "%" PRIu64 ",,,%s,%d\n", tick, "retired", i);
                    }
                }
            }
        } else if (got.size() == 1) {
            if (self_check_agrees) { delivered = true; result = out[got[0]]; }
            // on one replica, a periodic known-answer test catches a stuck fault
            if (tick % 64 == 0 && !known_answer_ok(r[got[0]], kat)) {
                delivered = false;
                retire(r[got[0]]);
                if (log) std::fprintf(log, "%" PRIu64 ",,,%s,%d\n", tick, "retired", got[0]);
            }
        }

        if (!delivered) {
            ++t.detected;
            if (log) std::fprintf(log, "%" PRIu64 ",%d,%s,detected,\n", tick, alive, mode);
        } else if (result == truth) {
            ++t.useful;
        } else {
            ++t.wrong;
            if (log) std::fprintf(log, "%" PRIu64 ",%d,%s,wrong,\n", tick, alive, mode);
        }
    }

    for (Replica &x : r) if (x.alive) ++t.alive_at_end;
    for (Replica &x : r) if (x.alive) { close(x.to); close(x.from); waitpid(x.pid, nullptr, 0); x.alive = false; }

    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("policy=%s ticks=%" PRIu64 " seed=%" PRIu64 " upset_rate=%g\n",
                o.policy.c_str(), o.ticks, o.seed, o.upset_rate);
    std::printf("useful=%" PRIu64 " detected=%" PRIu64 " wrong=%" PRIu64 " halted_at=%s alive_at_end=%d\n",
                t.useful, t.detected, t.wrong,
                t.halted_at ? std::to_string(t.halted_at).c_str() : "never", t.alive_at_end);
    std::printf("footprint: voter_max_rss=%ldKiB replica_max_rss=%ldKiB cpu=%.2fs wall=%.2fs\n",
                max_rss_kib(RUSAGE_SELF), max_rss_kib(RUSAGE_CHILDREN),
                cpu_seconds(RUSAGE_SELF) + cpu_seconds(RUSAGE_CHILDREN), wall);
    if (log) std::fclose(log);
    return 0;
}
