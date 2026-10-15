#ifndef _WIN32

#include "metrics.h"
#include <unistd.h>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

namespace {

std::uint64_t nowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

/**
 * utime + stime from /proc/<pid>/stat, converted to nanoseconds.
 *
 * Field 2 (comm) is the executable name in parentheses and may itself contain
 * spaces and parentheses, so the fields are counted from the LAST ')' rather
 * than by splitting the whole line. After that point index 0 is field 3, which
 * puts utime (field 14) at index 11 and stime (field 15) at index 12.
 */
bool readCpuNs(int pid, std::uint64_t& outNs) {
    std::ostringstream path;
    path << "/proc/" << pid << "/stat";
    std::ifstream f(path.str());
    if (!f.is_open()) return false;

    std::string line;
    std::getline(f, line);
    const size_t close = line.rfind(')');
    if (close == std::string::npos) return false;

    std::istringstream rest(line.substr(close + 1));
    std::vector<std::string> fields;
    std::string tok;
    while (rest >> tok) fields.push_back(tok);
    if (fields.size() < 13) return false;

    const long ticksPerSec = sysconf(_SC_CLK_TCK);
    if (ticksPerSec <= 0) return false;

    unsigned long long utime = std::strtoull(fields[11].c_str(), nullptr, 10);
    unsigned long long stime = std::strtoull(fields[12].c_str(), nullptr, 10);

    outNs = static_cast<std::uint64_t>(
        (static_cast<long double>(utime + stime) / ticksPerSec) * 1e9L);
    return true;
}

/** VmRSS from /proc/<pid>/status, reported in kB. */
bool readRssMb(int pid, double& outMb) {
    std::ostringstream path;
    path << "/proc/" << pid << "/status";
    std::ifstream f(path.str());
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            unsigned long long kb = 0;
            if (std::sscanf(line.c_str(), "VmRSS: %llu", &kb) == 1) {
                outMb = static_cast<double>(kb) / 1024.0;
                return true;
            }
            return false;
        }
    }
    return false;
}

} // namespace

ProcSampler::ProcSampler() : handle_(platform::invalidProc()) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    cores_ = (n > 0) ? static_cast<int>(n) : 1;
}

void ProcSampler::target(platform::ProcHandle h) {
    handle_ = h;
    primed_ = false;
}

void ProcSampler::clear() {
    handle_ = platform::invalidProc();
    primed_ = false;
}

ProcMetrics ProcSampler::sample() {
    ProcMetrics m;
    const int pid = (handle_ != platform::invalidProc())
                        ? static_cast<int>(handle_)
                        : static_cast<int>(::getpid());

    std::uint64_t cpuNs = 0;
    if (!readCpuNs(pid, cpuNs)) {
        primed_ = false;
        return m; // process gone
    }
    const std::uint64_t wallNs = nowNs();

    if (primed_ && wallNs > prevWallNs_ && cpuNs >= prevCpuNs_) {
        const double dCpu  = static_cast<double>(cpuNs - prevCpuNs_);
        const double dWall = static_cast<double>(wallNs - prevWallNs_);
        m.cpuPercent = (dCpu / (dWall * cores_)) * 100.0;
        if (m.cpuPercent < 0.0)   m.cpuPercent = 0.0;
        if (m.cpuPercent > 100.0) m.cpuPercent = 100.0;
    }

    prevCpuNs_  = cpuNs;
    prevWallNs_ = wallNs;
    primed_     = true;

    double mb = 0.0;
    if (readRssMb(pid, mb)) m.memoryMb = mb;

    return m;
}

#endif // !_WIN32
