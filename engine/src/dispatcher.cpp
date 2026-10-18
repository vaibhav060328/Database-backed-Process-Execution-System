#include "dispatcher.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace {

std::string isoNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tmv, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

constexpr auto kTick            = std::chrono::milliseconds(100);
constexpr auto kHeartbeatPeriod = std::chrono::seconds(4);
constexpr auto kCancelPeriod    = std::chrono::seconds(2);
constexpr auto kReclaimPeriod   = std::chrono::seconds(30);

} // namespace

Dispatcher::Dispatcher(PgClient& db, DispatcherOptions opts)
    : db_(db), opts_(std::move(opts)) {
    enginePath_ = platform::exePath();
}

Dispatcher::~Dispatcher() { stop(); }

bool Dispatcher::start() {
    if (enginePath_.empty()) {
        std::cerr << "Cannot determine the engine executable path." << std::endl;
        return false;
    }

    const std::string host = platform::hostName();
    int started = 0;

    for (int i = 1; i <= opts_.numWorkers; ++i) {
        auto slot = std::make_unique<WorkerSlot>();
        // Identity = host + dispatcher pid + slot index.
        //   - the hostname keeps two machines apart;
        //   - the dispatcher's pid keeps two dispatchers on ONE machine apart,
        //     which the old "<host>-w1" scheme did not;
        //   - the slot index is stable across worker respawns, so a restarted
        //     worker keeps its identity within this dispatcher's lifetime.
        slot->id = host + "-" + std::to_string(platform::currentPid()) +
                   "-w" + std::to_string(i);

        if (spawnWorker(*slot)) {
            ++started;
        } else {
            std::cerr << "Failed to start worker " << slot->id << std::endl;
        }
        slots_.push_back(std::move(slot));
    }

    jobStartedAt_.assign(slots_.size(), std::chrono::steady_clock::time_point{});
    std::cout << "Dispatcher started " << started << " of " << opts_.numWorkers
              << " worker processes." << std::endl;
    return started > 0;
}

bool Dispatcher::spawnWorker(WorkerSlot& slot) {
    std::vector<std::string> args = {
        "--role", "worker",
        "--id",   slot.id,
        "--dir",  opts_.baseDir,
        "--host", opts_.backendHost,
        "--port", std::to_string(opts_.backendPort),
    };
    if (opts_.keepSandbox) args.push_back("--keep-sandbox");
    if (opts_.maxMemoryMb > 0) {
        args.push_back("--max-memory");
        args.push_back(std::to_string(opts_.maxMemoryMb));
    }

    // No envExtra: buildChildEnvironment already strips credentials, so the
    // worker cannot see ENGINE_DATABASE_URL even though the dispatcher holds it.
    auto sp = platform::spawnProcess(enginePath_, args, {}, opts_.baseDir);
    if (!sp.ok) {
        std::cerr << "spawn " << slot.id << ": " << sp.error << std::endl;
        return false;
    }

    slot.sp           = sp;
    slot.ready        = false;
    slot.registered   = false;
    slot.busy         = false;
    slot.currentJobId = 0;
    slot.outReader    = ipc::LineReader();
    slot.errReader    = ipc::LineReader();
    return true;
}

void Dispatcher::drain(WorkerSlot& slot) {
    if (slot.sp.proc == platform::invalidProc()) return;

    char buf[8192];

    // Protocol stream. This must run every tick for every worker: a pipe has a
    // small fixed OS buffer, and a worker blocked writing to a full pipe stops
    // responding entirely. That deadlock is the likeliest failure in this design.
    for (;;) {
        const int n = platform::readPipe(slot.sp.stdoutR, buf, sizeof(buf));
        if (n <= 0) break;
        for (const auto& line : slot.outReader.feed(buf, static_cast<size_t>(n))) {
            if (line.empty()) continue;
            const ipc::Message m = ipc::parse(line);
            if (m.type.empty()) {
                std::cerr << "[" << slot.id << "] unparseable: " << m.rawError << std::endl;
                continue;
            }
            handleMessage(slot, m);
        }
    }

    // Diagnostics. Drained for the same reason, even though it only gets logged.
    for (;;) {
        const int n = platform::readPipe(slot.sp.stderrR, buf, sizeof(buf));
        if (n <= 0) break;
        for (const auto& line : slot.errReader.feed(buf, static_cast<size_t>(n))) {
            if (!line.empty()) std::cerr << "[" << slot.id << "] " << line << std::endl;
        }
    }
}

void Dispatcher::handleMessage(WorkerSlot& slot, const ipc::Message& m) {
    if (m.type == ipc::kReady) {
        slot.ready = true;

        // jobs."workerId" is a foreign key to workers(id), so the row must
        // exist BEFORE this worker's first claim or the claim fails outright.
        slot.registered = db_.upsertWorker(slot.id, "online", 0.0, 0.0, std::nullopt);
        if (!slot.registered) {
            std::cerr << "Could not register worker " << slot.id << ": "
                      << db_.lastError() << std::endl;
        }

        std::cout << "Worker " << slot.id << " ready (pid " << m.pid << ")" << std::endl;

    } else if (m.type == ipc::kLog) {
        db_.insertJobLog(m.jobId, m.line, m.level);

    } else if (m.type == ipc::kStarted) {
        // The claim already set status and startedAt; nothing more to write.

    } else if (m.type == ipc::kMetrics) {
        slot.cpu      = m.cpu;
        slot.memoryMb = m.memMb;

        // Publish immediately rather than waiting for the 4 second heartbeat.
        // A job that finishes in two seconds would otherwise never show any CPU
        // or memory at all, which is most jobs. Workers emit metrics once per
        // second, so this is naturally bounded to one write per second per busy
        // worker.
        db_.upsertWorker(slot.id, slot.busy ? "busy" : "online",
                         slot.cpu, slot.memoryMb,
                         slot.busy ? std::optional<int>(slot.currentJobId) : std::nullopt);

    } else if (m.type == ipc::kFinished) {
        const std::string status = (m.exitCode == 0) ? "done"
                                 : (m.exitCode < 0)  ? "cancelled"
                                                     : "failed";
        db_.updateJobStatus(m.jobId, status, m.exitCode, "",
                            m.timestamp.empty() ? isoNow() : m.timestamp);
        std::cout << "Job " << m.jobId << " -> " << status
                  << " (exit " << m.exitCode << ") on " << slot.id << std::endl;
        releaseSlot(slot);

    } else if (m.type == ipc::kError) {
        db_.insertJobLog(m.jobId, m.message, "error");
        db_.updateJobStatus(m.jobId, "failed", 1, "", isoNow());
        releaseSlot(slot);
    }
}

void Dispatcher::releaseSlot(WorkerSlot& slot) {
    slot.busy         = false;
    slot.currentJobId = 0;
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].get() == &slot) {
            jobStartedAt_[i] = std::chrono::steady_clock::time_point{};
            break;
        }
    }
}

void Dispatcher::claimAndAssign() {
    for (size_t i = 0; i < slots_.size(); ++i) {
        WorkerSlot& slot = *slots_[i];
        if (!slot.ready || slot.busy || slot.sp.proc == platform::invalidProc()) continue;

        if (!slot.registered) {
            slot.registered = db_.upsertWorker(slot.id, "online", 0.0, 0.0, std::nullopt);
            if (!slot.registered) continue;
        }

        auto job = db_.claimNextJob(slot.id);
        if (!job.has_value()) {
            if (db_.lastClaimFailed()) {
                // Never let a failing claim look like an idle queue: that is
                // exactly how a missing column grant turned into a dispatcher
                // that ran forever without doing anything.
                std::cerr << "Claim failed for " << slot.id << ": "
                          << db_.lastError() << std::endl;
            }
            // Empty queue: stop asking on behalf of the remaining idle workers
            // this tick rather than issuing one pointless query per slot.
            return;
        }

        ipc::Assignment a;
        a.jobId    = job->id;
        a.fileName = job->fileName;
        a.fileType = job->fileType;
        a.args     = job->args;
        a.env      = job->env;
        a.fileUrl  = "http://" + opts_.backendHost + ":" +
                     std::to_string(opts_.backendPort) +
                     "/api/jobs/" + std::to_string(job->id) + "/file";

        if (!platform::writeAll(slot.sp.stdinW, ipc::makeAssign(a))) {
            // The worker died between the claim and the handoff. Put the job
            // back rather than leaving it stranded in 'running'.
            std::cerr << "Failed to hand job " << job->id << " to " << slot.id
                      << "; releasing it." << std::endl;
            db_.updateJobStatus(job->id, "queued", std::nullopt, "", "");
            continue;
        }

        slot.busy         = true;
        slot.currentJobId = job->id;
        jobStartedAt_[i]  = std::chrono::steady_clock::now();
        db_.insertJobLog(job->id, "Assigned to worker " + slot.id, "info");
        std::cout << "Job " << job->id << " (" << job->name << ") -> " << slot.id << std::endl;
    }
}

void Dispatcher::heartbeat() {
    for (auto& s : slots_) {
        const bool alive = s->sp.proc != platform::invalidProc() && platform::isAlive(s->sp.proc);
        const std::string status = !alive ? "offline" : (s->busy ? "busy" : "online");
        db_.upsertWorker(s->id, status, s->cpu, s->memoryMb,
                         s->busy ? std::optional<int>(s->currentJobId) : std::nullopt);
    }
}

void Dispatcher::checkCancellations() {
    std::vector<std::string> busyIds;
    for (auto& s : slots_) if (s->busy) busyIds.push_back(s->id);
    if (busyIds.empty()) return;

    // The API marks a job 'cancelled' in the database; nothing previously told
    // the worker, so the process kept running to completion. This closes that gap.
    for (const int jobId : db_.fetchCancelledJobs(busyIds)) {
        for (auto& s : slots_) {
            if (s->busy && s->currentJobId == jobId) {
                std::cout << "Cancelling job " << jobId << " on " << s->id << std::endl;
                platform::writeAll(s->sp.stdinW, ipc::makeCancel(jobId));
            }
        }
    }
}

void Dispatcher::checkTimeouts() {
    if (opts_.jobTimeoutSeconds <= 0) return;
    const auto now = std::chrono::steady_clock::now();

    for (size_t i = 0; i < slots_.size(); ++i) {
        WorkerSlot& s = *slots_[i];
        if (!s.busy || jobStartedAt_[i].time_since_epoch().count() == 0) continue;

        const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - jobStartedAt_[i]);
        if (age.count() >= opts_.jobTimeoutSeconds) {
            std::cout << "Job " << s.currentJobId << " exceeded "
                      << opts_.jobTimeoutSeconds << "s; cancelling." << std::endl;
            db_.insertJobLog(s.currentJobId,
                "Job exceeded the " + std::to_string(opts_.jobTimeoutSeconds) +
                " second time limit and was terminated.", "error");
            platform::writeAll(s.sp.stdinW, ipc::makeCancel(s.currentJobId));
            jobStartedAt_[i] = std::chrono::steady_clock::time_point{};
        }
    }
}

void Dispatcher::superviseWorkers() {
    for (auto& s : slots_) {
        if (s->sp.proc == platform::invalidProc()) continue;
        if (platform::isAlive(s->sp.proc)) continue;

        // The worker died. With threads this would have taken the whole engine
        // down; here it costs one job.
        if (s->busy && s->currentJobId > 0) {
            std::cerr << "Worker " << s->id << " died running job "
                      << s->currentJobId << std::endl;
            db_.insertJobLog(s->currentJobId,
                "Worker process terminated unexpectedly.", "error");
            db_.updateJobStatus(s->currentJobId, "failed", -1, "", isoNow());
        } else {
            std::cerr << "Worker " << s->id << " died while idle." << std::endl;
        }

        platform::closePipe(s->sp.stdinW);
        platform::closePipe(s->sp.stdoutR);
        platform::closePipe(s->sp.stderrR);
        platform::closeProc(s->sp.proc);
        releaseSlot(*s);

        s->restarts++;
        if (spawnWorker(*s)) {
            std::cout << "Respawned worker " << s->id
                      << " (restart #" << s->restarts << ")" << std::endl;
        }
    }
}

void Dispatcher::run(const std::atomic<bool>& shutdownRequested) {
    lastHeartbeat_   = std::chrono::steady_clock::now();
    lastCancelCheck_ = lastHeartbeat_;
    // Sweep once at startup too, so a crashed predecessor is cleaned up promptly.
    lastReclaim_     = lastHeartbeat_ - kReclaimPeriod;

    while (!shutdownRequested) {
        // 1. Always drain first. Everything else depends on not being deadlocked.
        for (auto& s : slots_) drain(*s);

        // 2. Workers that exited get reported and replaced.
        superviseWorkers();

        // 3. Hand out work.
        claimAndAssign();

        const auto now = std::chrono::steady_clock::now();
        if (now - lastHeartbeat_ >= kHeartbeatPeriod) {
            lastHeartbeat_ = now;
            heartbeat();
        }
        if (now - lastReclaim_ >= kReclaimPeriod) {
            lastReclaim_ = now;
            const int freed = db_.reclaimStaleJobs(60,
                "Job released: its worker stopped reporting, so the engine "
                "running it is gone.");
            if (freed > 0) {
                std::cout << "Released " << freed
                          << " job(s) orphaned by a dead engine." << std::endl;
            }
        }
        if (now - lastCancelCheck_ >= kCancelPeriod) {
            lastCancelCheck_ = now;
            checkCancellations();
            checkTimeouts();
        }

        std::this_thread::sleep_for(kTick);
    }
}

void Dispatcher::stop() {
    if (stopped_) return;
    stopped_ = true;

    std::cout << "Stopping workers..." << std::endl;
    for (auto& s : slots_) {
        if (s->sp.proc != platform::invalidProc()) {
            platform::writeAll(s->sp.stdinW, ipc::makeShutdown());
        }
    }

    // Give them a few seconds to exit cleanly, draining as they go so a worker
    // blocked on a full pipe can still finish.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        bool anyAlive = false;
        for (auto& s : slots_) {
            if (s->sp.proc != platform::invalidProc() && platform::isAlive(s->sp.proc)) {
                drain(*s);
                anyAlive = true;
            }
        }
        if (!anyAlive || std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto& s : slots_) {
        if (s->sp.proc == platform::invalidProc()) continue;

        if (platform::isAlive(s->sp.proc)) {
            std::cerr << "Worker " << s->id << " did not exit; terminating." << std::endl;
            platform::terminateProc(s->sp.proc);
        }

        // Any job still in flight must not be left stuck in 'running' forever.
        if (s->busy && s->currentJobId > 0) {
            db_.insertJobLog(s->currentJobId,
                "Engine shut down while this job was running.", "warn");
            db_.updateJobStatus(s->currentJobId, "failed", -1, "", isoNow());
        }

        db_.upsertWorker(s->id, "offline", 0.0, 0.0, std::nullopt);

        platform::closePipe(s->sp.stdinW);
        platform::closePipe(s->sp.stdoutR);
        platform::closePipe(s->sp.stderrR);
        platform::closeProc(s->sp.proc);
    }
    std::cout << "All workers stopped." << std::endl;
}
