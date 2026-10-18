#ifndef DISPATCHER_H
#define DISPATCHER_H

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "ipc.h"
#include "pg_client.h"
#include "platform.h"

/**
 * dispatcher.h
 * Supervises N worker PROCESSES and feeds them jobs claimed from PostgreSQL.
 *
 * Replaces the old Scheduler, which ran a detached thread per job inside one
 * process. The dispatcher is single-threaded by design: there is no shared
 * mutable state between workers, so the mutexes, atomics and data races that
 * the threaded version needed simply do not arise.
 */
struct WorkerSlot {
    std::string           id;            // "<host>-w1"; stable across respawns
    platform::SpawnResult sp;
    ipc::LineReader       outReader;
    ipc::LineReader       errReader;

    bool   ready        = false;  // has sent its ready handshake
    bool   registered   = false;  // workers row exists (required before claiming)
    bool   busy         = false;
    int    currentJobId = 0;
    double cpu          = 0.0;
    double memoryMb     = 0.0;
    int    restarts     = 0;
};

struct DispatcherOptions {
    int         numWorkers  = 2;
    std::string baseDir;
    std::string backendHost = "localhost";
    int         backendPort = 3000;
    int         jobTimeoutSeconds = 0; // 0 disables the wall-clock limit
    bool        keepSandbox = false;   // passed through to each worker
    int         maxMemoryMb = 0;       // per-job memory cap, passed to each worker
};

class Dispatcher {
public:
    Dispatcher(PgClient& db, DispatcherOptions opts);
    ~Dispatcher();

    /** Spawn the workers. Returns false if none could be started. */
    bool start();

    /** Run until `shutdownRequested` becomes true. */
    void run(const std::atomic<bool>& shutdownRequested);

    /** Ask workers to exit, wait briefly, then force them down. */
    void stop();

private:
    bool spawnWorker(WorkerSlot& slot);
    void drain(WorkerSlot& slot);
    void handleMessage(WorkerSlot& slot, const ipc::Message& m);
    void claimAndAssign();
    void heartbeat();
    void checkCancellations();
    void checkTimeouts();
    void superviseWorkers();
    void releaseSlot(WorkerSlot& slot);

    PgClient&                 db_;
    DispatcherOptions         opts_;
    std::string               enginePath_;
    std::vector<std::unique_ptr<WorkerSlot>> slots_;

    std::chrono::steady_clock::time_point lastHeartbeat_{};
    std::chrono::steady_clock::time_point lastCancelCheck_{};
    std::chrono::steady_clock::time_point lastReclaim_{};
    std::vector<std::chrono::steady_clock::time_point> jobStartedAt_;
    bool stopped_ = false;
};

#endif // DISPATCHER_H
