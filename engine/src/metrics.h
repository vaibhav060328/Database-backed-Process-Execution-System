#ifndef METRICS_H
#define METRICS_H

#include "platform.h"
#include <cstdint>

/**
 * metrics.h
 * Real per-process CPU and memory sampling.
 *
 * Replaces the previous placeholders, which returned rand() for CPU and
 * system-wide memory load for memory.
 */
struct ProcMetrics {
    double cpuPercent = 0.0; // 0..100, normalised across all cores
    double memoryMb   = 0.0; // resident set (POSIX) / working set (Windows)
};

/**
 * Samples one process over time.
 *
 * CPU usage is a RATE, so it needs two observations: the first sample() after
 * construction or target() establishes the baseline and reports 0% CPU. Memory
 * is an instantaneous reading and is correct from the first call.
 *
 * Not thread-safe; callers must serialise access (Worker holds a mutex).
 */
class ProcSampler {
public:
    ProcSampler();

    /** Point at another process. Resets the CPU baseline. */
    void target(platform::ProcHandle h);

    /** Go back to sampling the calling process. Resets the CPU baseline. */
    void clear();

    /** CPU averaged over the interval since the previous call, plus live memory. */
    ProcMetrics sample();

private:
    platform::ProcHandle handle_;
    bool          primed_     = false;
    std::uint64_t prevCpuNs_  = 0;
    std::uint64_t prevWallNs_ = 0;
    int           cores_      = 1;
};

#endif // METRICS_H
