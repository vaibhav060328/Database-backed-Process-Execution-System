#ifndef WORKER_PROC_H
#define WORKER_PROC_H

#include <string>

/**
 * worker_proc.h
 * Entry point for a worker PROCESS (engine --role worker).
 *
 * A worker holds no database credentials. It learns about a job only from the
 * assignment message the dispatcher writes to its stdin, fetches the file over
 * plain HTTP from the backend (an endpoint that needs no credentials), runs it
 * as a child process, and reports everything back as NDJSON on its stdout.
 *
 * Its stdout is protocol only. Diagnostics go to stderr.
 */
struct WorkerOptions {
    std::string workerId;              // unique across machines
    std::string baseDir;               // sandbox root
    std::string backendHost = "localhost";
    int         backendPort = 3000;

    /**
     * Keep each job's sandbox directory after it finishes.
     *
     * Off by default: every job leaves its downloaded script and any compiled
     * binary behind, so retaining them grows without bound (a C++ job alone
     * leaves a ~140 KB binary). Turn it on to inspect what a job actually ran.
     */
    bool keepSandbox = false;

    /**
     * Per-job memory ceiling in MB (0 = unlimited).
     *
     * Enforced by the OS: a Windows Job Object or a POSIX RLIMIT_AS. This is
     * the concrete benefit of running jobs as processes - a thread could not be
     * capped this way.
     */
    int maxMemoryMb = 0;
};

/** Runs until the dispatcher sends shutdown or closes stdin. Returns an exit code. */
int runWorkerProcess(const WorkerOptions& opts);

#endif // WORKER_PROC_H
