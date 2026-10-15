#include "worker_proc.h"
#include "ipc.h"
#include "platform.h"
#include "metrics.h"
#include "job_command.h"
#include "http_client.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

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

/** Protocol out. Anything that is not a protocol line must go to stderr. */
void emit(const std::string& lineWithNewline) {
    platform::writeStdout(lineWithNewline);
}

void diag(const std::string& text) {
    std::fputs((text + "\n").c_str(), stderr);
    std::fflush(stderr);
}

/**
 * One job, running as a child process.
 *
 * Holds the child's handles and the sampler pointed at it. The worker owns at
 * most one of these at a time.
 */
class RunningJob {
public:
    RunningJob(const ipc::Assignment& a, platform::SpawnResult sp,
               std::string sandboxDir, bool keepSandbox)
        : assignment_(a), sp_(sp),
          sandboxDir_(std::move(sandboxDir)), keepSandbox_(keepSandbox) {
        sampler_.target(sp_.proc);
        sampler_.sample();              // establish the CPU baseline
        lastMetrics_ = std::chrono::steady_clock::now();
    }

    int jobId() const { return assignment_.jobId; }

    /** Returns false once the job has finished and been reported. */
    bool pump() {
        bool didSomething = false;
        char buf[4096];

        // Drain the job's stdout+stderr. Both are merged into the job log: the
        // dispatcher stores them as the job's output, exactly as before.
        if (outOpen_) {
            const int n = platform::readPipe(sp_.stdoutR, buf, sizeof(buf));
            if (n > 0)      { pushLines(buf, static_cast<size_t>(n)); didSomething = true; }
            else if (n < 0) { outOpen_ = false; }
        }
        if (errOpen_) {
            const int n = platform::readPipe(sp_.stderrR, buf, sizeof(buf));
            if (n > 0)      { pushLines(buf, static_cast<size_t>(n)); didSomething = true; }
            else if (n < 0) { errOpen_ = false; }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - lastMetrics_ >= std::chrono::seconds(1)) {
            lastMetrics_ = now;
            const ProcMetrics m = sampler_.sample();
            emit(ipc::makeMetrics(assignment_.jobId, m.cpuPercent, m.memoryMb));
        }

        if (!exited_ && platform::tryGetExitCode(sp_.proc, exitCode_)) {
            exited_ = true;
        }

        // Only finish once the process is gone AND both pipes are drained,
        // otherwise the last lines of output would be dropped.
        if (exited_ && !outOpen_ && !errOpen_) {
            finish();
            return false;
        }
        return didSomething || true;
    }

    void cancel() {
        if (cancelled_) return;
        cancelled_ = true;
        emit(ipc::makeLog(assignment_.jobId, "Cancellation requested; terminating process.", "warn"));
        platform::terminateProc(sp_.proc);
    }

    /** Hard stop used on shutdown; reports nothing further. */
    void abandon() {
        platform::terminateProc(sp_.proc);
        cleanup();
    }

private:
    void pushLines(const char* data, size_t len) {
        for (auto& l : reader_.feed(data, len)) {
            if (!l.empty()) emit(ipc::makeLog(assignment_.jobId, l, "info"));
        }
    }

    void finish() {
        const std::string tail = reader_.flush();
        if (!tail.empty()) emit(ipc::makeLog(assignment_.jobId, tail, "info"));

        if (cancelled_) {
            emit(ipc::makeLog(assignment_.jobId, "Process terminated by cancellation.", "warn"));
        } else {
            emit(ipc::makeLog(assignment_.jobId,
                              "Process exited with code " + std::to_string(exitCode_),
                              exitCode_ == 0 ? "success" : "error"));
        }
        emit(ipc::makeFinished(assignment_.jobId,
                               cancelled_ ? -1 : exitCode_,
                               isoNow()));
        cleanup();
    }

    void cleanup() {
        platform::closePipe(sp_.stdinW);
        platform::closePipe(sp_.stdoutR);
        platform::closePipe(sp_.stderrR);
        sampler_.clear();
        platform::closeProc(sp_.proc);
        // Closing the Job Object kills anything the job left running, so this
        // must come after the process itself is gone.
        platform::closeLimits(sp_.limits);
        removeSandbox();
    }

    void removeSandbox() {
        if (keepSandbox_ || sandboxDir_.empty()) return;

        // Only ever remove a directory this worker created, and only one whose
        // path still ends in the expected "job_<id>" segment - a guard against
        // ever deleting something else if baseDir is misconfigured.
        const std::string expected = "job_" + std::to_string(assignment_.jobId);
        std::error_code ec;
        const fs::path p(sandboxDir_);
        if (p.filename().string() != expected) {
            diag("refusing to remove unexpected sandbox path: " + sandboxDir_);
            return;
        }
        const auto removed = fs::remove_all(p, ec);
        if (ec) {
            diag("could not remove sandbox " + sandboxDir_ + ": " + ec.message());
        } else {
            diag("removed sandbox " + expected + " (" + std::to_string(removed) + " entries)");
        }
    }

    ipc::Assignment       assignment_;
    platform::SpawnResult sp_;
    ipc::LineReader       reader_;
    ProcSampler           sampler_;
    bool                  outOpen_  = true;
    bool                  errOpen_  = true;
    bool                  exited_   = false;
    bool                  cancelled_ = false;
    int                   exitCode_ = -1;
    std::string           sandboxDir_;
    bool                  keepSandbox_ = false;
    std::chrono::steady_clock::time_point lastMetrics_;
};

} // namespace

int runWorkerProcess(const WorkerOptions& opts) {
    HttpClient http(opts.backendHost, opts.backendPort);

    platform::PipeHandle in = platform::stdinPipe();
    ipc::LineReader      inReader;
    std::unique_ptr<RunningJob> job;
    bool running = true;

    emit(ipc::makeReady(opts.workerId, platform::currentPid()));
    diag("worker " + opts.workerId + " ready (pid " + std::to_string(platform::currentPid()) + ")");

    auto startJob = [&](const ipc::Assignment& a) {
        if (job) {
            emit(ipc::makeError(a.jobId, "Worker is already running a job"));
            return;
        }

        const std::string sandbox =
            (fs::path(opts.baseDir) / opts.workerId / ("job_" + std::to_string(a.jobId))).string();
        std::error_code ec;
        fs::create_directories(sandbox, ec);
        if (ec) {
            emit(ipc::makeError(a.jobId, "Cannot create sandbox: " + ec.message()));
            return;
        }

        emit(ipc::makeStarted(a.jobId, isoNow()));
        emit(ipc::makeLog(a.jobId, "Worker " + opts.workerId + " claimed the job.", "info"));

        const std::string fileName = a.fileName.empty() ? "job_file" : a.fileName;
        const std::string target   = (fs::path(sandbox) / fileName).string();

        emit(ipc::makeLog(a.jobId, "Downloading " + fileName + "...", "info"));
        if (!http.downloadJobFile(a.jobId, target) || !fs::exists(target, ec)) {
            emit(ipc::makeLog(a.jobId, "Failed to download the job file.", "error"));
            emit(ipc::makeFinished(a.jobId, 1, isoNow()));
            return;
        }

        std::string buildLog;
        JobCommand cmd = buildJobCommand(target, a.fileType, a.args, sandbox, buildLog);
        if (!buildLog.empty()) {
            ipc::LineReader blr;
            for (auto& l : blr.feed(buildLog.data(), buildLog.size())) {
                if (!l.empty()) emit(ipc::makeLog(a.jobId, l, "info"));
            }
            const std::string tail = blr.flush();
            if (!tail.empty()) emit(ipc::makeLog(a.jobId, tail, "info"));
        }
        if (!cmd.ok) {
            emit(ipc::makeLog(a.jobId, cmd.error, "error"));
            emit(ipc::makeFinished(a.jobId, 1, isoNow()));
            return;
        }

        emit(ipc::makeLog(a.jobId, "Running: " + cmd.exe, "info"));
        platform::SpawnLimits lim;
        lim.memoryMb = opts.maxMemoryMb;
        if (lim.memoryMb > 0) {
            emit(ipc::makeLog(a.jobId,
                "Memory limit: " + std::to_string(lim.memoryMb) + " MB", "info"));
        }
        auto sp = platform::spawnProcess(cmd.exe, cmd.args, a.env, sandbox, lim);
        if (!sp.ok) {
            emit(ipc::makeLog(a.jobId, "Failed to start the job: " + sp.error, "error"));
            emit(ipc::makeFinished(a.jobId, 1, isoNow()));
            return;
        }
        // The job reads no input; closing its stdin makes a program that waits
        // for input see EOF immediately instead of hanging forever.
        platform::closePipe(sp.stdinW);

        job.reset(new RunningJob(a, sp, sandbox, opts.keepSandbox));
    };

    while (running) {
        bool busyThisTick = false;

        // 1. Our own stdin: assignments, cancellation, shutdown.
        char buf[4096];
        const int n = platform::readPipe(in, buf, sizeof(buf));
        if (n > 0) {
            busyThisTick = true;
            for (const auto& line : inReader.feed(buf, static_cast<size_t>(n))) {
                if (line.empty()) continue;
                const ipc::Message m = ipc::parse(line);

                if (m.type == ipc::kAssign) {
                    startJob(m.assignment);
                } else if (m.type == ipc::kCancel) {
                    if (job && job->jobId() == m.jobId) job->cancel();
                } else if (m.type == ipc::kShutdown) {
                    diag("shutdown requested");
                    running = false;
                } else if (m.type.empty()) {
                    diag("ignoring unparseable message: " + m.rawError);
                }
            }
        } else if (n < 0) {
            // The dispatcher closed our stdin or died. Exit rather than linger
            // as an orphan with no way to receive further instructions.
            diag("stdin closed; exiting");
            running = false;
        }

        // 2. The running job.
        if (job) {
            if (!job->pump()) job.reset();
            busyThisTick = true;
        }

        if (!busyThisTick) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (job) job->abandon();
    diag("worker " + opts.workerId + " exiting");
    return 0;
}
