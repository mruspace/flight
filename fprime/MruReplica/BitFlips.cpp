// ======================================================================
// \title  BitFlips.cpp
// \brief  Real bit flips in a replica process's own memory (see the header)
// ======================================================================
#include "MruReplica/BitFlips.hpp"

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace MruReplica {

namespace {

constexpr int MAX_REGIONS = 128;
constexpr int PAGE_TRIES = 64;
constexpr unsigned long DEFAULT_MAX_FLIPS = 2000;
constexpr rlim_t FILE_LIMIT_BYTES = 256UL * 1024;

struct Region {
    unsigned long start;
    unsigned long end;
    char kind;  //!< t code, r read-only data, d data of a file, h heap, s main stack, a other memory
    bool writable;
    bool exec;
};

struct Settings {
    double rate;
    unsigned long long seed;
    unsigned long delayMs;
    unsigned long maxFlips;
    int replica;
    int logFd;
};

Settings g_settings;

unsigned long long next(unsigned long long& s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

//! The private, readable mappings of this process, except the kernel's own
//! pages (vdso, vvar, vectors, sigpage, vsyscall)
int readRegions(Region (&regions)[MAX_REGIONS]) {
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (maps == nullptr) {
        return 0;
    }
    int n = 0;
    char line[512];
    while (n < MAX_REGIONS && std::fgets(line, sizeof(line), maps) != nullptr) {
        unsigned long start = 0;
        unsigned long end = 0;
        char perms[5] = {};
        int pathAt = 0;
        if (std::sscanf(line, "%lx-%lx %4s %*s %*s %*s %n", &start, &end, perms, &pathAt) < 3) {
            continue;
        }
        if (perms[0] != 'r' || perms[3] != 'p' || end <= start) {
            continue;
        }
        const char* path = line + pathAt;
        if (std::strncmp(path, "[v", 2) == 0 || std::strncmp(path, "[sigpage]", 9) == 0) {
            continue;
        }
        Region& r = regions[n];
        r.start = start;
        r.end = end;
        r.writable = (perms[1] == 'w');
        r.exec = (perms[2] == 'x');
        if (std::strncmp(path, "[heap]", 6) == 0) {
            r.kind = 'h';
        } else if (std::strncmp(path, "[stack]", 7) == 0) {
            r.kind = 's';
        } else if (path[0] == '/') {
            r.kind = r.exec ? 't' : (r.writable ? 'd' : 'r');
        } else {
            r.kind = r.exec ? 't' : 'a';
        }
        n++;
    }
    std::fclose(maps);
    return n;
}

//! Flip one random bit on a page that is in memory. Writes the log line
//! first, so a flip that crashes the process is still on record.
bool flipOne(unsigned long long& s, unsigned long count, unsigned long ms) {
    Region regions[MAX_REGIONS];
    const int n = readRegions(regions);
    unsigned long total = 0;
    for (int i = 0; i < n; i++) {
        total += regions[i].end - regions[i].start;
    }
    if (total == 0) {
        return false;
    }
    const long page = sysconf(_SC_PAGESIZE);
    const unsigned long pageSize = (page > 0) ? static_cast<unsigned long>(page) : 4096UL;
    for (int t = 0; t < PAGE_TRIES; t++) {
        unsigned long pick = static_cast<unsigned long>(next(s) % total);
        int i = 0;
        while (i < n && pick >= regions[i].end - regions[i].start) {
            pick -= regions[i].end - regions[i].start;
            i++;
        }
        if (i >= n) {
            continue;
        }
        const Region& r = regions[i];
        const unsigned long addr = r.start + pick;
        const unsigned long pageStart = addr & ~(pageSize - 1);
        unsigned char resident = 0;
        if (mincore(reinterpret_cast<void*>(pageStart), pageSize, &resident) != 0 || (resident & 1U) == 0) {
            continue;  // not in memory: no cell for a particle to hit
        }
        const unsigned bit = static_cast<unsigned>(next(s) % 8);
        char line[96];
        const int len = std::snprintf(line, sizeof(line), "%lu %lu %c %08lx %u\n", count, ms, r.kind, addr, bit);
        if (g_settings.logFd >= 0 && len > 0) {
            (void)write(g_settings.logFd, line, static_cast<size_t>(len));
        }
        volatile unsigned char* cell = reinterpret_cast<volatile unsigned char*>(addr);
        const unsigned char mask = static_cast<unsigned char>(1U << bit);
        if (r.writable) {
            *cell = static_cast<unsigned char>(*cell ^ mask);
            return true;
        }
        // code and read-only data: open the page for writing, flip, close it
        void* p = reinterpret_cast<void*>(pageStart);
        const int exec = r.exec ? PROT_EXEC : 0;
        if (mprotect(p, pageSize, PROT_READ | PROT_WRITE | exec) != 0) {
            if (g_settings.logFd >= 0) {
                (void)write(g_settings.logFd, "refused\n", 8);
            }
            return false;
        }
        *cell = static_cast<unsigned char>(*cell ^ mask);
        (void)mprotect(p, pageSize, PROT_READ | exec);
        if (r.exec) {
            __builtin___clear_cache(reinterpret_cast<char*>(pageStart), reinterpret_cast<char*>(pageStart + pageSize));
        }
        return true;
    }
    return false;
}

unsigned long elapsedMs(const struct timespec& from) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    const long long ms = (static_cast<long long>(now.tv_sec - from.tv_sec) * 1000LL) +
                         ((now.tv_nsec - from.tv_nsec) / 1000000L);
    return (ms > 0) ? static_cast<unsigned long>(ms) : 0UL;
}

void* flipThread(void*) {
    struct timespec start;
    (void)clock_gettime(CLOCK_MONOTONIC, &start);
    // one stream per replica: the seed and the replica number, mixed
    unsigned long long s = (g_settings.seed * 0x9E3779B97F4A7C15ULL) ^ (static_cast<unsigned long long>(g_settings.replica + 1) * 0xBF58476D1CE4E5B9ULL);
    s = (s == 0) ? 1ULL : s;
    for (int i = 0; i < 8; i++) {
        (void)next(s);
    }
    struct timespec delay;
    delay.tv_sec = static_cast<time_t>(g_settings.delayMs / 1000);
    delay.tv_nsec = static_cast<long>((g_settings.delayMs % 1000) * 1000000UL);
    (void)nanosleep(&delay, nullptr);
    // exponential gaps, as for independent particles, at the mean rate
    const double meanNs = 1e9 / g_settings.rate;
    for (unsigned long count = 1; count <= g_settings.maxFlips; count++) {
        const double u = (static_cast<double>(next(s) % 1000000ULL) + 0.5) / 1000000.0;
        double gap = -meanNs * __builtin_log(u);
        gap = (gap > 5e9) ? 5e9 : gap;
        struct timespec wait;
        wait.tv_sec = static_cast<time_t>(gap / 1e9);
        wait.tv_nsec = static_cast<long>(gap - (static_cast<double>(wait.tv_sec) * 1e9));
        (void)nanosleep(&wait, nullptr);
        (void)flipOne(s, count, elapsedMs(start));
    }
    return nullptr;
}

void setLimit(int resource, rlim_t soft, rlim_t hard) {
    struct rlimit limit;
    limit.rlim_cur = soft;
    limit.rlim_max = hard;
    (void)setrlimit(resource, &limit);
}

}  // namespace

bool startBitFlips(int replica) {
    // a crashed replica must not leave a core file in the output folder
    setLimit(RLIMIT_CORE, 0, 0);
    const char* flips = std::getenv("MRU_FLIPS");
    if (flips == nullptr || flips[0] == '\0') {
        return true;
    }
    double rate = 0.0;
    unsigned long long seed = 0;
    unsigned long delayMs = 0;
    if (std::sscanf(flips, "%lf:%llu:%lu", &rate, &seed, &delayMs) != 3 || !(rate > 0.0) || rate > 10000.0) {
        std::fprintf(stderr, "MruReplica: MRU_FLIPS must be rate:seed:delay_ms\n");
        return false;
    }
    const char* max = std::getenv("MRU_FLIPS_MAX");
    const char* cpu = std::getenv("MRU_CPU_SECONDS");
    const char* log = std::getenv("MRU_FLIPS_LOG");
    g_settings.rate = rate;
    g_settings.seed = seed;
    g_settings.delayMs = delayMs;
    g_settings.maxFlips = (max != nullptr) ? std::strtoul(max, nullptr, 10) : DEFAULT_MAX_FLIPS;
    g_settings.replica = (replica >= 0) ? replica : 0;
    g_settings.logFd = -1;
    // a flip can set the process spinning or writing without end: bound both
    if (cpu != nullptr) {
        const rlim_t seconds = static_cast<rlim_t>(std::strtoul(cpu, nullptr, 10));
        if (seconds > 0) {
            setLimit(RLIMIT_CPU, seconds, seconds + 1);
        }
    }
    setLimit(RLIMIT_FSIZE, FILE_LIMIT_BYTES, FILE_LIMIT_BYTES);
    if (log != nullptr && log[0] != '\0') {
        char path[256];
        (void)std::snprintf(path, sizeof(path), "%s%d.txt", log, g_settings.replica);
        g_settings.logFd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    }
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    const int r = pthread_create(&thread, &attr, flipThread, nullptr);
    pthread_attr_destroy(&attr);
    std::printf("replica %d: bit flips at %.1f per second from %lu ms, seed %llu, at most %lu: %s\n",
                g_settings.replica, rate, delayMs, seed, g_settings.maxFlips, r == 0 ? "started" : "failed");
    std::fflush(stdout);
    return r == 0;
}

}  // namespace MruReplica
