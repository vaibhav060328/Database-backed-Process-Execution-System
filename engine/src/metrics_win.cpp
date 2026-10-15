#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN

#include "metrics.h"
#include <windows.h>
#include <psapi.h>

namespace {

std::uint64_t fileTimeToNs(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart * 100ULL; // FILETIME counts 100 ns intervals
}

std::uint64_t nowNs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return fileTimeToNs(ft);
}

} // namespace

ProcSampler::ProcSampler() : handle_(platform::invalidProc()) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    cores_ = static_cast<int>(si.dwNumberOfProcessors);
    if (cores_ < 1) cores_ = 1;
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
    HANDLE h = (handle_ != platform::invalidProc())
                   ? static_cast<HANDLE>(handle_)
                   : GetCurrentProcess();

    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(h, &created, &exited, &kernel, &user)) {
        // Process has exited or the handle no longer grants access.
        primed_ = false;
        return m;
    }

    const std::uint64_t cpuNs  = fileTimeToNs(kernel) + fileTimeToNs(user);
    const std::uint64_t wallNs = nowNs();

    if (primed_ && wallNs > prevWallNs_ && cpuNs >= prevCpuNs_) {
        const double dCpu  = static_cast<double>(cpuNs - prevCpuNs_);
        const double dWall = static_cast<double>(wallNs - prevWallNs_);
        m.cpuPercent = (dCpu / (dWall * cores_)) * 100.0;
        if (m.cpuPercent < 0.0)   m.cpuPercent = 0.0;
        if (m.cpuPercent > 100.0) m.cpuPercent = 100.0; // clamp scheduler jitter
    }

    prevCpuNs_  = cpuNs;
    prevWallNs_ = wallNs;
    primed_     = true;

    PROCESS_MEMORY_COUNTERS pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
        m.memoryMb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }

    return m;
}

#endif // _WIN32
